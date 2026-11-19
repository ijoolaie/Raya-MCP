#include "reaper-sync-v2.h"
#include <stdio.h>
#include <string.h>

static LONG tests=0, failures=0;
#define CHECK(x) do { InterlockedIncrement(&tests); if(!(x)) { \
    InterlockedIncrement(&failures); printf("FAIL: %s\\n",#x); } } while(0)

static void init_op(raya_reaper_op *op) {
    memset(op,0,sizeof(*op));
    op->state=RAYA_REAPER_OP_CALLER_PENDING;
}

static void drain_and_dispose_degraded(raya_reaper_sync *s,
                                       raya_reaper_op *expected) {
    raya_reaper_op *found=NULL;
    raya_reaper_take_result take;
    HANDLE wake=s->wake_event;
    CHECK(SetEvent(wake));
    take=raya_reaper_sync_take(s,1000,&found);
    CHECK(take==RAYA_REAPER_TAKE_OPERATION && found==expected);
    CHECK(raya_reaper_sync_terminal(s,expected));
    CHECK(s->core.queued==0 && s->core.outstanding==0);
    CHECK(s->core.head==NULL && s->core.tail==NULL);
    /* Test-only teardown: DEGRADED deliberately cannot claim clean shutdown. */
    CHECK(CloseHandle(wake));
    DeleteCriticalSection(&s->lock);
    s->wake_event=NULL;
    s->initialized=FALSE;
}

static void test_explicit_outcome(void) {
    raya_reaper_sync s;
    raya_reaper_op op;
    HANDLE real_wake;
    raya_reaper_handoff_result result;
    memset(&s,0,sizeof(s));
    init_op(&op);
    CHECK(raya_reaper_sync_init(&s));
    CHECK(raya_reaper_sync_admit(&s));
    real_wake=s.wake_event;
    s.wake_event=INVALID_HANDLE_VALUE; /* force the real SetEvent call to fail */
    result=raya_reaper_sync_handoff_ex(&s,&op);
    s.wake_event=real_wake;
    CHECK(result==RAYA_REAPER_HANDOFF_TRANSFERRED_DEGRADED);
    CHECK(op.state==RAYA_REAPER_OP_QUEUED);
    CHECK(s.core.queued==1 && s.core.outstanding==1);
    CHECK(s.core.caller_active==0 && s.core.head==&op);
    CHECK(s.core.lifecycle==RAYA_REAPER_DEGRADED);
    drain_and_dispose_degraded(&s,&op);
}

static void test_compatibility_wrapper(void) {
    raya_reaper_sync s;
    raya_reaper_op op;
    HANDLE real_wake;
    int result;
    memset(&s,0,sizeof(s));
    init_op(&op);
    CHECK(raya_reaper_sync_init(&s));
    CHECK(raya_reaper_sync_admit(&s));
    real_wake=s.wake_event;
    s.wake_event=INVALID_HANDLE_VALUE;
    result=raya_reaper_sync_handoff(&s,&op);
    s.wake_event=real_wake;
    CHECK(result==1); /* TRUE now means transferred, not necessarily signaled. */
    CHECK(op.state==RAYA_REAPER_OP_QUEUED);
    CHECK(s.core.queued==1 && s.core.outstanding==1);
    CHECK(s.core.lifecycle==RAYA_REAPER_DEGRADED);
    drain_and_dispose_degraded(&s,&op);
}

static void test_not_transferred(void) {
    raya_reaper_sync s;
    raya_reaper_op op;
    raya_reaper_handoff_result result;
    memset(&s,0,sizeof(s));
    init_op(&op);
    CHECK(raya_reaper_sync_init(&s));
    result=raya_reaper_sync_handoff_ex(&s,&op);
    CHECK(result==RAYA_REAPER_HANDOFF_NOT_TRANSFERRED);
    CHECK(op.state==RAYA_REAPER_OP_CALLER_PENDING);
    CHECK(s.core.queued==0 && s.core.outstanding==0);
    CHECK(raya_reaper_sync_shutdown(&s));
    CHECK(raya_reaper_sync_mark_stopped(&s));
    CHECK(raya_reaper_sync_destroy(&s));
}

int main(void) {
    test_explicit_outcome();
    test_compatibility_wrapper();
    test_not_transferred();
    printf("Sync handoff ownership outcome: %ld assertions, %ld failures\\n",
           tests,failures);
    return failures ? 1 : 0;
}
