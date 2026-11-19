#include "reaper-integrated-v2.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { InterlockedIncrement(&tests); if(!(x)){InterlockedIncrement(&failures);printf("FAIL: %s\n",#x);} } while(0)
static CRITICAL_SECTION lock;
static HANDLE gate,wake,event_handle;
static raya_reaper_integrated state;
static raya_integrated_op op,*promoted=NULL;
static volatile LONG tests=0,failures=0,handoff_result=0,new_admit_result=0;

static DWORD WINAPI admitted_caller(LPVOID unused) {
 int ok;
 (void)unused;
 (void)WaitForSingleObject(gate,INFINITE);
 EnterCriticalSection(&lock);
 ok=raya_reaper_integrated_handoff(&state,&op);
 if(ok && !SetEvent(wake)) (void)raya_reaper_mark_degraded_cause(&state.core,RAYA_RECOVERY_CAUSE_WAKE_SIGNAL);
 LeaveCriticalSection(&lock);
 if(ok && !SetEvent(event_handle)) ok=0;
 InterlockedExchange(&handoff_result,ok);
 return 0;
}
static DWORD WINAPI late_caller(LPVOID unused) {
 int ok;
 (void)unused;
 (void)WaitForSingleObject(gate,INFINITE);
 EnterCriticalSection(&lock);
 ok=raya_reaper_integrated_admit(&state);
 if(ok) (void)raya_reaper_caller_finish(&state.core);
 LeaveCriticalSection(&lock);
 InterlockedExchange(&new_admit_result,ok);
 return 0;
}
int main(void) {
 HANDLE a,b;
 DWORD wr;
 InitializeCriticalSection(&lock);
 gate=CreateEventW(NULL,TRUE,FALSE,NULL);
 wake=CreateEventW(NULL,TRUE,FALSE,NULL);
 event_handle=CreateEventW(NULL,TRUE,FALSE,NULL);
 memset(&op,0,sizeof(op));
 op.base.state=RAYA_REAPER_OP_CALLER_PENDING;
 op.completion_event=event_handle;
 CHECK(gate!=NULL && wake!=NULL && event_handle!=NULL);
 CHECK(raya_reaper_integrated_init(&state));
 EnterCriticalSection(&lock);
 CHECK(raya_reaper_integrated_admit(&state));
 CHECK(raya_reaper_integrated_reserve(&state,&op));
 LeaveCriticalSection(&lock);
 a=CreateThread(NULL,0,admitted_caller,NULL,0,NULL);
 b=CreateThread(NULL,0,late_caller,NULL,0,NULL);
 CHECK(a!=NULL && b!=NULL);
 EnterCriticalSection(&lock);
 CHECK(raya_reaper_integrated_shutdown(&state));
 LeaveCriticalSection(&lock);
 CHECK(SetEvent(gate));
 wr=WaitForMultipleObjects(2,(HANDLE[]){a,b},TRUE,10000);
 CHECK(wr==WAIT_OBJECT_0);
 CHECK(handoff_result==1);
 CHECK(new_admit_result==0);
 CHECK(WaitForSingleObject(event_handle,0)==WAIT_OBJECT_0);
 EnterCriticalSection(&lock);
 CHECK(raya_reaper_integrated_promote_one(&state,&promoted)==RAYA_INTEGRATED_OK);
 CHECK(promoted==&op);
 CHECK(raya_reaper_integrated_claim(&state,&op,event_handle));
 CHECK(raya_reaper_integrated_collect(&state,&op,RAYA_INTEGRATED_TERMINAL_SUCCESS)==RAYA_INTEGRATED_OK);
 CHECK(raya_reaper_integrated_can_exit(&state));
 CHECK(state.core.degradation_causes==0);
 CHECK(raya_reaper_mark_stopped(&state.core));
 LeaveCriticalSection(&lock);
 CHECK(CloseHandle(a)); CHECK(CloseHandle(b));
 CHECK(CloseHandle(gate)); CHECK(CloseHandle(wake)); CHECK(CloseHandle(event_handle));
 DeleteCriticalSection(&lock);
 printf("Concurrent shutdown admission race: %ld assertions, %ld failures\n",tests,failures);
 return failures?1:0;
}
