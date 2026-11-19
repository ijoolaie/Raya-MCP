#include "reaper-integrated-v2.h"
#include <stdio.h>
#include <string.h>
#define N RAYA_REAPER_INTEGRATED_CAPACITY
#define CHECK(x) do { ++tests; if (!(x)) { ++failures; printf("FAIL %d: %s\n",tests,#x); } } while(0)
static int tests,failures;
static raya_integrated_op ops[N],overflow;
static HANDLE events[N],overflow_event;
int main(void) {
 raya_reaper_integrated s;
 raya_integrated_op *out=NULL;
 size_t i;
 memset(ops,0,sizeof(ops)); memset(events,0,sizeof(events));
 memset(&overflow,0,sizeof(overflow));
 CHECK(raya_reaper_integrated_init(&s));
 for(i=0;i<N;i++) {
  events[i]=CreateEventA(NULL,TRUE,FALSE,NULL);
  CHECK(events[i]!=NULL);
  ops[i].base.state=RAYA_REAPER_OP_CALLER_PENDING;
  ops[i].completion_event=events[i];
  CHECK(raya_reaper_integrated_admit(&s));
  CHECK(raya_reaper_integrated_reserve(&s,&ops[i]));
  CHECK(!raya_reaper_integrated_reserve(&s,&ops[i]));
  CHECK(raya_reaper_integrated_handoff(&s,&ops[i]));
  CHECK(ops[i].reservation_state==RAYA_INTEGRATED_RESERVATION_HANDED_OFF);
 }
 CHECK(s.core.queued==N && s.core.outstanding==N);
 CHECK(s.reserved_slots==N && s.registry.count==0);
 CHECK(raya_reaper_integrated_admit(&s));
 overflow_event=CreateEventA(NULL,TRUE,FALSE,NULL);
 overflow.base.state=RAYA_REAPER_OP_CALLER_PENDING;
 overflow.completion_event=overflow_event;
 CHECK(!raya_reaper_integrated_reserve(&s,&overflow));
 CHECK(overflow.reservation_state==RAYA_INTEGRATED_RESERVATION_NONE);
 CHECK(raya_reaper_caller_finish(&s.core));
 for(i=0;i<N;i++) {
  out=NULL;
  CHECK(raya_reaper_integrated_promote_one(&s,&out)==RAYA_INTEGRATED_OK);
  CHECK(out==&ops[i]);
 }
 CHECK(s.registry.count==N && s.core.queued==0);
 for(i=0;i<N;i++) {
  CHECK(raya_reaper_integrated_claim(&s,&ops[i],events[i]));
  CHECK(raya_reaper_integrated_collect(&s,&ops[i],
        RAYA_INTEGRATED_TERMINAL_SUCCESS)==RAYA_INTEGRATED_OK);
  CHECK(ops[i].reservation_state==RAYA_INTEGRATED_RESERVATION_RELEASED);
 }
 CHECK(s.registry.count==0 && s.core.outstanding==0 && s.reserved_slots==0);
 CHECK(raya_reaper_integrated_shutdown(&s));
 CHECK(raya_reaper_integrated_can_exit(&s));
 CHECK(raya_reaper_mark_stopped(&s.core));
 for(i=0;i<N;i++) CHECK(CloseHandle(events[i]));
 CHECK(CloseHandle(overflow_event));
 printf("Integrated capacity: %d assertions, %d failures\n",tests,failures);
 return failures ? 1 : 0;
}
