#include "reaper-integrated-v2.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { ++tests; if (!(x)) { ++failures; printf("FAIL %d: %s\n",tests,#x); } } while(0)
static int tests,failures;
int main(void) {
 raya_reaper_integrated s;
 raya_integrated_op a,b,*promoted=NULL;
 HANDLE ea=CreateEventA(NULL,TRUE,FALSE,NULL), eb=CreateEventA(NULL,TRUE,FALSE,NULL);
 memset(&a,0,sizeof(a)); memset(&b,0,sizeof(b));
 a.base.state=RAYA_REAPER_OP_CALLER_PENDING; b.base.state=RAYA_REAPER_OP_CALLER_PENDING;
 a.completion_event=ea; b.completion_event=eb;
 CHECK(ea!=NULL && eb!=NULL);
 CHECK(raya_reaper_integrated_init(&s));
 CHECK(raya_reaper_integrated_admit(&s));
 CHECK(raya_reaper_integrated_reserve(&s,&a));
 CHECK(!raya_reaper_integrated_reserve(&s,&a));
 CHECK(!raya_reaper_integrated_handoff(&s,&b));
 CHECK(raya_reaper_integrated_handoff(&s,&a));
 CHECK(a.reservation_state==RAYA_INTEGRATED_RESERVATION_HANDED_OFF);
 CHECK(s.core.caller_active==0 && s.core.queued==1 && s.core.outstanding==1);
 CHECK(raya_reaper_integrated_promote_one(&s,&promoted)==RAYA_INTEGRATED_OK);
 CHECK(promoted==&a && s.registry.count==1 && s.reserved_slots==1);
 CHECK(!raya_reaper_integrated_claim(&s,&a,eb));
 CHECK(raya_reaper_integrated_claim(&s,&a,ea));
 CHECK(raya_reaper_integrated_collect(&s,&a,RAYA_INTEGRATED_INCOMPLETE)==RAYA_INTEGRATED_OK);
 CHECK(s.registry.count==1 && s.core.outstanding==1 && s.reserved_slots==1);
 CHECK(raya_reaper_integrated_claim(&s,&a,ea));
 CHECK(raya_reaper_integrated_collect(&s,&a,RAYA_INTEGRATED_TERMINAL_SUCCESS)==RAYA_INTEGRATED_OK);
 CHECK(s.registry.count==0 && s.core.outstanding==0 && s.reserved_slots==0);
 CHECK(a.reservation_state==RAYA_INTEGRATED_RESERVATION_RELEASED);
 CHECK(raya_reaper_integrated_shutdown(&s));
 CHECK(raya_reaper_integrated_can_exit(&s));
 CHECK(raya_reaper_mark_stopped(&s.core));
 CHECK(s.core.lifecycle==RAYA_REAPER_STOPPED);
 CHECK(CloseHandle(ea)); CHECK(CloseHandle(eb));
 printf("Integrated Reaper: %d assertions, %d failures\n",tests,failures);
 return failures ? 1 : 0;
}
