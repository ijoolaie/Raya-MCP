#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "reaper-integrated-v2.h"
#include "reaper-waitplan-v2.h"
#include "pipe-completion-context-v2.h"

#define SUCCESS_OPS 70
#define TOTAL_OPS 71
#define CHECK(x) do { InterlockedIncrement(&g_tests); if(!(x)) { InterlockedIncrement(&g_failures); printf("FAIL: %s (err=%lu)\n",#x,(unsigned long)GetLastError()); } } while(0)

typedef struct {
    raya_integrated_op integrated;
    OVERLAPPED ov;
    unsigned char byte;
    DWORD transferred;
    DWORD completion_error;
} io_operation;

typedef struct {
    CRITICAL_SECTION lock;
    HANDLE wake;
    HANDLE server;
    raya_reaper_integrated state;
    io_operation ops[TOTAL_OPS];
    volatile LONG collected_success;
    volatile LONG collected_aborted;
    volatile LONG worker_error;
    volatile LONG wait_plan_builds;
} test_context;

static test_context g;
static test_context disconnect_context;
static volatile LONG g_tests=0, g_failures=0;

static HANDLE event_for_overlapped(HANDLE event_handle) {
    return (HANDLE)((ULONG_PTR)event_handle | (ULONG_PTR)1);
}

static int make_pipe_pair(HANDLE *server, HANDLE *client) {
    wchar_t name[160];
    BOOL connected;
    DWORD error;
    *server=INVALID_HANDLE_VALUE;
    *client=INVALID_HANDLE_VALUE;
    if(swprintf_s(name,160,L"\\\\.\\pipe\\RayaIntegratedLive_%lu",
                  (unsigned long)GetCurrentProcessId())<0) return 0;
    *server=CreateNamedPipeW(name,PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,4096,4096,0,NULL);
    if(*server==INVALID_HANDLE_VALUE) return 0;
    *client=CreateFileW(name,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,NULL);
    if(*client==INVALID_HANDLE_VALUE) return 0;
    connected=ConnectNamedPipe(*server,NULL);
    if(!connected) {
        error=GetLastError();
        if(error!=ERROR_PIPE_CONNECTED) return 0;
    }
    return 1;
}

static DWORD WINAPI worker_main(LPVOID arg) {
    test_context *ctx=(test_context *)arg;
    for(;;) {
        raya_reaper_waitplan plan;
        raya_waitplan_result built;
        DWORD wr;
        EnterCriticalSection(&ctx->lock);
        if(!ResetEvent(ctx->wake)) {
            (void)raya_reaper_mark_degraded_cause(&ctx->state.core,RAYA_RECOVERY_CAUSE_WAITSET_INVALID);
            InterlockedExchange(&ctx->worker_error,1);
            LeaveCriticalSection(&ctx->lock);
            return 1;
        }
        for(;;) {
            raya_integrated_op *promoted=NULL;
            raya_integrated_result pr=raya_reaper_integrated_promote_one(
                                           &ctx->state,&promoted);
            if(pr==RAYA_INTEGRATED_EMPTY) break;
            if(pr!=RAYA_INTEGRATED_OK || promoted==NULL) {
                (void)raya_reaper_mark_degraded_cause(&ctx->state.core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
                InterlockedExchange(&ctx->worker_error,2);
                LeaveCriticalSection(&ctx->lock);
                return 2;
            }
        }
        if(raya_reaper_integrated_can_exit(&ctx->state)) {
            LeaveCriticalSection(&ctx->lock);
            return 0;
        }
        built=raya_reaper_waitplan_build(&ctx->state.registry,ctx->wake,&plan);
        if(built!=RAYA_WAITPLAN_READY) {
            (void)raya_reaper_mark_degraded_cause(&ctx->state.core,RAYA_RECOVERY_CAUSE_WAITSET_INVALID);
            InterlockedExchange(&ctx->worker_error,3);
            LeaveCriticalSection(&ctx->lock);
            return 3;
        }
        InterlockedIncrement(&ctx->wait_plan_builds);
        LeaveCriticalSection(&ctx->lock);

        wr=WaitForMultipleObjects(plan.waitset.handle_count,
                                  plan.waitset.handles,FALSE,100);
        if(wr==WAIT_TIMEOUT || wr==WAIT_OBJECT_0) continue;
        if(wr==WAIT_FAILED) {
            DWORD wait_error=GetLastError();
            (void)wait_error;
            EnterCriticalSection(&ctx->lock);
            (void)raya_reaper_mark_degraded_cause(&ctx->state.core,RAYA_RECOVERY_CAUSE_WAIT_FAILED);
            InterlockedExchange(&ctx->worker_error,4);
            LeaveCriticalSection(&ctx->lock);
            return 4;
        }
        {
            raya_reaper_op *base=NULL;
            HANDLE event_handle=NULL;
            raya_waitplan_result resolved;
            io_operation *op;
            BOOL api_ok;
            DWORD bytes=0, error=ERROR_SUCCESS;
            raya_pipe_completion_class classification;

            EnterCriticalSection(&ctx->lock);
            resolved=raya_reaper_waitplan_resolve(&ctx->state.registry,&plan,wr,
                                                  &base,&event_handle);
            if(resolved!=RAYA_WAITPLAN_OPERATION || base==NULL ||
               !raya_reaper_integrated_claim(&ctx->state,
                    (raya_integrated_op *)base,event_handle)) {
                (void)raya_reaper_mark_degraded_cause(&ctx->state.core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
                InterlockedExchange(&ctx->worker_error,5);
                LeaveCriticalSection(&ctx->lock);
                return 5;
            }
            op=(io_operation *)base;
            LeaveCriticalSection(&ctx->lock);

            if(WaitForSingleObject(event_handle,0)!=WAIT_OBJECT_0) {
                classification=RAYA_PIPE_COMPLETION_UNCERTAIN;
            } else {
                api_ok=GetOverlappedResult(ctx->server,&op->ov,&bytes,FALSE);
                if(!api_ok) error=GetLastError();
                classification=raya_classify_pipe_completion(
                    TRUE,TRUE,api_ok,error);
            }
            op->transferred=bytes;
            op->completion_error=error;

            EnterCriticalSection(&ctx->lock);
            if(classification==RAYA_PIPE_COMPLETION_INCOMPLETE) {
                if(raya_reaper_integrated_collect(&ctx->state,
                    &op->integrated,RAYA_INTEGRATED_INCOMPLETE)!=RAYA_INTEGRATED_OK) {
                    (void)raya_reaper_mark_degraded_cause(&ctx->state.core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
                    InterlockedExchange(&ctx->worker_error,6);
                    LeaveCriticalSection(&ctx->lock);
                    return 6;
                }
            } else if(classification==RAYA_PIPE_COMPLETION_TERMINAL_SUCCESS) {
                if(bytes!=1 || op->byte==0 ||
                   raya_reaper_integrated_collect(&ctx->state,
                    &op->integrated,RAYA_INTEGRATED_TERMINAL_SUCCESS)!=RAYA_INTEGRATED_OK) {
                    (void)raya_reaper_mark_degraded_cause(&ctx->state.core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
                    InterlockedExchange(&ctx->worker_error,7);
                    LeaveCriticalSection(&ctx->lock);
                    return 7;
                }
                InterlockedIncrement(&ctx->collected_success);
            } else if(classification==RAYA_PIPE_COMPLETION_TERMINAL_ABORTED) {
                if(raya_reaper_integrated_collect(&ctx->state,
                    &op->integrated,RAYA_INTEGRATED_TERMINAL_ABORTED)!=RAYA_INTEGRATED_OK) {
                    (void)raya_reaper_mark_degraded_cause(&ctx->state.core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
                    InterlockedExchange(&ctx->worker_error,8);
                    LeaveCriticalSection(&ctx->lock);
                    return 8;
                }
                InterlockedIncrement(&ctx->collected_aborted);
            } else {
                (void)raya_reaper_integrated_collect(&ctx->state,
                    &op->integrated,RAYA_INTEGRATED_UNCERTAIN_RESULT);
                InterlockedExchange(&ctx->worker_error,9);
                LeaveCriticalSection(&ctx->lock);
                return 9;
            }
            LeaveCriticalSection(&ctx->lock);
        }
    }
}


static void test_disconnect_retains_uncertain_ownership(void) {
    test_context *ctx=&disconnect_context;
    HANDLE client=INVALID_HANDLE_VALUE, worker=NULL, event_handle=NULL;
    DWORD immediate=0, wait_result;
    BOOL ok;
    DWORD error;
    memset(ctx,0,sizeof(*ctx));
    InitializeCriticalSection(&ctx->lock);
    ctx->wake=CreateEventW(NULL,TRUE,FALSE,NULL);
    ctx->server=INVALID_HANDLE_VALUE;
    CHECK(ctx->wake!=NULL);
    CHECK(make_pipe_pair(&ctx->server,&client));
    CHECK(raya_reaper_integrated_init(&ctx->state));
    if(ctx->wake==NULL || ctx->server==INVALID_HANDLE_VALUE ||
       client==INVALID_HANDLE_VALUE) goto disconnect_cleanup;

    event_handle=CreateEventW(NULL,TRUE,FALSE,NULL);
    CHECK(event_handle!=NULL);
    if(event_handle==NULL) goto disconnect_cleanup;
    ctx->ops[0].integrated.base.state=RAYA_REAPER_OP_CALLER_PENDING;
    ctx->ops[0].integrated.completion_event=event_handle;
    ctx->ops[0].ov.hEvent=event_for_overlapped(event_handle);
    ok=ReadFile(ctx->server,&ctx->ops[0].byte,1,&immediate,&ctx->ops[0].ov);
    CHECK(!ok);
    error=GetLastError();
    CHECK(error==ERROR_IO_PENDING);

    EnterCriticalSection(&ctx->lock);
    CHECK(raya_reaper_integrated_admit(&ctx->state));
    CHECK(raya_reaper_integrated_reserve(&ctx->state,&ctx->ops[0].integrated));
    CHECK(raya_reaper_integrated_handoff(&ctx->state,&ctx->ops[0].integrated));
    CHECK(SetEvent(ctx->wake));
    LeaveCriticalSection(&ctx->lock);

    worker=CreateThread(NULL,0,worker_main,ctx,0,NULL);
    CHECK(worker!=NULL);
    if(worker==NULL) goto disconnect_cleanup;
    CHECK(WaitForSingleObject(event_handle,50)==WAIT_TIMEOUT);
    CHECK(CloseHandle(client));
    client=INVALID_HANDLE_VALUE;
    wait_result=WaitForSingleObject(worker,10000);
    CHECK(wait_result==WAIT_OBJECT_0);
    CHECK(ctx->worker_error==9);
    EnterCriticalSection(&ctx->lock);
    CHECK(ctx->state.core.lifecycle==RAYA_REAPER_DEGRADED);
    CHECK(ctx->state.core.degradation_causes==RAYA_RECOVERY_CAUSE_COMPLETION_UNCERTAIN);
    CHECK(ctx->state.registry.count==1);
    CHECK(ctx->state.core.outstanding==1);
    CHECK(ctx->state.reserved_slots==1);
    CHECK(ctx->ops[0].integrated.collect_state==RAYA_INTEGRATED_UNCERTAIN);
    CHECK(ctx->ops[0].integrated.reservation_state==RAYA_INTEGRATED_RESERVATION_HANDED_OFF);
    CHECK(!raya_reaper_integrated_can_exit(&ctx->state));
    LeaveCriticalSection(&ctx->lock);

disconnect_cleanup:
    if(worker!=NULL) {
        if(WaitForSingleObject(worker,0)==WAIT_OBJECT_0) CloseHandle(worker);
        else { (void)CancelIoEx(ctx->server,NULL); (void)SetEvent(ctx->wake);
               (void)WaitForSingleObject(worker,5000); CloseHandle(worker); }
    } else if(ctx->server!=INVALID_HANDLE_VALUE) {
        (void)CancelIoEx(ctx->server,NULL);
    }
    if(client!=INVALID_HANDLE_VALUE) CloseHandle(client);
    if(ctx->server!=INVALID_HANDLE_VALUE) CloseHandle(ctx->server);
    if(ctx->wake!=NULL) CloseHandle(ctx->wake);
    /* Deliberately do not close event_handle: UNCERTAIN retains kernel-visible ownership. */
    DeleteCriticalSection(&ctx->lock);
}

int main(void) {
    HANDLE client=INVALID_HANDLE_VALUE, worker=NULL;
    unsigned char payload[SUCCESS_OPS];
    DWORD written=0, wait_result;
    size_t i;
    int setup_ok=0;
    memset(&g,0,sizeof(g));
    InitializeCriticalSection(&g.lock);
    g.server=INVALID_HANDLE_VALUE;
    g.wake=CreateEventW(NULL,TRUE,FALSE,NULL);
    CHECK(g.wake!=NULL);
    CHECK(make_pipe_pair(&g.server,&client));
    CHECK(raya_reaper_integrated_init(&g.state));
    if(g.wake==NULL || g.server==INVALID_HANDLE_VALUE || client==INVALID_HANDLE_VALUE)
        goto cleanup;
    setup_ok=1;
    for(i=0;i<TOTAL_OPS;i++) {
        HANDLE ev=CreateEventW(NULL,TRUE,FALSE,NULL);
        DWORD immediate=0;
        BOOL ok;
        DWORD error;
        CHECK(ev!=NULL);
        if(ev==NULL) goto cleanup;
        g.ops[i].integrated.base.state=RAYA_REAPER_OP_CALLER_PENDING;
        g.ops[i].integrated.completion_event=ev;
        g.ops[i].ov.hEvent=event_for_overlapped(ev);
        if(i<SUCCESS_OPS) {
            ok=ReadFile(g.server,&g.ops[i].byte,1,&immediate,&g.ops[i].ov);
            CHECK(!ok);
            error=GetLastError();
            CHECK(error==ERROR_IO_PENDING);
        } else {
            ok=ReadFile(g.server,&g.ops[i].byte,1,&immediate,&g.ops[i].ov);
            CHECK(!ok);
            error=GetLastError();
            CHECK(error==ERROR_IO_PENDING);
        }
        EnterCriticalSection(&g.lock);
        CHECK(raya_reaper_integrated_admit(&g.state));
        CHECK(raya_reaper_integrated_reserve(&g.state,&g.ops[i].integrated));
        CHECK(raya_reaper_integrated_handoff(&g.state,&g.ops[i].integrated));
        CHECK(SetEvent(g.wake));
        LeaveCriticalSection(&g.lock);
    }
    worker=CreateThread(NULL,0,worker_main,&g,0,NULL);
    CHECK(worker!=NULL);
    if(worker==NULL) goto cleanup;

    for(i=0;i<SUCCESS_OPS;i++) payload[i]=(unsigned char)(i+1);
    CHECK(WriteFile(client,payload,SUCCESS_OPS,&written,NULL));
    CHECK(written==SUCCESS_OPS);

    CHECK(WaitForSingleObject(g.ops[SUCCESS_OPS].integrated.completion_event,50)==WAIT_TIMEOUT);
    CHECK(CancelIoEx(g.server,&g.ops[SUCCESS_OPS].ov));
    EnterCriticalSection(&g.lock);
    CHECK(raya_reaper_integrated_shutdown(&g.state));
    CHECK(SetEvent(g.wake));
    LeaveCriticalSection(&g.lock);

    wait_result=WaitForSingleObject(worker,30000);
    CHECK(wait_result==WAIT_OBJECT_0);
    CHECK(g.worker_error==0);
    /* Native cancellation race: operation 0 is already collected; cancellation must report not found. */
    { BOOL cancel_ok=CancelIoEx(g.server,&g.ops[0].ov); DWORD cancel_error=cancel_ok?ERROR_SUCCESS:GetLastError(); CHECK(!cancel_ok); CHECK(cancel_error==ERROR_NOT_FOUND); }
    CHECK(g.collected_success==SUCCESS_OPS);
    CHECK(g.collected_aborted==1);
    CHECK(g.wait_plan_builds>=2);
    EnterCriticalSection(&g.lock);
    CHECK(g.state.registry.count==0);
    CHECK(g.state.core.outstanding==0);
    CHECK(g.state.core.queued==0);
    CHECK(g.state.core.caller_active==0);
    CHECK(g.state.reserved_slots==0);
    CHECK(g.state.core.degradation_causes==0);
    CHECK(raya_reaper_integrated_can_exit(&g.state));
    CHECK(raya_reaper_mark_stopped(&g.state.core));
    LeaveCriticalSection(&g.lock);

cleanup:
    if(worker!=NULL) {
        if(WaitForSingleObject(worker,0)==WAIT_OBJECT_0) CloseHandle(worker);
        else {
            (void)CancelIoEx(g.server,NULL);
            (void)SetEvent(g.wake);
            (void)WaitForSingleObject(worker,5000);
            CloseHandle(worker);
        }
    } else if(setup_ok) {
        (void)CancelIoEx(g.server,NULL);
    }
    if(client!=INVALID_HANDLE_VALUE) CloseHandle(client);
    if(g.server!=INVALID_HANDLE_VALUE) CloseHandle(g.server);
    for(i=0;i<TOTAL_OPS;i++) {
        HANDLE ev=g.ops[i].integrated.completion_event;
        if(ev!=NULL && ev!=INVALID_HANDLE_VALUE) CloseHandle(ev);
    }
    if(g.wake!=NULL) CloseHandle(g.wake);
    DeleteCriticalSection(&g.lock);
    test_disconnect_retains_uncertain_ownership();
    printf("Live named-pipe integrated Reaper: %ld assertions, %ld failures; success=%ld aborted=%ld waitplans=%ld worker_error=%ld\n",
        g_tests,g_failures,g.collected_success,g.collected_aborted,g.wait_plan_builds,g.worker_error);
    return g_failures ? 1 : 0;
}
