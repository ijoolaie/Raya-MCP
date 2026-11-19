#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "reaper-integrated-v2.h"
#include "reaper-waitplan-v2.h"
#include "pipe-completion-context-v2.h"
#define N 70
#define CHECK(x) do { ++tests; if(!(x)) { ++failures; printf("FAIL %d: %s (err=%lu)\n",tests,#x,(unsigned long)GetLastError()); } } while(0)
static int tests,failures;
typedef struct {
    raya_integrated_op integrated;
    OVERLAPPED ov;
    unsigned char byte;
    HANDLE server,client,event_handle;
    unsigned char sent;
} test_op;
static HANDLE overlapped_event(HANDLE h) { return (HANDLE)((ULONG_PTR)h | (ULONG_PTR)1); }
static int make_pair(test_op *o,unsigned index) {
    wchar_t name[160]; BOOL connected; DWORD e;
    swprintf_s(name,160,L"\\\\.\\pipe\\RayaPollMulti_%lu_%u",
       (unsigned long)GetCurrentProcessId(),index);
    o->server=CreateNamedPipeW(name,PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
       PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,4096,4096,0,NULL);
    if(o->server==INVALID_HANDLE_VALUE) return 0;
    o->client=CreateFileW(name,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL,NULL);
    if(o->client==INVALID_HANDLE_VALUE) return 0;
    connected=ConnectNamedPipe(o->server,NULL);
    if(!connected) { e=GetLastError(); if(e!=ERROR_PIPE_CONNECTED) return 0; }
    o->event_handle=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!o->event_handle) return 0;
    o->ov.hEvent=overlapped_event(o->event_handle);
    o->integrated.base.state=RAYA_REAPER_OP_CALLER_PENDING;
    o->integrated.completion_event=o->event_handle;
    o->sent=(unsigned char)(index+1);
    return 1;
}
int main(void) {
    static raya_reaper_integrated state;
    static test_op ops[N];
    raya_integrated_op *promoted=NULL;
    raya_reaper_waitplan failed_plan,plan;
    raya_reaper_op *resolved=NULL;
    HANDLE bad_wake=NULL,recovery_wake=NULL;
    DWORD immediate=0,err=0,wr,bytes=0;
    BOOL ok;
    raya_pipe_completion_class cls;
    unsigned i,completed=0,passes=0,scanned=0;
    memset(&state,0,sizeof(state)); memset(ops,0,sizeof(ops));
    bad_wake=CreateEventW(NULL,TRUE,FALSE,NULL);
    recovery_wake=CreateEventW(NULL,TRUE,FALSE,NULL);
    CHECK(bad_wake!=NULL && recovery_wake!=NULL);
    CHECK(raya_reaper_integrated_init(&state));
    if(!bad_wake || !recovery_wake) goto cleanup;
    for(i=0;i<N;i++) {
        ops[i].server=ops[i].client=INVALID_HANDLE_VALUE;
        CHECK(make_pair(&ops[i],i));
        if(ops[i].server==INVALID_HANDLE_VALUE || ops[i].client==INVALID_HANDLE_VALUE ||
           !ops[i].event_handle) goto cleanup;
        ok=ReadFile(ops[i].server,&ops[i].byte,1,&immediate,&ops[i].ov);
        CHECK(!ok); err=GetLastError(); CHECK(err==ERROR_IO_PENDING);
        CHECK(raya_reaper_integrated_admit(&state));
        CHECK(raya_reaper_integrated_reserve(&state,&ops[i].integrated));
        CHECK(raya_reaper_integrated_handoff(&state,&ops[i].integrated));
    }
    for(i=0;i<N;i++) {
        CHECK(raya_reaper_integrated_promote_one(&state,&promoted)==RAYA_INTEGRATED_OK);
        CHECK(promoted==&ops[i].integrated);
    }
    CHECK(state.registry.count==N && state.core.outstanding==N &&
          state.reserved_slots==N);
    CHECK(raya_reaper_waitplan_build(&state.registry,bad_wake,&failed_plan)==RAYA_WAITPLAN_READY);
    CHECK(failed_plan.waitset.operation_count==63);
    CHECK(CloseHandle(bad_wake)); bad_wake=NULL;
    wr=WaitForMultipleObjects(failed_plan.waitset.handle_count,
                              failed_plan.waitset.handles,FALSE,0);
    CHECK(wr==WAIT_FAILED); err=GetLastError(); CHECK(err==ERROR_INVALID_HANDLE);
    CHECK(raya_reaper_mark_degraded_cause(&state.core,RAYA_RECOVERY_CAUSE_WAIT_FAILED));
    /* Complete in reverse order to force recovery across rotating windows. */
    for(i=0;i<N;i++) {
        unsigned k=N-1-i;
        CHECK(WriteFile(ops[k].client,&ops[k].sent,1,&bytes,NULL));
        CHECK(bytes==1);
    }
    while(completed<N && passes<1000) {
        size_t slot;
        CHECK(raya_reaper_waitplan_build(&state.registry,recovery_wake,&plan)==RAYA_WAITPLAN_READY);
        passes++;
        for(slot=0;slot<plan.waitset.operation_count && completed<N;slot++) {
            HANDLE ev=plan.waitset.handles[slot+1];
            if(WaitForSingleObject(ev,0)==WAIT_OBJECT_0) {
                raya_waitplan_result rr=raya_reaper_waitplan_resolve(
                    &state.registry,&plan,WAIT_OBJECT_0+1+(DWORD)slot,&resolved,&ev);
                CHECK(rr==RAYA_WAITPLAN_OPERATION);
                if(rr!=RAYA_WAITPLAN_OPERATION) continue;
                {
                    unsigned j;
                    test_op *found=NULL;
                    for(j=0;j<N;j++) if(&ops[j].integrated.base==resolved) { found=&ops[j]; break; }
                    CHECK(found!=NULL);
                    if(!found) continue;
                    CHECK(raya_reaper_integrated_claim(&state,&found->integrated,ev));
                    ok=GetOverlappedResult(found->server,&found->ov,&bytes,FALSE);
                    err=ok?ERROR_SUCCESS:GetLastError();
                    cls=raya_classify_pipe_completion(TRUE,TRUE,ok,err);
                    CHECK(cls==RAYA_PIPE_COMPLETION_TERMINAL_SUCCESS);
                    CHECK(bytes==1 && found->byte==found->sent);
                    CHECK(raya_reaper_integrated_collect(&state,&found->integrated,
                         RAYA_INTEGRATED_TERMINAL_SUCCESS)==RAYA_INTEGRATED_OK);
                    completed++;
                }
            } else {
                scanned++;
            }
        }
    }
    CHECK(completed==N);
    CHECK(passes>=2); /* More than one 63-operation window was required. */
    CHECK(state.registry.count==0 && state.core.outstanding==0 &&
          state.reserved_slots==0);
    for(i=0;i<N;i++) CHECK(ops[i].integrated.base.state==RAYA_REAPER_OP_RELEASED);
    CHECK(state.core.lifecycle==RAYA_REAPER_DEGRADED);
    CHECK(state.core.degradation_causes==RAYA_RECOVERY_CAUSE_WAIT_FAILED);
    CHECK(!raya_reaper_integrated_can_exit(&state));
cleanup:
    for(i=0;i<N;i++) {
        if(ops[i].server && ops[i].server!=INVALID_HANDLE_VALUE) {
            if(ops[i].integrated.base.state!=RAYA_REAPER_OP_RELEASED)
                (void)CancelIoEx(ops[i].server,&ops[i].ov);
            CloseHandle(ops[i].server);
        }
        if(ops[i].client && ops[i].client!=INVALID_HANDLE_VALUE) CloseHandle(ops[i].client);
        if(ops[i].event_handle) CloseHandle(ops[i].event_handle);
    }
    if(bad_wake) CloseHandle(bad_wake);
    if(recovery_wake) CloseHandle(recovery_wake);
    printf("Multi-operation polling recovery: %d assertions, %d failures, completed=%u/%u passes=%u scanned=%u\n",
           tests,failures,completed,N,passes,scanned);
    return failures?1:0;
}
