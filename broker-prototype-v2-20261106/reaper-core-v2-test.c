#include "reaper-core-v2.h"
#include <stdio.h>
#include <string.h>
static int failures;
#define CHECK(x,n) do { if(x) printf("PASS: %s\n",n); else { printf("FAIL: %s\n",n); ++failures; } } while(0)
static void init_op(raya_reaper_op *op) { op->next=NULL; op->state=RAYA_REAPER_OP_CALLER_PENDING; }
int main(void) {
    raya_reaper_core c={0};
    raya_reaper_op a,b,d,*p;
    CHECK(!raya_reaper_admit(&c),"uninitialized core rejects admission");
    CHECK(raya_reaper_core_start(&c),"core enters accepting exactly once");
    CHECK(!raya_reaper_core_start(&c),"duplicate start rejected");
    CHECK(raya_reaper_admit(&c) && raya_reaper_admit(&c),"two caller leases admitted");
    init_op(&a); init_op(&b); init_op(&d);
    CHECK(raya_reaper_handoff(&c,&a),"first pending operation handed off");
    CHECK(c.caller_active==1 && c.outstanding==1 && c.queued==1,
          "handoff atomically transfers caller and outstanding accounting");
    CHECK(raya_reaper_handoff(&c,&b),"second pending operation handed off");
    CHECK(c.caller_active==0 && c.outstanding==2 && c.queued==2,
          "multiple intrusive entries accounted");
    CHECK(!raya_reaper_handoff(&c,&a),"duplicate enqueue rejected");
    CHECK(raya_reaper_admit(&c) && raya_reaper_caller_finish(&c),
          "lease can be acquired and released before shutdown");
    CHECK(raya_reaper_begin_shutdown(&c),"shutdown stops new admission");
    CHECK(!raya_reaper_admit(&c),"admission rejected after stopping");
    CHECK(!raya_reaper_can_exit(&c),"worker cannot exit with queued work");
    p=raya_reaper_dequeue(&c);
    CHECK(p==&a && p->state==RAYA_REAPER_OP_WORKER_PENDING,
          "FIFO dequeue transfers queue ownership to worker");
    CHECK(!raya_reaper_can_exit(&c),"worker cannot exit with outstanding operation");
    CHECK(raya_reaper_worker_terminal(&c,p) && p->state==RAYA_REAPER_OP_RELEASED,
          "terminal collection releases one worker-owned operation");
    p=raya_reaper_dequeue(&c);
    CHECK(p==&b && raya_reaper_worker_terminal(&c,p),
          "second operation drains exactly once");
    CHECK(c.outstanding==0 && c.queued==0 && c.head==NULL && c.tail==NULL,
          "queue and outstanding counters return to zero");
    CHECK(raya_reaper_can_exit(&c),"exit predicate true only after complete drain");
    CHECK(raya_reaper_mark_stopped(&c) && c.lifecycle==RAYA_REAPER_STOPPED,
          "stopped transition accepted after drain");
    CHECK(!raya_reaper_mark_degraded(&c),"stopped lifecycle is immutable");
    {
        raya_reaper_core x={0}; raya_reaper_op q;
        CHECK(raya_reaper_core_start(&x) && raya_reaper_admit(&x),
              "shutdown-race caller lease admitted");
        init_op(&q);
        CHECK(raya_reaper_begin_shutdown(&x),"shutdown can begin with active caller");
        CHECK(!raya_reaper_can_exit(&x),"active caller lease prevents worker exit");
        CHECK(raya_reaper_handoff(&x,&q),"pre-admitted caller may handoff during stopping");
        CHECK(x.caller_active==0 && x.outstanding==1,
              "handoff transfers lease while stopping without exit gap");
        p=raya_reaper_dequeue(&x);
        CHECK(p==&q && raya_reaper_worker_terminal(&x,p) &&
              raya_reaper_can_exit(&x),"shutdown completes after raced handoff drains");
    }
    {
        raya_reaper_core x={0};
        CHECK(raya_reaper_core_start(&x) && raya_reaper_admit(&x),
              "caller lease acquired for ordinary completion");
        CHECK(raya_reaper_caller_finish(&x) && x.caller_active==0,
              "ordinary caller completion releases lease");
        CHECK(!raya_reaper_caller_finish(&x),"double lease release rejected");
    }
    {
        raya_reaper_core x={0}; raya_reaper_op q;
        CHECK(raya_reaper_core_start(&x) && raya_reaper_admit(&x),
              "invalid transition test setup");
        init_op(&q);
        CHECK(!raya_reaper_worker_terminal(&x,&q),
              "worker cannot release caller-owned operation");
        CHECK(raya_reaper_mark_degraded(&x) &&
              x.lifecycle==RAYA_REAPER_DEGRADED,
              "sticky degraded transition recorded");
    }
    printf("SUMMARY: %d failure(s)\n",failures);
    return failures?1:0;
}
