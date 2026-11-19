#include "reaper-waitplan-v2.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x,n) do { if (x) printf("PASS: %s\n",n); else { printf("FAIL: %s\n",n); ++failures; } } while (0)

typedef struct {
    CRITICAL_SECTION lock;
    raya_reaper_registry registry;
    raya_reaper_op *op;
    HANDLE event_handle;
    HANDLE wake;
    HANDLE ready;
    HANDLE release;
    int add_ok;
} producer_context;

static DWORD WINAPI producer_main(LPVOID parameter) {
    producer_context *c=(producer_context *)parameter;
    SetEvent(c->ready);
    WaitForSingleObject(c->release,2000);
    EnterCriticalSection(&c->lock);
    c->add_ok=raya_reaper_registry_add(&c->registry,c->op,c->event_handle);
    if (c->add_ok) SetEvent(c->wake);
    LeaveCriticalSection(&c->lock);
    return 0;
}

static int init_event(HANDLE *h) {
    *h=CreateEventW(NULL,TRUE,FALSE,NULL);
    return *h!=NULL;
}

int main(void) {
    producer_context c;
    raya_reaper_op original, appended, *found=NULL;
    HANDLE original_event=NULL, appended_event=NULL, thread=NULL;
    raya_reaper_waitplan plan;
    DWORD wr;
    raya_waitplan_result resolved;
    memset(&c,0,sizeof(c)); memset(&original,0,sizeof(original));
    memset(&appended,0,sizeof(appended));
    original.state=RAYA_REAPER_OP_WORKER_PENDING;
    appended.state=RAYA_REAPER_OP_WORKER_PENDING;
    InitializeCriticalSection(&c.lock);
    init_event(&c.wake); init_event(&c.ready); init_event(&c.release);
    init_event(&original_event); init_event(&appended_event);
    c.op=&appended; c.event_handle=appended_event;
    CHECK(c.wake!=NULL && c.ready!=NULL && c.release!=NULL &&
          original_event!=NULL && appended_event!=NULL,
          "create synchronization and operation events");
    EnterCriticalSection(&c.lock);
    CHECK(raya_reaper_registry_add(&c.registry,&original,original_event),
          "seed registry before wait-plan snapshot");
    CHECK(raya_reaper_waitplan_build(&c.registry,c.wake,&plan)==RAYA_WAITPLAN_READY,
          "build immutable wait snapshot while holding shared lock");
    LeaveCriticalSection(&c.lock);

    thread=CreateThread(NULL,0,producer_main,&c,0,NULL);
    CHECK(thread!=NULL,"start concurrent registry producer");
    if (thread!=NULL) {
        WaitForSingleObject(c.ready,1000);
        SetEvent(c.release);
        CHECK(WaitForSingleObject(thread,2000)==WAIT_OBJECT_0 && c.add_ok,
              "producer appends a new operation while worker waits");
        /* Model the worker consuming the producer wake before reusing the snapshot. */
        EnterCriticalSection(&c.lock);
        ResetEvent(c.wake);
        LeaveCriticalSection(&c.lock);
        SetEvent(original_event);
        wr=WaitForMultipleObjects(plan.waitset.handle_count,
                                  plan.waitset.handles,FALSE,1000);
        CHECK(wr==WAIT_OBJECT_0+1,
              "existing snapshot remains waitable after append-only mutation");
        EnterCriticalSection(&c.lock);
        resolved=raya_reaper_waitplan_resolve(&c.registry,&plan,wr,
                                               &found,&appended_event);
        LeaveCriticalSection(&c.lock);
        CHECK(resolved==RAYA_WAITPLAN_OPERATION && found==&original,
              "resolve retains exact original operation after concurrent append");

        EnterCriticalSection(&c.lock);
        CHECK(raya_reaper_waitplan_build(&c.registry,c.wake,&plan)==RAYA_WAITPLAN_READY,
              "rebuild snapshot observes producer append");
        LeaveCriticalSection(&c.lock);
        SetEvent(c.wake);
        wr=WaitForMultipleObjects(plan.waitset.handle_count,
                                  plan.waitset.handles,FALSE,1000);
        CHECK(wr==WAIT_OBJECT_0,"producer wake notification is visible in rebuilt plan");
        EnterCriticalSection(&c.lock);
        resolved=raya_reaper_waitplan_resolve(&c.registry,&plan,wr,
                                               &found,&appended_event);
        LeaveCriticalSection(&c.lock);
        CHECK(resolved==RAYA_WAITPLAN_WAKE,
              "wake is resolved separately from operation completion");
        CloseHandle(thread);
    }

    if (original_event) CloseHandle(original_event);
    if (appended_event) CloseHandle(appended_event);
    if (c.wake) CloseHandle(c.wake);
    if (c.ready) CloseHandle(c.ready);
    if (c.release) CloseHandle(c.release);
    DeleteCriticalSection(&c.lock);
    printf("SUMMARY: %d failure(s)\n",failures);
    return failures ? 1 : 0;
}
