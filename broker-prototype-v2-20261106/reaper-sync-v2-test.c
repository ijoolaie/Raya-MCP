#include "reaper-sync-v2.h"
#include <stdio.h>
#include <string.h>
static int failures;
#define CHECK(x,n) do { if(x) printf("PASS: %s\n",n); else { printf("FAIL: %s\n",n); ++failures; } } while(0)
static void initop(raya_reaper_op *p){p->next=NULL;p->state=RAYA_REAPER_OP_CALLER_PENDING;}
int main(void){
 raya_reaper_sync s; raya_reaper_op a,b,*p=NULL; raya_reaper_take_result tr;
 memset(&s,0,sizeof(s)); initop(&a);initop(&b);
 CHECK(raya_reaper_sync_init(&s),"initialize lock, manual-reset event and core");
 CHECK(raya_reaper_sync_admit(&s)&&raya_reaper_sync_admit(&s),"admit two callers");
 CHECK(raya_reaper_sync_handoff(&s,&a)&&raya_reaper_sync_handoff(&s,&b),
       "handoff signals wake event while holding shared lock");
 tr=raya_reaper_sync_take(&s,1000,&p);
 CHECK(tr==RAYA_REAPER_TAKE_OPERATION&&p==&a,"wake/reset/dequeue obtains FIFO head");
 tr=raya_reaper_sync_take(&s,1000,&p);
 CHECK(tr==RAYA_REAPER_TAKE_OPERATION&&p==&b,"remaining queue re-signals after reset");
 CHECK(raya_reaper_sync_terminal(&s,&a)&&raya_reaper_sync_terminal(&s,&b),
       "worker terminal accounting accepted");
 CHECK(raya_reaper_sync_shutdown(&s),"shutdown transitions and wakes waiter");
 tr=raya_reaper_sync_take(&s,1000,&p);
 CHECK(tr==RAYA_REAPER_TAKE_EXIT,"empty drained shutdown produces exit indication");
 CHECK(raya_reaper_sync_mark_stopped(&s),"mark stopped after drained exit");
 CHECK(raya_reaper_sync_destroy(&s),"destroy only after stopped and drained");
 {
  raya_reaper_sync x; raya_reaper_op q;
  memset(&x,0,sizeof(x));initop(&q);
  CHECK(raya_reaper_sync_init(&x)&&raya_reaper_sync_admit(&x),
        "second instance admits caller");
  CHECK(raya_reaper_sync_shutdown(&x),"shutdown can race a pre-admitted caller");
  CHECK(raya_reaper_sync_handoff(&x,&q),"pre-admitted caller hands off during stopping");
  tr=raya_reaper_sync_take(&x,1000,&p);
  CHECK(tr==RAYA_REAPER_TAKE_OPERATION&&p==&q,"queued operation remains wake-visible");
  CHECK(raya_reaper_sync_terminal(&x,&q),"terminal collection drains raced handoff");
  tr=raya_reaper_sync_take(&x,1000,&p);
  CHECK(tr==RAYA_REAPER_TAKE_EXIT,"shutdown exit visible after final terminal");
  CHECK(raya_reaper_sync_mark_stopped(&x)&&raya_reaper_sync_destroy(&x),
        "second instance cleanly stops and destroys");
 }
 printf("SUMMARY: %d failure(s)\n",failures);return failures?1:0;
}
