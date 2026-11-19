#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "reaper-integrated-v2.h"
#include "reaper-waitplan-v2.h"

#define CHECK(x) do { ++tests; if (!(x)) { ++failures; printf("FAIL %d: %s (err=%lu)\n",tests,#x,(unsigned long)GetLastError()); } } while(0)
static int tests, failures;

int main(void) {
    raya_reaper_integrated state;
    raya_integrated_op op, *promoted=NULL;
    raya_reaper_waitplan plan;
    raya_reaper_op *resolved_op=NULL;
    HANDLE operation_event=NULL, wake_event=NULL, resolved_event=NULL;
    DWORD wait_result=WAIT_FAILED, wait_error=ERROR_SUCCESS;
    DWORD probe=WAIT_FAILED;
    int initialized=0;

    memset(&op,0,sizeof(op));
    operation_event=CreateEventW(NULL,TRUE,FALSE,NULL);
    wake_event=CreateEventW(NULL,TRUE,FALSE,NULL);
    CHECK(operation_event!=NULL && wake_event!=NULL);
    if(operation_event==NULL || wake_event==NULL) goto done;

    op.base.state=RAYA_REAPER_OP_CALLER_PENDING;
    op.completion_event=operation_event;
    CHECK(raya_reaper_integrated_init(&state));
    initialized=1;
    CHECK(raya_reaper_integrated_admit(&state));
    CHECK(raya_reaper_integrated_reserve(&state,&op));
    CHECK(raya_reaper_integrated_handoff(&state,&op));
    CHECK(raya_reaper_integrated_promote_one(&state,&promoted)==RAYA_INTEGRATED_OK);
    CHECK(promoted==&op);
    CHECK(state.registry.count==1 && state.core.outstanding==1);
    CHECK(state.reserved_slots==1);

    CHECK(raya_reaper_waitplan_build(&state.registry,wake_event,&plan)==RAYA_WAITPLAN_READY);
    CHECK(plan.waitset.handle_count==2);
    CHECK(plan.waitset.handles[0]==wake_event);
    CHECK(plan.waitset.handles[1]==operation_event);

    /* Close only the disposable wake handle, never the operation's event. */
    CHECK(CloseHandle(wake_event));
    wake_event=NULL;
    wait_result=WaitForMultipleObjects(plan.waitset.handle_count,
                                       plan.waitset.handles,FALSE,100);
    CHECK(wait_result==WAIT_FAILED);
    /* Capture immediately: no Win32 call may intervene. */
    wait_error=GetLastError();
    CHECK(wait_error==ERROR_INVALID_HANDLE);

    CHECK(raya_reaper_mark_degraded_cause(&state.core,RAYA_RECOVERY_CAUSE_WAIT_FAILED));
    probe=WaitForSingleObject(operation_event,0);
    CHECK(probe==WAIT_TIMEOUT);
    CHECK(state.core.lifecycle==RAYA_REAPER_DEGRADED);
    CHECK(state.core.degradation_causes==RAYA_RECOVERY_CAUSE_WAIT_FAILED);
    CHECK(state.registry.count==1);
    CHECK(state.core.outstanding==1);
    CHECK(state.reserved_slots==1);
    CHECK(op.base.state==RAYA_REAPER_OP_WORKER_PENDING);
    CHECK(op.reservation_state==RAYA_INTEGRATED_RESERVATION_HANDED_OFF);
    CHECK(op.collect_state==RAYA_INTEGRATED_COLLECT_PENDING);
    CHECK(!raya_reaper_integrated_can_exit(&state));
    CHECK(raya_reaper_waitplan_resolve(&state.registry,&plan,wait_result,
          &resolved_op,&resolved_event)==RAYA_WAITPLAN_FAILED);
    CHECK(resolved_op==NULL && resolved_event==NULL);
    /* Do not close operation_event: it remains registry-owned after uncertain wait failure. */

done:
    if(wake_event!=NULL) CloseHandle(wake_event);
    /* operation_event intentionally retained when ownership was transferred. */
    printf("Native WAIT_FAILED retention: %d assertions, %d failures, wait_error=%lu, probe=%lu\n",
           tests,failures,(unsigned long)wait_error,(unsigned long)probe);
    (void)initialized;
    return failures ? 1 : 0;
}
