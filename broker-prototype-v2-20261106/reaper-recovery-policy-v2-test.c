#include "reaper-recovery-policy-v2.h"
#include <stdio.h>
static unsigned tests,failures;
#define CHECK(expr) do { ++tests; if (!(expr)) { ++failures; printf("FAIL: %s\n",#expr); } } while (0)
int main(void) {
 uint32_t wake=RAYA_RECOVERY_CAUSE_WAKE_SIGNAL, wait=RAYA_RECOVERY_CAUSE_WAIT_FAILED;
 uint32_t uncertain=RAYA_RECOVERY_CAUSE_COMPLETION_UNCERTAIN, owner=RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT;
 uint32_t close=RAYA_RECOVERY_CAUSE_HANDLE_CLOSE, waitset=RAYA_RECOVERY_CAUSE_WAITSET_INVALID;
 CHECK(raya_recovery_policy_evaluate(0)==RAYA_RECOVERY_BLOCKED);
 CHECK(raya_recovery_policy_evaluate(wake)==RAYA_RECOVERY_POLLING_ELIGIBLE);
 CHECK(raya_recovery_policy_evaluate(wait)==RAYA_RECOVERY_POLLING_ELIGIBLE);
 CHECK(raya_recovery_policy_evaluate(wake|wait)==RAYA_RECOVERY_POLLING_ELIGIBLE);
 CHECK(raya_recovery_policy_evaluate(uncertain)==RAYA_RECOVERY_BLOCKED);
 CHECK(raya_recovery_policy_evaluate(owner)==RAYA_RECOVERY_BLOCKED);
 CHECK(raya_recovery_policy_evaluate(close)==RAYA_RECOVERY_BLOCKED);
 CHECK(raya_recovery_policy_evaluate(waitset)==RAYA_RECOVERY_BLOCKED);
 CHECK(raya_recovery_policy_evaluate(wake|uncertain)==RAYA_RECOVERY_BLOCKED);
 CHECK(raya_recovery_policy_evaluate(wait|owner)==RAYA_RECOVERY_BLOCKED);
 CHECK(raya_recovery_policy_evaluate(wake|close)==RAYA_RECOVERY_BLOCKED);
 CHECK(raya_recovery_policy_evaluate(wake|waitset)==RAYA_RECOVERY_BLOCKED);
 CHECK(raya_recovery_policy_evaluate(UINT32_C(1)<<31)==RAYA_RECOVERY_BLOCKED);
 CHECK(raya_recovery_policy_evaluate(UINT32_MAX)==RAYA_RECOVERY_BLOCKED);
 CHECK(raya_recovery_policy_evaluate(wake|wait|uncertain|owner|close|waitset)==RAYA_RECOVERY_BLOCKED);
 printf("Recovery policy: %u assertions, %u failures\n",tests,failures);
 return failures?1:0;
}
