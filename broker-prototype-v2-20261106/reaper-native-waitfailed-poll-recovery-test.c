#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "reaper-integrated-v2.h"
#include "reaper-waitplan-v2.h"
#include "pipe-completion-context-v2.h"

#define CHECK(x) do { ++tests; if(!(x)) { ++failures; printf("FAIL %d: %s (err=%lu)\n",tests,#x,(unsigned long)GetLastError()); } } while(0)
static int tests,failures;
typedef struct {
    raya_integrated_op integrated;
    OVERLAPPED ov;
    unsigned char byte;
} test_op;

static HANDLE overlapped_event(HANDLE h) {
    return (HANDLE)((ULONG_PTR)h | (ULONG_PTR)1);
}
static int make_pair(HANDLE *server,HANDLE *client) {
    wchar_t name[160];
    BOOL connected;
    DWORD e;
    *server=INVALID_HANDLE_VALUE; *client=INVALID_HANDLE_VALUE;
    if(swprintf_s(name,160,L"\\\\.\\pipe\\RayaPollRecovery_%lu",
       (unsigned long)GetCurrentProcessId())<0) return 0;
    *server=CreateNamedPipeW(name,PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
       PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,4096,4096,0,NULL);
    if(*server==INVALID_HANDLE_VALUE) return 0;
    *client=CreateFileW(name,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,NULL);
    if(*client==INVALID_HANDLE_VALUE) return 0;
    connected=ConnectNamedPipe(*server,NULL);
    if(!connected) { e=GetLastError(); if(e!=ERROR_PIPE_CONNECTED) return 0; }
    return 1;
}

int main(void) {
    raya_reaper_integrated state;
    raya_integrated_op *promoted=NULL;
    raya_reaper_waitplan plan;
    raya_reaper_op *resolved=NULL;
    HANDLE server=INVALID_HANDLE_VALUE,client=INVALID_HANDLE_VALUE;
    HANDLE wake=NULL,event_handle=NULL,resolved_event=NULL;
    test_op op;
    DWORD immediate=0,err=0,wr,bytes=0;
    BOOL ok;
    raya_pipe_completion_class cls;
    unsigned char sent=0x5A;
    int transferred=0, poll_count=0;

    memset(&state,0,sizeof(state)); memset(&op,0,sizeof(op));
    wake=CreateEventW(NULL,TRUE,FALSE,NULL);
    event_handle=CreateEventW(NULL,TRUE,FALSE,NULL);
    CHECK(wake!=NULL && event_handle!=NULL);
    CHECK(make_pair(&server,&client));
    if(wake==NULL || event_handle==NULL || server==INVALID_HANDLE_VALUE ||
       client==INVALID_HANDLE_VALUE) goto cleanup;

    op.integrated.base.state=RAYA_REAPER_OP_CALLER_PENDING;
    op.integrated.completion_event=event_handle;
    op.ov.hEvent=overlapped_event(event_handle);
    ok=ReadFile(server,&op.byte,1,&immediate,&op.ov);
    CHECK(!ok); err=GetLastError(); CHECK(err==ERROR_IO_PENDING);

    CHECK(raya_reaper_integrated_init(&state));
    CHECK(raya_reaper_integrated_admit(&state));
    CHECK(raya_reaper_integrated_reserve(&state,&op.integrated));
    CHECK(raya_reaper_integrated_handoff(&state,&op.integrated));
    CHECK(raya_reaper_integrated_promote_one(&state,&promoted)==RAYA_INTEGRATED_OK);
    CHECK(promoted==&op.integrated);
    CHECK(state.registry.count==1 && state.core.outstanding==1 &&
          state.reserved_slots==1);

    CHECK(raya_reaper_waitplan_build(&state.registry,wake,&plan)==RAYA_WAITPLAN_READY);
    CHECK(plan.waitset.handle_count==2);
    /* Invalidate only the wake handle; the overlapped operation's event stays valid. */
    CHECK(CloseHandle(wake)); wake=NULL;
    wr=WaitForMultipleObjects(plan.waitset.handle_count,plan.waitset.handles,FALSE,100);
    CHECK(wr==WAIT_FAILED);
    err=GetLastError(); /* Capture immediately after WAIT_FAILED. */
    CHECK(err==ERROR_INVALID_HANDLE);
    CHECK(raya_reaper_mark_degraded_cause(&state.core,RAYA_RECOVERY_CAUSE_WAIT_FAILED));
    CHECK(state.registry.count==1 && state.core.outstanding==1 &&
          state.reserved_slots==1);
    CHECK(WaitForSingleObject(event_handle,0)==WAIT_TIMEOUT);

    /* Peer completes after the wait-set failure. Polling-only recovery must find it. */
    CHECK(WriteFile(client,&sent,1,&bytes,NULL));
    CHECK(bytes==1);
    for(poll_count=0;poll_count<100 && !transferred;poll_count++) {
        HANDLE snapshot_event=NULL;
        raya_reaper_op *snapshot_op=NULL;
        /* This harness is single-worker/single-owner: Registry cannot mutate during this snapshot. */
        if(state.registry.count==1) {
            snapshot_op=state.registry.entries[0].op;
            snapshot_event=state.registry.entries[0].event_handle;
        }
        CHECK(snapshot_op==&op.integrated.base && snapshot_event==event_handle);
        if(WaitForSingleObject(snapshot_event,0)==WAIT_OBJECT_0) {
            CHECK(raya_reaper_waitplan_resolve(&state.registry,&plan,
                 WAIT_OBJECT_0+1,&resolved,&resolved_event)==RAYA_WAITPLAN_OPERATION);
            CHECK(resolved==&op.integrated.base && resolved_event==event_handle);
            CHECK(raya_reaper_integrated_claim(&state,&op.integrated,resolved_event));
            ok=GetOverlappedResult(server,&op.ov,&bytes,FALSE);
            err=ok?ERROR_SUCCESS:GetLastError();
            cls=raya_classify_pipe_completion(TRUE,TRUE,ok,err);
            CHECK(cls==RAYA_PIPE_COMPLETION_TERMINAL_SUCCESS);
            CHECK(bytes==1 && op.byte==sent);
            CHECK(raya_reaper_integrated_collect(&state,&op.integrated,
                 RAYA_INTEGRATED_TERMINAL_SUCCESS)==RAYA_INTEGRATED_OK);
            transferred=1;
        } else {
            Sleep(1);
        }
    }
    CHECK(transferred);
    CHECK(poll_count<100);
    CHECK(state.registry.count==0 && state.core.outstanding==0 &&
          state.reserved_slots==0);
    CHECK(op.integrated.base.state==RAYA_REAPER_OP_RELEASED);
    CHECK(state.core.lifecycle==RAYA_REAPER_DEGRADED);
    CHECK(state.core.degradation_causes==RAYA_RECOVERY_CAUSE_WAIT_FAILED);
    CHECK(!raya_reaper_integrated_can_exit(&state));

cleanup:
    if(client!=INVALID_HANDLE_VALUE) CloseHandle(client);
    if(server!=INVALID_HANDLE_VALUE) {
        if(!transferred) (void)CancelIoEx(server,&op.ov);
        CloseHandle(server);
    }
    if(wake!=NULL) CloseHandle(wake);
    if(event_handle!=NULL) CloseHandle(event_handle);
    printf("Polling recovery after native WAIT_FAILED: %d assertions, %d failures, polls=%d\n",
           tests,failures,poll_count);
    return failures?1:0;
}
