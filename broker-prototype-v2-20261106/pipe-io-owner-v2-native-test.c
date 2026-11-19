#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pipe-io-owner-v2.h"

static unsigned tests, failures, sequence;
#define CHECK(x) do { ++tests; if (!(x)) { ++failures; printf("FAIL %u: %s (err=%lu)\n",tests,#x,(unsigned long)GetLastError()); return 0; } } while (0)

typedef struct {
    HANDLE server;
    HANDLE client;
    HANDLE issuing_pipe;
    HANDLE event_handle;
    OVERLAPPED ov;
    unsigned char buffer[8];
    raya_pipe_owner owner;
} live_operation;

static HANDLE event_for_overlapped(HANDLE h) {
    return (HANDLE)((ULONG_PTR)h | (ULONG_PTR)1);
}

static int create_connected_pipe(live_operation *op) {
    wchar_t name[160];
    DWORD error, ignored=0;
    BOOL connected;
    OVERLAPPED connect_ov;
    HANDLE connect_event=NULL;
    op->server=INVALID_HANDLE_VALUE;
    op->client=INVALID_HANDLE_VALUE;
    op->issuing_pipe=INVALID_HANDLE_VALUE;
    op->event_handle=NULL;
    if (swprintf_s(name,160,L"\\\\.\\pipe\\RayaOwnerV2_%lu_%u",
        (unsigned long)GetCurrentProcessId(),++sequence)<0) return 0;
    op->server=CreateNamedPipeW(name,PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,4096,4096,0,NULL);
    if (op->server==INVALID_HANDLE_VALUE) return 0;
    op->client=CreateFileW(name,GENERIC_READ|GENERIC_WRITE,0,NULL,
        OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if (op->client==INVALID_HANDLE_VALUE) return 0;
    memset(&connect_ov,0,sizeof(connect_ov));
    connect_event=CreateEventW(NULL,TRUE,FALSE,NULL);
    if (!connect_event) return 0;
    connect_ov.hEvent=event_for_overlapped(connect_event);
    connected=ConnectNamedPipe(op->server,&connect_ov);
    if (!connected) {
        error=GetLastError();
        if (error==ERROR_IO_PENDING) {
            if (WaitForSingleObject(connect_event,5000)!=WAIT_OBJECT_0 ||
                !GetOverlappedResult(op->server,&connect_ov,&ignored,FALSE)) {
                CloseHandle(connect_event);
                return 0;
            }
        } else if (error!=ERROR_PIPE_CONNECTED) {
            CloseHandle(connect_event);
            return 0;
        }
    }
    CloseHandle(connect_event);
    if (!DuplicateHandle(GetCurrentProcess(),op->server,GetCurrentProcess(),
        &op->issuing_pipe,0,FALSE,DUPLICATE_SAME_ACCESS)) return 0;
    op->event_handle=CreateEventW(NULL,TRUE,FALSE,NULL);
    if (!op->event_handle) return 0;
    memset(&op->ov,0,sizeof(op->ov));
    op->ov.hEvent=event_for_overlapped(op->event_handle);
    raya_pipe_owner_init(&op->owner);
    return 1;
}

static int submit_pending_read(live_operation *op) {
    DWORD immediate=0, error;
    BOOL ok=ReadFile(op->issuing_pipe,op->buffer,sizeof(op->buffer),
        &immediate,&op->ov);
    if (ok) return 0; /* This harness specifically requires a pending read. */
    error=GetLastError();
    return error==ERROR_IO_PENDING;
}

static int peer_write(live_operation *op) {
    DWORD written=0;
    return WriteFile(op->client,"DATA",4,&written,NULL) && written==4;
}

static int collect_real_result(live_operation *op, BOOL expect_success) {
    DWORD bytes=0,error;
    BOOL ok=GetOverlappedResult(op->issuing_pipe,&op->ov,&bytes,FALSE);
    if (expect_success) {
        if (!ok || bytes!=4 || memcmp(op->buffer,"DATA",4)!=0) return 0;
        return raya_pipe_owner_reaper_collect(&op->owner,TRUE,TRUE,TRUE,ERROR_SUCCESS);
    }
    if (ok) return 0;
    error=GetLastError();
    return error==ERROR_OPERATION_ABORTED &&
        raya_pipe_owner_reaper_collect(&op->owner,TRUE,TRUE,FALSE,error);
}

static void close_successful_operation(live_operation *op) {
    if (op->event_handle) CloseHandle(op->event_handle);
    if (op->issuing_pipe!=INVALID_HANDLE_VALUE) CloseHandle(op->issuing_pipe);
    if (op->client!=INVALID_HANDLE_VALUE) CloseHandle(op->client);
    if (op->server!=INVALID_HANDLE_VALUE) CloseHandle(op->server);
}

static int run_normal_incomplete_then_reaper(void) {
    live_operation op={0};
    CHECK(create_connected_pipe(&op));
    CHECK(submit_pending_read(&op));
    CHECK(peer_write(&op));
    CHECK(WaitForSingleObject(op.event_handle,5000)==WAIT_OBJECT_0);
    /* Inject the observed bug: event is signaled but collector reports INCOMPLETE. */
    CHECK(raya_pipe_owner_caller_collect(&op.owner,TRUE,TRUE,FALSE,ERROR_IO_INCOMPLETE));
    CHECK(op.owner.state==RAYA_PIPE_OWNER_CALLER_PENDING);
    CHECK(!raya_pipe_owner_resources_may_be_freed(&op.owner));
    CHECK(raya_pipe_owner_handoff(&op.owner));
    CHECK(!raya_pipe_owner_caller_may_access(&op.owner));
    CHECK(raya_pipe_owner_reaper_take(&op.owner));
    CHECK(collect_real_result(&op,TRUE));
    CHECK(op.owner.state==RAYA_PIPE_OWNER_REAPER_TERMINAL);
    CHECK(raya_pipe_owner_release(&op.owner));
    CHECK(op.owner.release_commits==1);
    close_successful_operation(&op);
    return 1;
}

static int run_cancel_drain_uncertain_then_reaper(void) {
    live_operation op={0};
    CHECK(create_connected_pipe(&op));
    CHECK(submit_pending_read(&op));
    CHECK(CancelIoEx(op.issuing_pipe,&op.ov));
    CHECK(WaitForSingleObject(op.event_handle,5000)==WAIT_OBJECT_0);
    /* Inject a failed GetOverlappedResult in the synchronous cancellation drain. */
    CHECK(raya_pipe_owner_caller_collect(&op.owner,TRUE,TRUE,FALSE,ERROR_INVALID_HANDLE));
    CHECK(op.owner.state==RAYA_PIPE_OWNER_CALLER_UNCERTAIN);
    CHECK(!raya_pipe_owner_resources_may_be_freed(&op.owner));
    CHECK(raya_pipe_owner_handoff(&op.owner));
    CHECK(raya_pipe_owner_reaper_take(&op.owner));
    CHECK(collect_real_result(&op,FALSE));
    CHECK(op.owner.state==RAYA_PIPE_OWNER_REAPER_TERMINAL);
    CHECK(raya_pipe_owner_release(&op.owner));
    close_successful_operation(&op);
    return 1;
}

static int run_reaper_collection_failure_then_retry(void) {
    live_operation op={0};
    CHECK(create_connected_pipe(&op));
    CHECK(submit_pending_read(&op));
    CHECK(raya_pipe_owner_handoff(&op.owner));
    CHECK(raya_pipe_owner_reaper_take(&op.owner));
    /* Inject failed blocking collection in the former per-operation reaper thread. */
    CHECK(raya_pipe_owner_reaper_collect(&op.owner,FALSE,TRUE,FALSE,ERROR_ACCESS_DENIED));
    CHECK(op.owner.state==RAYA_PIPE_OWNER_REAPER_UNCERTAIN);
    CHECK(!raya_pipe_owner_resources_may_be_freed(&op.owner));
    CHECK(!raya_pipe_owner_release(&op.owner));
    CHECK(peer_write(&op));
    CHECK(WaitForSingleObject(op.event_handle,5000)==WAIT_OBJECT_0);
    CHECK(raya_pipe_owner_reaper_retry(&op.owner));
    CHECK(collect_real_result(&op,TRUE));
    CHECK(op.owner.state==RAYA_PIPE_OWNER_REAPER_TERMINAL);
    CHECK(raya_pipe_owner_release(&op.owner));
    close_successful_operation(&op);
    return 1;
}

int main(void) {
    unsigned run;
    for (run=0;run<10;++run) {
        if (!run_normal_incomplete_then_reaper() ||
            !run_cancel_drain_uncertain_then_reaper() ||
            !run_reaper_collection_failure_then_retry()) {
            printf("Native ownership test failed on batch %u\n",run+1);
            return 1;
        }
    }
    printf("Native named-pipe ownership: %u assertions, %u failures; 10 batches x 3 scenarios\n",
        tests,failures);
    return failures?1:0;
}
