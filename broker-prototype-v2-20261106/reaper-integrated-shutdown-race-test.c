#include "reaper-integrated-v2.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { ++tests; if (!(x)) { ++failures; printf("FAIL %d: %s\n",tests,#x); } } while(0)
static int tests,failures;
int main(void) {
 raya_reaper_integrated s;
 raya_integrated_op op,*out=NULL;
 HANDLE ev=CreateEventA(NULL,TRUE,FALSE,NULL);
 memset(&op,0,sizeof(op)); op.base.state=RAYA_REAPER_OP_CALLER_PENDING;
 op.completion_event=ev;
 CHECK(ev!=NULL);
 CHECK(raya_reaper_integrated_init(&s));
 CHECK(raya_reaper_integrated_admit(&s));
 CHECK(raya_reaper_integrated_reserve(&s,&op));
 CHECK(raya_reaper_integrated_shutdown(&s));
 CHECK(!raya_reaper_integrated_admit(&s));
 CHECK(!raya_reaper_integrated_reserve(&s,&op));
 CHECK(raya_reaper_integrated_handoff(&s,&op));
 CHECK(s.core.caller_active==0 && s.core.queued==1 && s.reserved_slots==1);
 CHECK(raya_reaper_integrated_promote_one(&s,&out)==RAYA_INTEGRATED_OK);
 CHECK(out==&op);
 CHECK(raya_reaper_integrated_claim(&s,&op,ev));
 CHECK(raya_reaper_integrated_collect(&s,&op,RAYA_INTEGRATED_TERMINAL_ABORTED)==RAYA_INTEGRATED_OK);
 CHECK(raya_reaper_integrated_can_exit(&s));
 CHECK(raya_reaper_mark_stopped(&s.core));
 CHECK(CloseHandle(ev));
 printf("Integrated shutdown race: %d assertions, %d failures\n",tests,failures);
 return failures ? 1 : 0;
}
