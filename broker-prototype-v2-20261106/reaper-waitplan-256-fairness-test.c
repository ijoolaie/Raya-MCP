#include "reaper-waitplan-v2.h"
#include <stdio.h>
#include <string.h>
#define N RAYA_REAPER_REGISTRY_CAPACITY
#define CHECK(x) do { ++tests; if(!(x)){++failures;printf("FAIL: %s\n",#x);} } while(0)
static int tests,failures;
int main(void) {
 raya_reaper_registry registry;
 raya_reaper_op ops[N],*found=NULL;
 HANDLE events[N],wake,found_event=NULL;
 unsigned char seen[N];
 size_t i,total=0,windows=0;
 memset(&registry,0,sizeof(registry));
 memset(ops,0,sizeof(ops));
 memset(seen,0,sizeof(seen));
 wake=CreateEventW(NULL,TRUE,FALSE,NULL);
 CHECK(wake!=NULL);
 for(i=0;i<N;i++) {
  events[i]=CreateEventW(NULL,TRUE,FALSE,NULL);
  CHECK(events[i]!=NULL);
  CHECK(raya_reaper_registry_add(&registry,&ops[i],events[i]));
  CHECK(SetEvent(events[i]));
 }
 while(total<N && windows<8) {
  raya_reaper_waitplan plan;
  raya_waitplan_result built;
  size_t observed_this_window=0;
  built=raya_reaper_waitplan_build(&registry,wake,&plan);
  CHECK(built==RAYA_WAITPLAN_READY);
  if(built!=RAYA_WAITPLAN_READY) break;
  CHECK(plan.waitset.handle_count<=64 && plan.waitset.operation_count<=63);
  ++windows;
  for(;;) {
   DWORD wr=WaitForMultipleObjects(plan.waitset.handle_count,
                                   plan.waitset.handles,FALSE,0);
   raya_waitplan_result rr;
   if(wr==WAIT_TIMEOUT) break;
   found=NULL; found_event=NULL;
   rr=raya_reaper_waitplan_resolve(&registry,&plan,wr,&found,&found_event);
   CHECK(rr==RAYA_WAITPLAN_OPERATION);
   if(rr!=RAYA_WAITPLAN_OPERATION || found==NULL || found_event==NULL) break;
   {
    size_t index=(size_t)(found-ops);
    CHECK(index<N && found_event==events[index]);
    if(index<N && found_event==events[index] && !seen[index]) {
     seen[index]=1; ++total; ++observed_this_window;
    } else if(index<N && seen[index]) {
     CHECK(0 && "operation observed more than once");
    }
   }
   CHECK(ResetEvent(found_event));
  }
  CHECK(observed_this_window==plan.waitset.operation_count ||
        (total==N && observed_this_window<=plan.waitset.operation_count));
 }
 CHECK(total==N);
 for(i=0;i<N;i++) CHECK(seen[i]==1);
 CHECK(windows==5);
 for(i=0;i<N;i++) CHECK(CloseHandle(events[i]));
 CHECK(CloseHandle(wake));
 printf("WaitPlan 256-event fairness: %d assertions, %d failures; windows=%llu observed=%llu\n",
        tests,failures,(unsigned long long)windows,(unsigned long long)total);
 return failures?1:0;
}
