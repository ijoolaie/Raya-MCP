#define WIN32_LEAN_AND_MEAN
#include "pipe-io-v2.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    OVERLAPPED ov;
    HANDLE pipe; /* duplicated handle owned until the I/O is fully drained */
    HANDLE event;
    unsigned char *buffer;
} pending_io;

static DWORD remaining_ms(ULONGLONG end) {
    ULONGLONG now=GetTickCount64();
    if(now>=end) return 0;
    if(end-now>0xfffffffeULL) return 0xfffffffeUL;
    return (DWORD)(end-now);
}

static void free_pending(pending_io *p) {
    if(!p) return;
    if(p->event) CloseHandle(p->event);
    if(p->pipe && p->pipe!=INVALID_HANDLE_VALUE) CloseHandle(p->pipe);
    free(p->buffer);
    free(p);
}

static DWORD WINAPI reap_cancelled_io(void *arg) {
    pending_io *p=(pending_io*)arg;
    DWORD transferred=0;
    /* The duplicated handle remains valid even if the caller closes its handle. */
    (void)GetOverlappedResult(p->pipe,&p->ov,&transferred,TRUE);
    free_pending(p);
    return 0;
}

static raya_pipe_io_status one_io(HANDLE pipe, unsigned char *buffer, DWORD length,
    BOOL write_operation, ULONGLONG deadline, DWORD *transferred_out) {
    pending_io *p;
    DWORD transferred=0, wait_ms, error;
    BOOL ok, wait_error=FALSE;
    HANDLE reaper;

    if(transferred_out) *transferred_out=0;
    p=(pending_io*)calloc(1,sizeof(*p));
    if(!p) return RAYA_PIPE_IO_ERROR;
    p->buffer=(unsigned char*)malloc(length ? length : 1);
    if(!p->buffer) { free_pending(p); return RAYA_PIPE_IO_ERROR; }
    if(write_operation) memcpy(p->buffer,buffer,length);

    p->pipe=NULL;
    if(!DuplicateHandle(GetCurrentProcess(),pipe,GetCurrentProcess(),&p->pipe,
        0,FALSE,DUPLICATE_SAME_ACCESS)) {
        free_pending(p);
        return RAYA_PIPE_IO_ERROR;
    }
    p->event=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!p->event) { free_pending(p); return RAYA_PIPE_IO_ERROR; }
    p->ov.hEvent=p->event;

    /* Named-pipe overlapped I/O: TRUE returns the immediate byte count here.
       For FALSE + ERROR_IO_PENDING, this output is ignored and collected later. */
    ok=write_operation ? WriteFile(p->pipe,p->buffer,length,&transferred,&p->ov)
                       : ReadFile(p->pipe,p->buffer,length,&transferred,&p->ov);
    if(ok) {
        /* Immediate completion: do not call GetOverlappedResult on this branch. */
    } else {
        error=GetLastError();
        if(error!=ERROR_IO_PENDING) goto failed;

        wait_ms=remaining_ms(deadline);
        if(wait_ms==0) {
            (void)CancelIoEx(p->pipe,&p->ov);
            goto cancel_wait;
        }
        {
            DWORD wr=WaitForSingleObject(p->event,wait_ms);
            if(wr!=WAIT_OBJECT_0) {
                if(wr!=WAIT_TIMEOUT) wait_error=TRUE;
                (void)CancelIoEx(p->pipe,&p->ov);
                goto cancel_wait;
            }
        }
        if(!GetOverlappedResult(p->pipe,&p->ov,&transferred,FALSE)) {
            error=GetLastError();
            goto failed;
        }
    }

    if(transferred_out) *transferred_out=transferred;
    if(!write_operation && transferred) memcpy(buffer,p->buffer,transferred);
    free_pending(p);
    return transferred ? RAYA_PIPE_IO_OK : RAYA_PIPE_IO_DISCONNECTED;

cancel_wait:
    /* Drain cancellation before releasing kernel-visible state. */
    {
        DWORD drain=WaitForSingleObject(p->event,2000);
        if(drain==WAIT_OBJECT_0) {
            if(GetOverlappedResult(p->pipe,&p->ov,&transferred,FALSE)) {
                if(transferred_out) *transferred_out=transferred;
                if(!write_operation && transferred) memcpy(buffer,p->buffer,transferred);
                free_pending(p);
                return transferred ? RAYA_PIPE_IO_OK : RAYA_PIPE_IO_DISCONNECTED;
            }
            error=GetLastError();
            free_pending(p);
            if(error==ERROR_BROKEN_PIPE || error==ERROR_PIPE_NOT_CONNECTED ||
               error==ERROR_NO_DATA || error==ERROR_HANDLE_EOF)
                return RAYA_PIPE_IO_DISCONNECTED;
            if(error==ERROR_OPERATION_ABORTED && !wait_error)
                return RAYA_PIPE_IO_TIMEOUT;
            return RAYA_PIPE_IO_ERROR;
        }
        reaper=CreateThread(NULL,0,reap_cancelled_io,p,0,NULL);
        if(reaper) {
            CloseHandle(reaper);
        } else {
            /* Safety over reclamation: never free memory still owned by the kernel. */
            /* Exceptional leak includes the duplicated pipe handle and event. */
        }
        return (wait_error || drain!=WAIT_TIMEOUT) ? RAYA_PIPE_IO_ERROR : RAYA_PIPE_IO_TIMEOUT;
    }

failed:
    if(error==ERROR_BROKEN_PIPE || error==ERROR_PIPE_NOT_CONNECTED ||
       error==ERROR_NO_DATA || error==ERROR_HANDLE_EOF) {
        free_pending(p);
        return RAYA_PIPE_IO_DISCONNECTED;
    }
    /* Non-pending failed requests are complete and may be safely reclaimed. */
    free_pending(p);
    return RAYA_PIPE_IO_ERROR;
}

static raya_pipe_io_status transfer(HANDLE pipe, unsigned char *buffer, size_t length,
    BOOL write_operation, DWORD deadline_ms) {
    size_t offset=0;
    ULONGLONG end;
    if(pipe==NULL || pipe==INVALID_HANDLE_VALUE || (!buffer && length))
        return RAYA_PIPE_IO_INVALID;
    if(length==0) return RAYA_PIPE_IO_OK;
    if(deadline_ms==0) return RAYA_PIPE_IO_INVALID;
    end=GetTickCount64()+(ULONGLONG)deadline_ms;

    while(offset<length) {
        size_t remain=length-offset;
        DWORD chunk=remain>65536u ? 65536u : (DWORD)remain;
        DWORD done=0;
        raya_pipe_io_status s=one_io(pipe,buffer+offset,chunk,write_operation,end,&done);
        if(s!=RAYA_PIPE_IO_OK) return s;
        if(done==0 || done>chunk) return RAYA_PIPE_IO_DISCONNECTED;
        offset+=done;
        /* Enforce the total deadline even when the final chunk wins a cancellation race. */
        if(remaining_ms(end)==0) return RAYA_PIPE_IO_TIMEOUT;
    }
    return RAYA_PIPE_IO_OK;
}

raya_pipe_io_status raya_pipe_write_all(HANDLE pipe,const unsigned char *data,
    size_t length,DWORD deadline_ms) {
    return transfer(pipe,(unsigned char*)data,length,TRUE,deadline_ms);
}

raya_pipe_io_status raya_pipe_read_exact(HANDLE pipe,unsigned char *data,
    size_t length,DWORD deadline_ms) {
    return transfer(pipe,data,length,FALSE,deadline_ms);
}
