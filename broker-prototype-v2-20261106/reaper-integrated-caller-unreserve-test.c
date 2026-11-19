#include "reaper-integrated-v2.h"
#include <stdio.h>
#include <string.h>
static int tests,failures;
#define CHECK(x) do { ++tests; if(!(x)){++failures;printf("FAIL line %d: %s\n",__LINE__,#x);} } while(0)
static void test_caller_local_unreserve(void) {
 raya_reaper_integrated s;
 raya_integrated_op op;
 HANDLE ev=CreateEventW(NULL,TRUE,FALSE,NULL);
 CHECK(ev!=NULL);
 memset(&op,0,sizeof(op));
 op.base.state=RAYA_REAPER_OP_CALLER_PENDING;
 op.completion_event=ev;
 CHECK(raya_reaper_integrated_init(&s));
 CHECK(raya_reaper_integrated_admit(&s));
 CHECK(raya_reaper_integrated_reserve(&s,&op));
 CHECK(s.core.caller_active==1&&s.reserved_slots==1);
 CHECK(!raya_reaper_integrated_unreserve(&s,&op));
 CHECK(op.reservation_state==RAYA_INTEGRATED_RESERVATION_HELD);
 CHECK(raya_reaper_integrated_shutdown(&s));
 CHECK(raya_reaper_integrated_caller_terminal(&s,&op));
 CHECK(op.base.state==RAYA_REAPER_OP_CALLER_TERMINAL);
 CHECK(raya_reaper_integrated_unreserve(&s,&op));
 CHECK(op.reservation_state==RAYA_INTEGRATED_RESERVATION_RELEASED);
 CHECK(s.reserved_slots==0&&s.core.caller_active==1);
 CHECK(!raya_reaper_integrated_unreserve(&s,&op));
 CHECK(raya_reaper_caller_finish(&s.core));
 CHECK(raya_reaper_integrated_can_exit(&s));
 CHECK(raya_reaper_mark_stopped(&s.core));
 CHECK(CloseHandle(ev));
}
static void test_invalid_unreserve_does_not_mutate(void) {
 raya_reaper_integrated s;
 raya_integrated_op op;
 HANDLE ev=CreateEventW(NULL,TRUE,FALSE,NULL);
 CHECK(ev!=NULL);
 memset(&op,0,sizeof(op));
 op.base.state=RAYA_REAPER_OP_CALLER_PENDING;
 op.completion_event=ev;
 CHECK(raya_reaper_integrated_init(&s));
 CHECK(raya_reaper_integrated_admit(&s));
 CHECK(raya_reaper_integrated_reserve(&s,&op));
 op.base.state=RAYA_REAPER_OP_QUEUED;
 CHECK(!raya_reaper_integrated_unreserve(&s,&op));
 CHECK(s.reserved_slots==1&&op.reservation_state==RAYA_INTEGRATED_RESERVATION_HELD);
 op.base.state=RAYA_REAPER_OP_CALLER_PENDING;
 CHECK(raya_reaper_integrated_caller_terminal(&s,&op));
 CHECK(raya_reaper_integrated_unreserve(&s,&op));
 CHECK(raya_reaper_caller_finish(&s.core));
 CHECK(raya_reaper_integrated_shutdown(&s));
 CHECK(raya_reaper_integrated_can_exit(&s));
 CHECK(raya_reaper_mark_stopped(&s.core));
 CHECK(CloseHandle(ev));
}
int main(void) {
 test_caller_local_unreserve();
 test_invalid_unreserve_does_not_mutate();
 printf("Integrated caller-local unreserve: %d assertions, %d failures\n",tests,failures);
 return failures?1:0;
}
