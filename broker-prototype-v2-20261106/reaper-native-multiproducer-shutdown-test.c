#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "reaper-integrated-v2.h"
#include "reaper-waitplan-v2.h"
#include "pipe-completion-context-v2.h"

#define N 70
#define PRODUCERS 4
#define EACH ((N + PRODUCERS - 1) / PRODUCERS)
#define CHECK(x) do { InterlockedIncrement(&g_tests); if (!(x)) { InterlockedIncrement(&g_failures); printf("FAIL: %s (err=%lu)\n",#x,(unsigned long)GetLastError()); } } while (0)

typedef struct {
    raya_integrated_op op;
    OVERLAPPED ov;
    HANDLE server, client, event_handle;
    unsigned char byte, sent;
} operation;

typedef struct {
    CRITICAL_SECTION lock;
    HANDLE gate, wake, prepared_event, worker, producers[PRODUCERS];
    raya_reaper_integrated state;
    operation ops[N];
    volatile LONG prepared, producer_done, handed_off, completed, worker_error;
    volatile LONG waitplans, rotations;
} context;

static context g;
static volatile LONG g_tests=0, g_failures=0;

static HANDLE event_for_io(HANDLE h) {
    return (HANDLE)((ULONG_PTR)h | (ULONG_PTR)1);
}

static int create_pair(operation *o, unsigned i) {
    wchar_t name[160];
    BOOL connected;
    DWORD error;
    swprintf_s(name,160,L"\\\\.\\pipe\\RayaMultiProducer_%lu_%u",
               (unsigned long)GetCurrentProcessId(),i);
    o->server=CreateNamedPipeW(name,
        PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,4096,4096,0,NULL);
    if (o->server==INVALID_HANDLE_VALUE) return 0;
    o->client=CreateFileW(name,GENERIC_READ|GENERIC_WRITE,0,NULL,
                          OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if (o->client==INVALID_HANDLE_VALUE) return 0;
    connected=ConnectNamedPipe(o->server,NULL);
    if (!connected) {
        error=GetLastError();
        if (error!=ERROR_PIPE_CONNECTED) return 0;
    }
    o->event_handle=CreateEventW(NULL,TRUE,FALSE,NULL);
    if (o->event_handle==NULL) return 0;
    o->ov.hEvent=event_for_io(o->event_handle);
    o->op.base.state=RAYA_REAPER_OP_CALLER_PENDING;
    o->op.completion_event=o->event_handle;
    o->sent=(unsigned char)(i+1);
    return 1;
}

static DWORD WINAPI producer_main(LPVOID arg) {
    size_t producer=(size_t)(uintptr_t)arg;
    size_t i;
    for (i=producer;i<N;i+=PRODUCERS) {
        int admitted, reserved;
        EnterCriticalSection(&g.lock);
        admitted=raya_reaper_integrated_admit(&g.state);
        reserved=admitted && raya_reaper_integrated_reserve(&g.state,&g.ops[i].op);
        CHECK(admitted);
        CHECK(reserved);
        if (!reserved) {
            if (admitted) (void)raya_reaper_caller_finish(&g.state.core);
            InterlockedExchange(&g.worker_error,9);
        }
        LeaveCriticalSection(&g.lock);
    }
    if (InterlockedIncrement(&g.prepared)==PRODUCERS &&
        !SetEvent(g.prepared_event))
        InterlockedExchange(&g.worker_error,8);
    (void)WaitForSingleObject(g.gate,INFINITE);
    for (i=producer;i<N;i+=PRODUCERS) {
        operation *o=&g.ops[i];
        DWORD immediate=0, written=0, error;
        BOOL submitted;
        submitted=ReadFile(o->server,&o->byte,1,&immediate,&o->ov);
        if (submitted || (error=GetLastError())!=ERROR_IO_PENDING) {
            InterlockedExchange(&g.worker_error,10);
            continue;
        }
        EnterCriticalSection(&g.lock);
        if (!raya_reaper_integrated_handoff(&g.state,&o->op)) {
            (void)raya_reaper_mark_degraded_cause(&g.state.core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
            InterlockedExchange(&g.worker_error,11);
            LeaveCriticalSection(&g.lock);
            continue;
        }
        InterlockedIncrement(&g.handed_off);
        if (!SetEvent(g.wake)) {
            (void)raya_reaper_mark_degraded_cause(&g.state.core,RAYA_RECOVERY_CAUSE_WAKE_SIGNAL);
            InterlockedExchange(&g.worker_error,12);
        }
        LeaveCriticalSection(&g.lock);
        if (!WriteFile(o->client,&o->sent,1,&written,NULL) || written!=1) {
            InterlockedExchange(&g.worker_error,13);
            continue;
        }
    }
    InterlockedIncrement(&g.producer_done);
    return 0;
}

static DWORD WINAPI worker_main(LPVOID unused) {
    size_t previous=(size_t)-1;
    (void)unused;
    for (;;) {
        raya_reaper_waitplan plan;
        raya_waitplan_result built;
        DWORD wr;
        EnterCriticalSection(&g.lock);
        if (!ResetEvent(g.wake)) {
            (void)raya_reaper_mark_degraded_cause(&g.state.core,RAYA_RECOVERY_CAUSE_WAITSET_INVALID);
            InterlockedExchange(&g.worker_error,20);
            LeaveCriticalSection(&g.lock);
            return 20;
        }
        for (;;) {
            raya_integrated_op *promoted=NULL;
            raya_integrated_result result=
                raya_reaper_integrated_promote_one(&g.state,&promoted);
            if (result==RAYA_INTEGRATED_EMPTY) break;
            if (result!=RAYA_INTEGRATED_OK || promoted==NULL) {
                (void)raya_reaper_mark_degraded_cause(&g.state.core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
                InterlockedExchange(&g.worker_error,21);
                LeaveCriticalSection(&g.lock);
                return 21;
            }
        }
        if (raya_reaper_integrated_can_exit(&g.state)) {
            LeaveCriticalSection(&g.lock);
            return 0;
        }
        built=raya_reaper_waitplan_build(&g.state.registry,g.wake,&plan);
        if (built!=RAYA_WAITPLAN_READY) {
            (void)raya_reaper_mark_degraded_cause(&g.state.core,RAYA_RECOVERY_CAUSE_WAITSET_INVALID);
            InterlockedExchange(&g.worker_error,22);
            LeaveCriticalSection(&g.lock);
            return 22;
        }
        InterlockedIncrement(&g.waitplans);
        if (plan.waitset.next_cursor==previous)
            InterlockedIncrement(&g.rotations);
        previous=plan.waitset.next_cursor;
        LeaveCriticalSection(&g.lock);

        wr=WaitForMultipleObjects(plan.waitset.handle_count,
                                  plan.waitset.handles,FALSE,100);
        if (wr==WAIT_FAILED) {
            DWORD e=GetLastError();
            EnterCriticalSection(&g.lock);
            (void)raya_reaper_mark_degraded_cause(&g.state.core,RAYA_RECOVERY_CAUSE_WAIT_FAILED);
            InterlockedExchange(&g.worker_error,(LONG)(1000+e));
            LeaveCriticalSection(&g.lock);
            return 23;
        }
        if (wr==WAIT_TIMEOUT || wr==WAIT_OBJECT_0) continue;
        {
            raya_reaper_op *base=NULL;
            HANDLE exact_event=NULL;
            raya_waitplan_result resolved;
            operation *o;
            BOOL ok;
            DWORD bytes=0,error=ERROR_SUCCESS;
            raya_pipe_completion_class classification;

            EnterCriticalSection(&g.lock);
            resolved=raya_reaper_waitplan_resolve(&g.state.registry,&plan,wr,
                                                   &base,&exact_event);
            if (resolved!=RAYA_WAITPLAN_OPERATION || base==NULL ||
                !raya_reaper_integrated_claim(&g.state,
                    (raya_integrated_op *)base,exact_event)) {
                (void)raya_reaper_mark_degraded_cause(&g.state.core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
                InterlockedExchange(&g.worker_error,24);
                LeaveCriticalSection(&g.lock);
                return 24;
            }
            o=(operation *)base;
            LeaveCriticalSection(&g.lock);

            ok=GetOverlappedResult(o->server,&o->ov,&bytes,FALSE);
            if (!ok) error=GetLastError();
            classification=raya_classify_pipe_completion(TRUE,TRUE,ok,error);

            EnterCriticalSection(&g.lock);
            if (classification==RAYA_PIPE_COMPLETION_INCOMPLETE) {
                if (raya_reaper_integrated_collect(&g.state,&o->op,
                       RAYA_INTEGRATED_INCOMPLETE)!=RAYA_INTEGRATED_OK) {
                    InterlockedExchange(&g.worker_error,25);
                    LeaveCriticalSection(&g.lock);
                    return 25;
                }
            } else if (classification==RAYA_PIPE_COMPLETION_TERMINAL_SUCCESS &&
                       bytes==1 && o->byte==o->sent &&
                       raya_reaper_integrated_collect(&g.state,&o->op,
                           RAYA_INTEGRATED_TERMINAL_SUCCESS)==RAYA_INTEGRATED_OK) {
                InterlockedIncrement(&g.completed);
            } else {
                (void)raya_reaper_integrated_collect(&g.state,&o->op,
                    RAYA_INTEGRATED_UNCERTAIN_RESULT);
                InterlockedExchange(&g.worker_error,26);
                LeaveCriticalSection(&g.lock);
                return 26;
            }
            LeaveCriticalSection(&g.lock);
        }
    }
}

int main(void) {
    unsigned i;
    DWORD wr;
    memset(&g,0,sizeof(g));
    InitializeCriticalSection(&g.lock);
    g.gate=CreateEventW(NULL,TRUE,FALSE,NULL);
    g.wake=CreateEventW(NULL,TRUE,FALSE,NULL);
    g.prepared_event=CreateEventW(NULL,TRUE,FALSE,NULL);
    CHECK(g.gate!=NULL && g.wake!=NULL && g.prepared_event!=NULL);
    CHECK(raya_reaper_integrated_init(&g.state));
    for (i=0;i<N;i++) {
        g.ops[i].server=INVALID_HANDLE_VALUE;
        g.ops[i].client=INVALID_HANDLE_VALUE;
        CHECK(create_pair(&g.ops[i],i));
        if (g.ops[i].server==INVALID_HANDLE_VALUE ||
            g.ops[i].client==INVALID_HANDLE_VALUE ||
            g.ops[i].event_handle==NULL) goto cleanup;
    }

    /* Each producer concurrently acquires its own admission leases and
       reservations; shutdown begins only after all producers are parked. */
    for (i=0;i<PRODUCERS;i++) {
        g.producers[i]=CreateThread(NULL,0,producer_main,
                                    (LPVOID)(uintptr_t)i,0,NULL);
        CHECK(g.producers[i]!=NULL);
    }
    for (i=0;i<PRODUCERS;i++) if (g.producers[i]==NULL) goto cleanup;
    CHECK(WaitForSingleObject(g.prepared_event,30000)==WAIT_OBJECT_0);
    CHECK(g.prepared==PRODUCERS);
    EnterCriticalSection(&g.lock);
    CHECK(g.state.core.caller_active==N);
    CHECK(g.state.reserved_slots==N);
    LeaveCriticalSection(&g.lock);

    g.worker=CreateThread(NULL,0,worker_main,NULL,0,NULL);
    CHECK(g.worker!=NULL);
    if (g.worker==NULL) goto cleanup;

    EnterCriticalSection(&g.lock);
    CHECK(raya_reaper_integrated_shutdown(&g.state));
    CHECK(!raya_reaper_integrated_admit(&g.state));
    CHECK(!raya_reaper_integrated_can_exit(&g.state));
    CHECK(SetEvent(g.wake));
    LeaveCriticalSection(&g.lock);
    CHECK(SetEvent(g.gate));

    wr=WaitForMultipleObjects(PRODUCERS,g.producers,TRUE,30000);
    CHECK(wr==WAIT_OBJECT_0);
    CHECK(g.producer_done==PRODUCERS);
    CHECK(g.handed_off==N);
    wr=WaitForSingleObject(g.worker,30000);
    CHECK(wr==WAIT_OBJECT_0);
    CHECK(g.worker_error==0);
    CHECK(g.completed==N);
    EnterCriticalSection(&g.lock);
    CHECK(g.state.core.lifecycle==RAYA_REAPER_STOPPING);
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
    {
        int threads_stopped=1;
        if (g.gate!=NULL) (void)SetEvent(g.gate);
        if (g.worker!=NULL &&
            WaitForSingleObject(g.worker,5000)!=WAIT_OBJECT_0)
            threads_stopped=0;
        for (i=0;i<PRODUCERS;i++) if (g.producers[i]!=NULL &&
            WaitForSingleObject(g.producers[i],5000)!=WAIT_OBJECT_0)
            threads_stopped=0;
        if (!threads_stopped || g.completed!=N) {
            /* Fail closed: never close/free kernel-visible I/O state on an
               uncertain path. Process teardown reclaims this test process. */
            printf("FAIL-CLOSED: retaining pipe/event resources until process exit\\n");
            return 1;
        }
        if (g.worker!=NULL) (void)CloseHandle(g.worker);
        for (i=0;i<PRODUCERS;i++)
            if (g.producers[i]!=NULL) (void)CloseHandle(g.producers[i]);
        for (i=0;i<N;i++) {
            if (g.ops[i].server!=INVALID_HANDLE_VALUE) (void)CloseHandle(g.ops[i].server);
            if (g.ops[i].client!=INVALID_HANDLE_VALUE) (void)CloseHandle(g.ops[i].client);
            if (g.ops[i].event_handle!=NULL) (void)CloseHandle(g.ops[i].event_handle);
        }
        if (g.gate!=NULL) (void)CloseHandle(g.gate);
        if (g.wake!=NULL) (void)CloseHandle(g.wake);
        if (g.prepared_event!=NULL) (void)CloseHandle(g.prepared_event);
        DeleteCriticalSection(&g.lock);
    }
    printf("Native multiproducer + shutdown race: %ld assertions, %ld failures, handoff=%ld/%d, completed=%ld/%d, waitplans=%ld, rotations=%ld\n",
           g_tests,g_failures,g.handed_off,N,g.completed,N,g.waitplans,g.rotations);
    return g_failures ? 1 : 0;
}