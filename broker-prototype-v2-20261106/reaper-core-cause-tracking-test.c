#include "reaper-core-v2.h"
#include <stdio.h>
static unsigned tests,failures;
#define CHECK(x) do { ++tests; if (!(x)) { ++failures; printf("FAIL %s\n",#x); } } while (0)
int main(void) {
 raya_reaper_core c={0};
 CHECK(raya_reaper_core_start(&c));
 CHECK(c.degradation_causes==0);
 CHECK(raya_reaper_mark_degraded_cause(&c,RAYA_RECOVERY_CAUSE_WAKE_SIGNAL));
 CHECK(c.degradation_causes==RAYA_RECOVERY_CAUSE_WAKE_SIGNAL);
 CHECK(raya_recovery_policy_evaluate(c.degradation_causes)==RAYA_RECOVERY_POLLING_ELIGIBLE);
 CHECK(raya_reaper_mark_degraded_cause(&c,RAYA_RECOVERY_CAUSE_WAIT_FAILED));
 CHECK(c.degradation_causes==(RAYA_RECOVERY_CAUSE_WAKE_SIGNAL|RAYA_RECOVERY_CAUSE_WAIT_FAILED));
 CHECK(raya_recovery_policy_evaluate(c.degradation_causes)==RAYA_RECOVERY_POLLING_ELIGIBLE);
 CHECK(raya_reaper_mark_degraded_cause(&c,RAYA_RECOVERY_CAUSE_COMPLETION_UNCERTAIN));
 CHECK(raya_recovery_policy_evaluate(c.degradation_causes)==RAYA_RECOVERY_BLOCKED);
 CHECK(raya_reaper_mark_degraded_cause(&c,RAYA_RECOVERY_CAUSE_WAKE_SIGNAL));
 CHECK(raya_recovery_policy_evaluate(c.degradation_causes)==RAYA_RECOVERY_BLOCKED);
 CHECK(c.degradation_causes==(RAYA_RECOVERY_CAUSE_WAKE_SIGNAL|RAYA_RECOVERY_CAUSE_WAIT_FAILED|RAYA_RECOVERY_CAUSE_COMPLETION_UNCERTAIN));
 CHECK(!raya_reaper_mark_degraded_cause(&c,UINT32_C(1)<<31));
 {
  raya_reaper_core h={0};
  CHECK(raya_reaper_core_start(&h));
  CHECK(raya_reaper_mark_degraded_cause(&h,RAYA_RECOVERY_CAUSE_HANDLE_CLOSE));
  CHECK(h.lifecycle==RAYA_REAPER_DEGRADED);
  CHECK(h.degradation_causes==RAYA_RECOVERY_CAUSE_HANDLE_CLOSE);
  CHECK(raya_recovery_policy_evaluate(h.degradation_causes)==RAYA_RECOVERY_BLOCKED);
  CHECK(!raya_reaper_mark_stopped(&h));
 }
 CHECK(raya_reaper_mark_degraded(&c));
 CHECK((c.degradation_causes&RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT)!=0);
 CHECK(raya_recovery_policy_evaluate(c.degradation_causes)==RAYA_RECOVERY_BLOCKED);
 {
  raya_reaper_core x={0}; raya_reaper_op q={0};
  CHECK(raya_reaper_core_start(&x));
  q.state=RAYA_REAPER_OP_CALLER_PENDING;
  x.head=&q; x.tail=&q; x.queued=1;
  CHECK(raya_reaper_dequeue(&x)==NULL);
  CHECK(x.lifecycle==RAYA_REAPER_DEGRADED);
  CHECK(x.degradation_causes==RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
 }
 printf("Core cause tracking: %u assertions, %u failures\n",tests,failures);
 return failures?1:0;
}
