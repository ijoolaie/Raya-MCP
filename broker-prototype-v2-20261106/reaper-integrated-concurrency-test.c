#include "reaper-integrated-v2.h"
#include "reaper-waitplan-v2.h"
#include <stdio.h>
#include <string.h>

#define PRODUCERS 4
#define EACH 64
#define TOTAL (PRODUCERS*EACH)
#define CHECK(x) do { InterlockedIncrement(&g_tests); if (!(x)) { InterlockedIncrement(&g_failures); printf("FAIL: %s\n",#x); } } while(0)

typedef struct {
 CRITICAL_SECTION lock;
 HANDLE wake;
 raya_reaper_integrated state;
 raya_integrated_op ops[TOTAL];
 HANDLE events[TOTAL];
 HANDLE producers[PRODUCERS];
 HANDLE worker;
 volatile LONG producer_done;
 volatile LONG handed_off;
 volatile LONG collected;
 volatile LONG failures;
} test_context;

static test_context g;
static volatile LONG g_tests=0, g_failures=0;

static DWORD WINAPI producer_main(LPVOID p) {
 size_t producer=(size_t)(uintptr_t)p;
 size_t j;
 for (j=0;j<EACH;j++) {
  size_t index=producer*EACH+j;
  raya_integrated_op *op=&g.ops[index];
  int ok=0;
  int admitted=0;
  int reserved=0;
  EnterCriticalSection(&g.lock);
  admitted=raya_reaper_integrated_admit(&g.state);
  if (admitted) reserved=raya_reaper_integrated_reserve(&g.state,op);
  if (admitted && reserved &&
      raya_reaper_integrated_handoff(&g.state,op)) {
   ok=1;
   InterlockedIncrement(&g.handed_off);
   if (!SetEvent(g.wake)) {
    (void)raya_reaper_mark_degraded_cause(&g.state.core,RAYA_RECOVERY_CAUSE_WAKE_SIGNAL);
    InterlockedIncrement(&g.failures);
   }
  } else {
   if (admitted) (void)raya_reaper_caller_finish(&g.state.core);
   if (reserved) (void)raya_reaper_mark_degraded_cause(&g.state.core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
   InterlockedIncrement(&g.failures);
  }
  LeaveCriticalSection(&g.lock);
  if (ok && !SetEvent(g.events[index]))
   InterlockedIncrement(&g.failures);
 }
 InterlockedIncrement(&g.producer_done);
 return 0;
}

static DWORD WINAPI worker_main(LPVOID p) {
 test_context *ctx=(test_context *)p;
 for (;;) {
  raya_reaper_waitplan plan;
  raya_waitplan_result built;
  DWORD wr;
  EnterCriticalSection(&ctx->lock);
  if (!ResetEvent(ctx->wake)) {
   (void)raya_reaper_mark_degraded_cause(&ctx->state.core,RAYA_RECOVERY_CAUSE_WAITSET_INVALID);
   LeaveCriticalSection(&ctx->lock);
   return 10;
  }
  for (;;) {
   raya_integrated_op *promoted=NULL;
   raya_integrated_result pr=raya_reaper_integrated_promote_one(
                                  &ctx->state,&promoted);
   if (pr==RAYA_INTEGRATED_EMPTY) break;
   if (pr!=RAYA_INTEGRATED_OK || promoted==NULL) {
    (void)raya_reaper_mark_degraded_cause(&ctx->state.core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
    LeaveCriticalSection(&ctx->lock);
    return 11;
   }
  }
  if (raya_reaper_integrated_can_exit(&ctx->state)) {
   LeaveCriticalSection(&ctx->lock);
   return 0;
  }
  built=raya_reaper_waitplan_build(&ctx->state.registry,ctx->wake,&plan);
  if (built!=RAYA_WAITPLAN_READY) {
   (void)raya_reaper_mark_degraded_cause(&ctx->state.core,RAYA_RECOVERY_CAUSE_WAITSET_INVALID);
   LeaveCriticalSection(&ctx->lock);
   return 12;
  }
  LeaveCriticalSection(&ctx->lock);

  wr=WaitForMultipleObjects(plan.waitset.handle_count,
                            plan.waitset.handles,FALSE,100);
  if (wr==WAIT_FAILED) {
   EnterCriticalSection(&ctx->lock);
   (void)raya_reaper_mark_degraded_cause(&ctx->state.core,RAYA_RECOVERY_CAUSE_WAIT_FAILED);
   LeaveCriticalSection(&ctx->lock);
   return 13;
  }
  if (wr==WAIT_TIMEOUT || wr==WAIT_OBJECT_0) continue;
  {
   raya_reaper_op *base=NULL;
   HANDLE event_handle=NULL;
   raya_waitplan_result resolved;
   raya_integrated_op *op;
   EnterCriticalSection(&ctx->lock);
   resolved=raya_reaper_waitplan_resolve(&ctx->state.registry,&plan,wr,
                                         &base,&event_handle);
   if (resolved!=RAYA_WAITPLAN_OPERATION || base==NULL ||
       !raya_reaper_integrated_claim(&ctx->state,
           (raya_integrated_op *)base,event_handle)) {
    (void)raya_reaper_mark_degraded_cause(&ctx->state.core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
    LeaveCriticalSection(&ctx->lock);
    return 14;
   }
   op=(raya_integrated_op *)base;
   LeaveCriticalSection(&ctx->lock);

   /* Test-only collector: event signal is the simulated terminal proof. */
   if (WaitForSingleObject(event_handle,0)!=WAIT_OBJECT_0) {
    EnterCriticalSection(&ctx->lock);
    if (raya_reaper_integrated_collect(&ctx->state,op,
                                         RAYA_INTEGRATED_INCOMPLETE)!=RAYA_INTEGRATED_OK) {
     (void)raya_reaper_mark_degraded_cause(&ctx->state.core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
     LeaveCriticalSection(&ctx->lock);
     return 15;
    }
    LeaveCriticalSection(&ctx->lock);
    continue;
   }
   EnterCriticalSection(&ctx->lock);
   if (raya_reaper_integrated_collect(&ctx->state,op,
                                      RAYA_INTEGRATED_TERMINAL_SUCCESS)
       !=RAYA_INTEGRATED_OK) {
    (void)raya_reaper_mark_degraded_cause(&ctx->state.core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
    LeaveCriticalSection(&ctx->lock);
    return 16;
   }
   InterlockedIncrement(&ctx->collected);
   LeaveCriticalSection(&ctx->lock);
  }
 }
}

int main(void) {
 size_t i;
 DWORD wr;
 memset(&g,0,sizeof(g));
 InitializeCriticalSection(&g.lock);
 g.wake=CreateEventW(NULL,TRUE,FALSE,NULL);
 CHECK(g.wake!=NULL);
 CHECK(raya_reaper_integrated_init(&g.state));
 for (i=0;i<TOTAL;i++) {
  g.events[i]=CreateEventW(NULL,TRUE,FALSE,NULL);
  CHECK(g.events[i]!=NULL);
  g.ops[i].base.state=RAYA_REAPER_OP_CALLER_PENDING;
  g.ops[i].completion_event=g.events[i];
 }
 g.worker=CreateThread(NULL,0,worker_main,&g,0,NULL);
 CHECK(g.worker!=NULL);
 for (i=0;i<PRODUCERS;i++) {
  g.producers[i]=CreateThread(NULL,0,producer_main,
                              (LPVOID)(uintptr_t)i,0,NULL);
  CHECK(g.producers[i]!=NULL);
 }
 wr=WaitForMultipleObjects(PRODUCERS,g.producers,TRUE,30000);
 CHECK(wr==WAIT_OBJECT_0);
 CHECK(g.producer_done==PRODUCERS);
 CHECK(g.handed_off==TOTAL);
 EnterCriticalSection(&g.lock);
 CHECK(raya_reaper_integrated_shutdown(&g.state));
 CHECK(SetEvent(g.wake));
 LeaveCriticalSection(&g.lock);
 wr=WaitForSingleObject(g.worker,30000);
 CHECK(wr==WAIT_OBJECT_0);
 CHECK(g.collected==TOTAL);
 CHECK(g.failures==0);
 EnterCriticalSection(&g.lock);
 CHECK(g.state.registry.count==0);
 CHECK(g.state.core.outstanding==0);
 CHECK(g.state.core.queued==0);
 CHECK(g.state.core.caller_active==0);
 CHECK(g.state.reserved_slots==0);
 CHECK(g.state.core.degradation_causes==0);
 CHECK(raya_reaper_integrated_can_exit(&g.state));
 CHECK(raya_reaper_mark_stopped(&g.state.core));
 LeaveCriticalSection(&g.lock);
 for (i=0;i<PRODUCERS;i++) CHECK(CloseHandle(g.producers[i]));
 CHECK(CloseHandle(g.worker));
 for (i=0;i<TOTAL;i++) CHECK(CloseHandle(g.events[i]));
 CHECK(CloseHandle(g.wake));
 DeleteCriticalSection(&g.lock);
 printf("Integrated concurrent producers/worker: %ld assertions, %ld failures; handed_off=%ld collected=%ld\n",
        g_tests,g_failures,g.handed_off,g.collected);
 return g_failures ? 1 : 0;
}
