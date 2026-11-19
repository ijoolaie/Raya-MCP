#include "reaper-worker-v2.h"
#include <stdio.h>
#include <string.h>
static int failures, collected;
#define CHECK(x,n) do { if(x) printf("PASS: %s\n",n); else { printf("FAIL: %s\n",n); ++failures; } } while(0)
static int fake_collect(raya_reaper_op *op, void *ctx) {
    int *count=(int *)ctx;
    if(op==NULL) return 0;
    InterlockedIncrement((volatile LONG *)count);
    return 1; /* synthetic test completion only; no kernel I/O is present */
}
static int fail_collect(raya_reaper_op *op, void *ctx) {
    (void)op;(void)ctx;return 0;
}
static int slow_collect(raya_reaper_op *op, void *ctx) {
    (void)ctx;
    if(op==NULL) return 0;
    Sleep(250);
    return 1;
}
static void initop(raya_reaper_op *p){p->next=NULL;p->state=RAYA_REAPER_OP_CALLER_PENDING;}
int main(void) {
 raya_reaper_worker w; raya_reaper_op a,b; int count=0;
 memset(&w,0,sizeof(w));initop(&a);initop(&b);
 CHECK(raya_reaper_worker_start(&w,fake_collect,&count),"create worker thread and sync primitives");
 CHECK(raya_reaper_worker_admit(&w)&&raya_reaper_worker_admit(&w),"admit two caller leases");
 CHECK(raya_reaper_worker_handoff(&w,&a)&&raya_reaper_worker_handoff(&w,&b),
       "submit two synthetic pending operations");
 CHECK(raya_reaper_worker_shutdown(&w,3000),"bounded shutdown joins after worker drains queue");
 CHECK(count==2,"worker callback observed both operations");
 CHECK(a.state==RAYA_REAPER_OP_RELEASED&&b.state==RAYA_REAPER_OP_RELEASED,
       "accounting released only after callback reports terminal");
 CHECK(raya_reaper_worker_destroy(&w),"destroy joined and drained worker");
 {
  raya_reaper_worker x; raya_reaper_op q;
  memset(&x,0,sizeof(x));initop(&q);
  CHECK(raya_reaper_worker_start(&x,slow_collect,NULL),"start delayed synthetic worker");
  CHECK(raya_reaper_worker_admit(&x)&&raya_reaper_worker_handoff(&x,&q),
        "queue operation before bounded join timeout");
  CHECK(!raya_reaper_worker_shutdown(&x,10),
        "short join deadline reports incomplete without freeing worker state");
  CHECK(raya_reaper_worker_shutdown(&x,3000),
        "shutdown can be retried and joins after delayed collection");
  CHECK(raya_reaper_worker_destroy(&x),"delayed worker destroys after successful retry");
 }
 {
  raya_reaper_worker x; raya_reaper_op q;
  memset(&x,0,sizeof(x));initop(&q);
  CHECK(raya_reaper_worker_start(&x,fail_collect,NULL),"start failure-path worker");
  CHECK(raya_reaper_worker_admit(&x)&&raya_reaper_worker_handoff(&x,&q),
        "queue operation for synthetic collection failure");
  Sleep(100);
  CHECK(!raya_reaper_worker_shutdown(&x,1000),
        "collection failure prevents clean shutdown claim");
  CHECK(x.sync.core.lifecycle==RAYA_REAPER_DEGRADED&&
        x.sync.core.degradation_causes==RAYA_RECOVERY_CAUSE_COMPLETION_UNCERTAIN&&
        x.sync.core.outstanding==1&&q.state==RAYA_REAPER_OP_WORKER_PENDING,
        "failed collection retains ownership and outstanding accounting");
  /* Deliberately retain resources: worker is joined but degraded and not destroyable. */
  WaitForSingleObject(x.thread,1000);
  CloseHandle(x.thread); x.thread=NULL;
 }
 printf("SUMMARY: %d failure(s)\n",failures);return failures?1:0;
}
