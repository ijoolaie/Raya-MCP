#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "reaper-integrated-v2.h"

#define N 70
#define PRODUCERS 4
#define CHECK(x) do { InterlockedIncrement(&tests); if (!(x)) { InterlockedIncrement(&failures); printf("FAIL: %s (err=%lu)\n",#x,(unsigned long)GetLastError()); } } while(0)

typedef struct {
    raya_integrated_op op;
    OVERLAPPED ov;
    HANDLE server,client,event_handle;
    unsigned char byte,sent;
} test_op;

static CRITICAL_SECTION lock;
static HANDLE gate,prepared,producers[PRODUCERS];
static raya_reaper_integrated state;
static test_op ops[N];
static volatile LONG tests=0,failures=0,ready=0,done=0,handoffs=0,io_errors=0;

static HANDLE io_event(HANDLE h) {
    return (HANDLE)((ULONG_PTR)h | (ULONG_PTR)1);
}
static int make_pipe(test_op *o,unsigned i) {
    wchar_t name[160];
    BOOL connected;
    DWORD e;
    swprintf_s(name,160,L"\\\\.\\pipe\\RayaWakeFailRetain_%lu_%u",
               (unsigned long)GetCurrentProcessId(),i);
    o->server=CreateNamedPipeW(name,PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,4096,4096,0,NULL);
    if(o->server==INVALID_HANDLE_VALUE) return 0;
    o->client=CreateFileW(name,GENERIC_READ|GENERIC_WRITE,0,NULL,
                          OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(o->client==INVALID_HANDLE_VALUE) return 0;
    connected=ConnectNamedPipe(o->server,NULL);
    if(!connected && (e=GetLastError())!=ERROR_PIPE_CONNECTED) return 0;
    o->event_handle=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(o->event_handle==NULL) return 0;
    o->ov.hEvent=io_event(o->event_handle);
    o->op.base.state=RAYA_REAPER_OP_CALLER_PENDING;
    o->op.completion_event=o->event_handle;
    o->sent=(unsigned char)(i+1);
    return 1;
}
static DWORD WINAPI producer(LPVOID arg) {
    size_t p=(size_t)(uintptr_t)arg,i;
    for(i=p;i<N;i+=PRODUCERS) {
        EnterCriticalSection(&lock);
        CHECK(raya_reaper_integrated_admit(&state));
        CHECK(raya_reaper_integrated_reserve(&state,&ops[i].op));
        LeaveCriticalSection(&lock);
    }
    if(InterlockedIncrement(&ready)==PRODUCERS) (void)SetEvent(prepared);
    (void)WaitForSingleObject(gate,INFINITE);
    for(i=p;i<N;i+=PRODUCERS) {
        test_op *o=&ops[i];
        DWORD bytes=0,written=0,e=0;
        BOOL ok=ReadFile(o->server,&o->byte,1,&bytes,&o->ov);
        if(ok || (e=GetLastError())!=ERROR_IO_PENDING) {
            InterlockedIncrement(&io_errors);
            continue;
        }
        EnterCriticalSection(&lock);
        if(!raya_reaper_integrated_handoff(&state,&o->op)) {
            InterlockedIncrement(&io_errors);
            (void)raya_reaper_mark_degraded_cause(&state.core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
            LeaveCriticalSection(&lock);
            continue;
        }
        InterlockedIncrement(&handoffs);
        LeaveCriticalSection(&lock);
        if(!WriteFile(o->client,&o->sent,1,&written,NULL) || written!=1)
            InterlockedIncrement(&io_errors);
    }
    InterlockedIncrement(&done);
    return 0;
}

int main(void) {
    unsigned i;
    DWORD wr;
    HANDLE invalid_wake=INVALID_HANDLE_VALUE;
    InitializeCriticalSection(&lock);
    gate=CreateEventW(NULL,TRUE,FALSE,NULL);
    prepared=CreateEventW(NULL,TRUE,FALSE,NULL);
    CHECK(gate!=NULL && prepared!=NULL);
    CHECK(raya_reaper_integrated_init(&state));
    for(i=0;i<N;i++) {
        ops[i].server=INVALID_HANDLE_VALUE;
        ops[i].client=INVALID_HANDLE_VALUE;
        CHECK(make_pipe(&ops[i],i));
        if(ops[i].server==INVALID_HANDLE_VALUE ||
           ops[i].client==INVALID_HANDLE_VALUE || ops[i].event_handle==NULL)
            return 2;
    }
    for(i=0;i<PRODUCERS;i++) {
        producers[i]=CreateThread(NULL,0,producer,(LPVOID)(uintptr_t)i,0,NULL);
        CHECK(producers[i]!=NULL);
        if(producers[i]==NULL) return 3;
    }
    CHECK(WaitForSingleObject(prepared,30000)==WAIT_OBJECT_0);
    CHECK(ready==PRODUCERS);
    EnterCriticalSection(&lock);
    CHECK(state.core.caller_active==N);
    CHECK(state.reserved_slots==N);
    CHECK(raya_reaper_integrated_shutdown(&state));
    CHECK(!raya_reaper_integrated_admit(&state));
    CHECK(!raya_reaper_integrated_can_exit(&state));
    LeaveCriticalSection(&lock);
    CHECK(SetEvent(gate));
    wr=WaitForMultipleObjects(PRODUCERS,producers,TRUE,30000);
    CHECK(wr==WAIT_OBJECT_0);
    CHECK(done==PRODUCERS);
    CHECK(handoffs==N);
    CHECK(io_errors==0);

    /* Inject the native wake failure only after all callers have transferred
       ownership. No worker is started: this test verifies safe retention. */
    CHECK(SetEvent(invalid_wake)==FALSE);
    EnterCriticalSection(&lock);
    CHECK(raya_reaper_mark_degraded_cause(&state.core,RAYA_RECOVERY_CAUSE_WAKE_SIGNAL));
    CHECK(state.core.lifecycle==RAYA_REAPER_DEGRADED);
    CHECK(state.core.degradation_causes==RAYA_RECOVERY_CAUSE_WAKE_SIGNAL);
    CHECK(state.core.queued==N);
    CHECK(state.core.outstanding==N);
    CHECK(state.core.caller_active==0);
    CHECK(state.reserved_slots==N);
    CHECK(state.registry.count==0);
    CHECK(!raya_reaper_integrated_can_exit(&state));
    {
        raya_integrated_op *promoted=NULL;
        CHECK(raya_reaper_integrated_promote_one(&state,&promoted)==
              RAYA_INTEGRATED_DEGRADED);
        CHECK(promoted==NULL);
    }
    CHECK(state.core.queued==N && state.core.outstanding==N);
    for(i=0;i<N;i++) {
        CHECK(ops[i].op.base.state==RAYA_REAPER_OP_QUEUED);
        CHECK(ops[i].op.reservation_state==RAYA_INTEGRATED_RESERVATION_HANDED_OFF);
    }
    LeaveCriticalSection(&lock);

    printf("Native wake failure retention: %ld assertions, %ld failures; queued=%zu outstanding=%zu reserved=%zu; recovery=BLOCKED\n",
           tests,failures,state.core.queued,state.core.outstanding,state.reserved_slots);
    /* Fail-closed by design: do not close OVERLAPPED/event/pipe resources.
       All handles and operation storage remain valid until process teardown. */
    return failures?1:0;
}