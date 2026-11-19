#include "reaper-integrated-v2.h"
#include "reaper-waitplan-v2.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { ++tests; if (!(x)) { ++failures; printf("FAIL %d: %s\n",tests,#x); } } while(0)
static int tests,failures;

static int setup_op(raya_reaper_integrated *s, raya_integrated_op *op,
                    HANDLE event_handle) {
 memset(op,0,sizeof(*op));
 op->base.state=RAYA_REAPER_OP_CALLER_PENDING;
 op->completion_event=event_handle;
 return raya_reaper_integrated_init(s) &&
        raya_reaper_integrated_admit(s) &&
        raya_reaper_integrated_reserve(s,op) &&
        raya_reaper_integrated_handoff(s,op);
}

static void test_signaled_but_incomplete(void) {
 raya_reaper_integrated s;
 raya_integrated_op op,*promoted=NULL;
 raya_reaper_waitplan plan;
 raya_reaper_op *resolved_op=NULL;
 HANDLE ev=CreateEventW(NULL,TRUE,FALSE,NULL);
 HANDLE wake=CreateEventW(NULL,TRUE,FALSE,NULL), resolved_event=NULL;
 DWORD wr;
 CHECK(ev!=NULL && wake!=NULL);
 CHECK(setup_op(&s,&op,ev));
 CHECK(raya_reaper_integrated_promote_one(&s,&promoted)==RAYA_INTEGRATED_OK);
 CHECK(promoted==&op);
 CHECK(SetEvent(ev));
 CHECK(raya_reaper_waitplan_build(&s.registry,wake,&plan)==RAYA_WAITPLAN_READY);
 wr=WaitForMultipleObjects(plan.waitset.handle_count,plan.waitset.handles,FALSE,0);
 CHECK(wr==WAIT_OBJECT_0+1);
 CHECK(raya_reaper_waitplan_resolve(&s.registry,&plan,wr,&resolved_op,
                                    &resolved_event)==RAYA_WAITPLAN_OPERATION);
 CHECK(resolved_op==&op.base && resolved_event==ev);
 CHECK(raya_reaper_integrated_claim(&s,&op,resolved_event));
 /* Fault injection: event is signaled, but the completion collector says pending. */
 CHECK(raya_reaper_integrated_collect(&s,&op,RAYA_INTEGRATED_INCOMPLETE)
       ==RAYA_INTEGRATED_OK);
 CHECK(s.registry.count==1 && s.core.outstanding==1 && s.reserved_slots==1);
 CHECK(op.base.state==RAYA_REAPER_OP_WORKER_PENDING);
 CHECK(op.collect_state==RAYA_INTEGRATED_COLLECT_PENDING);
 resolved_op=NULL; resolved_event=NULL;
 CHECK(raya_reaper_waitplan_build(&s.registry,wake,&plan)==RAYA_WAITPLAN_READY);
 wr=WaitForMultipleObjects(plan.waitset.handle_count,plan.waitset.handles,FALSE,0);
 CHECK(wr==WAIT_OBJECT_0+1);
 CHECK(raya_reaper_waitplan_resolve(&s.registry,&plan,wr,&resolved_op,
                                    &resolved_event)==RAYA_WAITPLAN_OPERATION);
 CHECK(raya_reaper_integrated_claim(&s,&op,resolved_event));
 CHECK(raya_reaper_integrated_collect(&s,&op,RAYA_INTEGRATED_TERMINAL_SUCCESS)
       ==RAYA_INTEGRATED_OK);
 CHECK(s.registry.count==0 && s.core.outstanding==0 && s.reserved_slots==0);
 CHECK(raya_reaper_integrated_shutdown(&s));
 CHECK(raya_reaper_integrated_can_exit(&s));
 CHECK(raya_reaper_mark_stopped(&s.core));
 CHECK(CloseHandle(ev)); CHECK(CloseHandle(wake));
}

static void test_uncertain_collection_cause(void) {
 raya_reaper_integrated s;
 raya_integrated_op op,*promoted=NULL;
 HANDLE ev=CreateEventW(NULL,TRUE,FALSE,NULL);
 CHECK(ev!=NULL);
 CHECK(setup_op(&s,&op,ev));
 CHECK(raya_reaper_integrated_promote_one(&s,&promoted)==RAYA_INTEGRATED_OK);
 CHECK(raya_reaper_integrated_claim(&s,&op,ev));
 CHECK(raya_reaper_integrated_collect(&s,&op,RAYA_INTEGRATED_UNCERTAIN_RESULT)
       ==RAYA_INTEGRATED_DEGRADED);
 CHECK(s.core.lifecycle==RAYA_REAPER_DEGRADED);
 CHECK(s.core.degradation_causes==RAYA_RECOVERY_CAUSE_COMPLETION_UNCERTAIN);
 CHECK(s.registry.count==1 && s.core.outstanding==1 && s.reserved_slots==1);
 CHECK(op.base.state==RAYA_REAPER_OP_WORKER_PENDING);
 /* The operation remains tracked; deliberately retain its event. */
}

static void test_wait_failed_retains(void) {
 raya_reaper_integrated s;
 raya_integrated_op op,*promoted=NULL;
 raya_reaper_waitplan plan;
 raya_reaper_op *resolved_op=NULL;
 HANDLE ev=CreateEventW(NULL,TRUE,FALSE,NULL);
 HANDLE wake=CreateEventW(NULL,TRUE,FALSE,NULL), resolved_event=NULL;
 CHECK(ev!=NULL && wake!=NULL);
 CHECK(setup_op(&s,&op,ev));
 CHECK(raya_reaper_integrated_promote_one(&s,&promoted)==RAYA_INTEGRATED_OK);
 CHECK(raya_reaper_waitplan_build(&s.registry,wake,&plan)==RAYA_WAITPLAN_READY);
 CHECK(raya_reaper_waitplan_resolve(&s.registry,&plan,WAIT_FAILED,&resolved_op,
                                    &resolved_event)==RAYA_WAITPLAN_FAILED);
 CHECK(resolved_op==NULL && resolved_event==NULL);
 CHECK(raya_reaper_mark_degraded_cause(&s.core,RAYA_RECOVERY_CAUSE_WAIT_FAILED));
 CHECK(s.core.degradation_causes==RAYA_RECOVERY_CAUSE_WAIT_FAILED);
 CHECK(s.registry.count==1 && s.core.outstanding==1 && s.reserved_slots==1);
 CHECK(op.base.state==RAYA_REAPER_OP_WORKER_PENDING);
 CHECK(!raya_reaper_integrated_can_exit(&s));
 /* Ownership is deliberately retained; do not close the op event while tracked. */
 CHECK(CloseHandle(wake));
 /* The test process owns the intentionally retained event until process exit. */
}

static void test_wake_signal_failure_retains_queue(void) {
 raya_reaper_integrated s;
 raya_integrated_op op,*out=NULL;
 HANDLE ev=CreateEventW(NULL,TRUE,FALSE,NULL);
 HANDLE wake=CreateEventW(NULL,TRUE,FALSE,NULL);
 CHECK(ev!=NULL && wake!=NULL);
 CHECK(setup_op(&s,&op,ev));
 /* Fault injection: emulate SetEvent(wake) failure after ownership transfer. */
 CHECK(raya_reaper_mark_degraded_cause(&s.core,RAYA_RECOVERY_CAUSE_WAKE_SIGNAL));
 CHECK(s.core.degradation_causes==RAYA_RECOVERY_CAUSE_WAKE_SIGNAL);
 CHECK(s.core.queued==1 && s.core.outstanding==1);
 CHECK(s.reserved_slots==1 && op.reservation_state==RAYA_INTEGRATED_RESERVATION_HANDED_OFF);
 CHECK(raya_reaper_integrated_promote_one(&s,&out)==RAYA_INTEGRATED_DEGRADED);
 CHECK(out==NULL);
 CHECK(!raya_reaper_integrated_can_exit(&s));
 /* Queue and event are intentionally retained because ownership was not discharged. */
 CHECK(CloseHandle(wake));
}

int main(void) {
 test_signaled_but_incomplete();
 test_uncertain_collection_cause();
 test_wait_failed_retains();
 test_wake_signal_failure_retains_queue();
 printf("Integrated Reaper fault-injection: %d assertions, %d failures\n",tests,failures);
 return failures ? 1 : 0;
}
