#include "reaper-integrated-v2.h"
#include "pipe-io-finalizer-v2.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static int tests,failures;
#define CHECK(x) do { ++tests; if(!(x)){++failures;printf("FAIL line %d: %s\n",__LINE__,#x);} } while(0)
static HANDLE hv(uintptr_t v){return (HANDLE)v;}
typedef struct {int calls;int fail_on;} close_ctx;
static BOOL close_fake(HANDLE h,void *v){close_ctx *c=(close_ctx *)v;(void)h;++c->calls;return c->calls!=c->fail_on;}
static void prepare(raya_reaper_integrated *s,raya_integrated_op *op,HANDLE ev){
 memset(op,0,sizeof(*op)); op->base.state=RAYA_REAPER_OP_CALLER_PENDING;op->completion_event=ev;
 CHECK(raya_reaper_integrated_init(s));
 CHECK(raya_reaper_integrated_admit(s));
 CHECK(raya_reaper_integrated_reserve(s,op));
 CHECK(raya_reaper_integrated_handoff(s,op));
 {raya_integrated_op *promoted=NULL;CHECK(raya_reaper_integrated_promote_one(s,&promoted)==RAYA_INTEGRATED_OK);CHECK(promoted==op);}
 CHECK(raya_reaper_integrated_claim(s,op,ev));
}
static void test_release_after_finalizer(void){
 raya_reaper_integrated s;raya_integrated_op op;raya_pipe_finalizer f;close_ctx c={0};
 HANDLE ev=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(ev!=NULL);
 prepare(&s,&op,ev);
 CHECK(raya_reaper_integrated_terminal_hold(&s,&op,RAYA_INTEGRATED_TERMINAL_SUCCESS)==RAYA_INTEGRATED_OK);
 CHECK(op.base.state==RAYA_REAPER_OP_TERMINAL_HELD);
 CHECK(op.reservation_state==RAYA_INTEGRATED_RESERVATION_TERMINAL_HELD);
 CHECK(s.core.outstanding==1&&s.reserved_slots==1&&s.registry.count==0);
 CHECK(!raya_reaper_integrated_can_exit(&s));
 CHECK(raya_pipe_finalizer_init(&f,hv(0x111),hv(0x222)));
 CHECK(raya_pipe_finalizer_close_handles(&f,close_fake,&c)==RAYA_FINALIZER_OK);
 CHECK(raya_pipe_finalizer_commit_release(&f));
 CHECK(raya_reaper_integrated_release(&s,&op));
 CHECK(s.core.outstanding==0&&s.reserved_slots==0);
 CHECK(op.base.state==RAYA_REAPER_OP_RELEASED);
 CHECK(raya_reaper_integrated_shutdown(&s));
 CHECK(raya_reaper_integrated_can_exit(&s));
 CHECK(raya_reaper_mark_stopped(&s.core));
 CHECK(CloseHandle(ev));
}
static void test_partial_close_blocks_release(void){
 raya_reaper_integrated s;raya_integrated_op op;raya_pipe_finalizer f;close_ctx c={0};c.fail_on=2;
 HANDLE ev=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(ev!=NULL);
 prepare(&s,&op,ev);
 CHECK(raya_reaper_integrated_terminal_hold(&s,&op,RAYA_INTEGRATED_TERMINAL_ABORTED)==RAYA_INTEGRATED_OK);
 CHECK(raya_pipe_finalizer_init(&f,hv(0x333),hv(0x444)));
 CHECK(raya_pipe_finalizer_close_handles(&f,close_fake,&c)==RAYA_FINALIZER_CLOSE_FAILED);
 CHECK(f.state==RAYA_FINALIZER_CLOSE_UNCERTAIN&&f.successful_closes==1);
 CHECK(raya_reaper_mark_degraded_cause(&s.core,RAYA_RECOVERY_CAUSE_HANDLE_CLOSE));
 CHECK(!raya_pipe_finalizer_commit_release(&f));
 CHECK(!raya_reaper_integrated_release(&s,&op));
 CHECK(s.core.outstanding==1&&s.reserved_slots==1);
 CHECK(op.base.state==RAYA_REAPER_OP_TERMINAL_HELD);
 CHECK(!raya_reaper_integrated_can_exit(&s));
 CHECK(s.core.degradation_causes==RAYA_RECOVERY_CAUSE_HANDLE_CLOSE);
 CHECK(CloseHandle(ev));
}
int main(void){test_release_after_finalizer();test_partial_close_blocks_release();printf("Reaper two-phase release: %d assertions, %d failures\n",tests,failures);return failures?1:0;}
