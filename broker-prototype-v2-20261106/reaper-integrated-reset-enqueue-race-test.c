#include "reaper-integrated-v2.h"
#include <stdio.h>
#include <string.h>

#define PRODUCERS 4
#define EACH 64
#define TOTAL (PRODUCERS*EACH)
#define CHECK(x) do { InterlockedIncrement(&g_tests); if (!(x)) { InterlockedIncrement(&g_failures); printf("FAIL: %s\n",#x); } } while(0)

typedef struct {
 CRITICAL_SECTION lock;
 HANDLE wake;
 HANDLE start;
 HANDLE producers[PRODUCERS];
 HANDLE worker;
 raya_reaper_integrated state;
 raya_integrated_op ops[TOTAL];
 HANDLE events[TOTAL];
 volatile LONG producer_done;
 volatile LONG handed_off;
 volatile LONG promoted;
 volatile LONG collected;
 volatile LONG reset_count;
 volatile LONG wake_count;
 volatile LONG failures;
} context;

static context g;
static volatile LONG g_tests=0,g_failures=0;

static DWORD WINAPI producer_main(LPVOID arg) {
 size_t p=(size_t)(uintptr_t)arg,j;
 (void)WaitForSingleObject(g.start,INFINITE);
 for(j=0;j<EACH;j++) {
  size_t n=p*EACH+j;
  raya_integrated_op *op=&g.ops[n];
  int admitted,reserved,handed=0,signal_ok=1;
  EnterCriticalSection(&g.lock);
  admitted=raya_reaper_integrated_admit(&g.state);
  reserved=admitted ? raya_reaper_integrated_reserve(&g.state,op) : 0;
  if(admitted && reserved)
   handed=raya_reaper_integrated_handoff(&g.state,op);
  if(handed) {
   InterlockedIncrement(&g.handed_off);
   signal_ok=SetEvent(g.wake)!=0;
   if(signal_ok) InterlockedIncrement(&g.wake_count);
   else (void)raya_reaper_mark_degraded_cause(&g.state.core,RAYA_RECOVERY_CAUSE_WAKE_SIGNAL);
  } else {
   if(admitted && !reserved) (void)raya_reaper_caller_finish(&g.state.core);
   if(reserved) (void)raya_reaper_mark_degraded_cause(&g.state.core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
   InterlockedIncrement(&g.failures);
  }
  LeaveCriticalSection(&g.lock);
  if(handed && !SetEvent(g.events[n])) InterlockedIncrement(&g.failures);
  if((j&3u)==0) SwitchToThread();
 }
 InterlockedIncrement(&g.producer_done);
 return 0;
}

static DWORD WINAPI worker_main(LPVOID arg) {
 context *c=(context *)arg;
 (void)WaitForSingleObject(c->start,INFINITE);
 for(;;) {
  raya_integrated_op *snapshot[TOTAL];
  HANDLE event_snapshot[TOTAL];
  size_t count=0,i;
  int progress=0;
  EnterCriticalSection(&c->lock);
  if(!ResetEvent(c->wake)) {
   (void)raya_reaper_mark_degraded_cause(&c->state.core,RAYA_RECOVERY_CAUSE_WAITSET_INVALID);
   LeaveCriticalSection(&c->lock);
   return 20;
  }
  InterlockedIncrement(&c->reset_count);
  for(;;) {
   raya_integrated_op *op=NULL;
   raya_integrated_result pr=raya_reaper_integrated_promote_one(&c->state,&op);
   if(pr==RAYA_INTEGRATED_EMPTY) break;
   if(pr!=RAYA_INTEGRATED_OK || op==NULL) {
    (void)raya_reaper_mark_degraded_cause(&c->state.core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
    LeaveCriticalSection(&c->lock);
    return 21;
   }
   InterlockedIncrement(&c->promoted);
   progress=1;
  }
  count=c->state.registry.count;
  for(i=0;i<count;i++) {
   snapshot[i]=(raya_integrated_op *)c->state.registry.entries[i].op;
   event_snapshot[i]=c->state.registry.entries[i].event_handle;
  }
  if(raya_reaper_integrated_can_exit(&c->state)) {
   LeaveCriticalSection(&c->lock);
   return 0;
  }
  LeaveCriticalSection(&c->lock);

  for(i=0;i<count;i++) {
   if(WaitForSingleObject(event_snapshot[i],0)==WAIT_OBJECT_0) {
    raya_integrated_op *op=snapshot[i];
    EnterCriticalSection(&c->lock);
    if(raya_reaper_integrated_claim(&c->state,op,event_snapshot[i])) {
     if(raya_reaper_integrated_collect(&c->state,op,
          RAYA_INTEGRATED_TERMINAL_SUCCESS)==RAYA_INTEGRATED_OK) {
      InterlockedIncrement(&c->collected);
      progress=1;
     } else {
      (void)raya_reaper_mark_degraded_cause(&c->state.core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
      LeaveCriticalSection(&c->lock);
      return 22;
     }
    }
    LeaveCriticalSection(&c->lock);
   }
  }
  if(!progress) (void)WaitForSingleObject(c->wake,2);
 }
}

int main(void) {
 size_t i;
 DWORD wr;
 memset(&g,0,sizeof(g));
 InitializeCriticalSection(&g.lock);
 g.wake=CreateEventW(NULL,TRUE,FALSE,NULL);
 g.start=CreateEventW(NULL,TRUE,FALSE,NULL);
 CHECK(g.wake!=NULL && g.start!=NULL);
 CHECK(raya_reaper_integrated_init(&g.state));
 for(i=0;i<TOTAL;i++) {
  g.events[i]=CreateEventW(NULL,TRUE,FALSE,NULL);
  CHECK(g.events[i]!=NULL);
  g.ops[i].base.state=RAYA_REAPER_OP_CALLER_PENDING;
  g.ops[i].completion_event=g.events[i];
 }
 g.worker=CreateThread(NULL,0,worker_main,&g,0,NULL);
 CHECK(g.worker!=NULL);
 for(i=0;i<PRODUCERS;i++) {
  g.producers[i]=CreateThread(NULL,0,producer_main,(LPVOID)(uintptr_t)i,0,NULL);
  CHECK(g.producers[i]!=NULL);
 }
 CHECK(SetEvent(g.start));
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
 CHECK(g.promoted==TOTAL);
 CHECK(g.collected==TOTAL);
 CHECK(g.failures==0);
 CHECK(g.reset_count>0 && g.wake_count==TOTAL);
 EnterCriticalSection(&g.lock);
 CHECK(g.state.registry.count==0);
 CHECK(g.state.core.outstanding==0 && g.state.core.queued==0);
 CHECK(g.state.core.caller_active==0 && g.state.reserved_slots==0);
 CHECK(g.state.core.degradation_causes==0);
 CHECK(raya_reaper_integrated_can_exit(&g.state));
 CHECK(raya_reaper_mark_stopped(&g.state.core));
 LeaveCriticalSection(&g.lock);
 for(i=0;i<PRODUCERS;i++) CHECK(CloseHandle(g.producers[i]));
 CHECK(CloseHandle(g.worker));
 for(i=0;i<TOTAL;i++) CHECK(CloseHandle(g.events[i]));
 CHECK(CloseHandle(g.start));
 CHECK(CloseHandle(g.wake));
 DeleteCriticalSection(&g.lock);
 printf("Reset/enqueue race: %ld assertions, %ld failures; handoff=%ld promoted=%ld collected=%ld resets=%ld\n",
  g_tests,g_failures,g.handed_off,g.promoted,g.collected,g.reset_count);
 return g_failures ? 1 : 0;
}
