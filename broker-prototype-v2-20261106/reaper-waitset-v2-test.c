#include "reaper-waitset-v2.h"
#include <stdio.h>
#include <string.h>
static int failures;
#define CHECK(x,n) do { if(x) printf("PASS: %s\n",n); else { printf("FAIL: %s\n",n); ++failures; } } while(0)
int main(void) {
 HANDLE wake=NULL, ev[130], duplicate[2]; raya_reaper_waitset s;
 size_t i,cursor=0,seen[130]={0},round;
 HANDLE e;
 wake=CreateEventW(NULL,TRUE,FALSE,NULL);
 for(i=0;i<130;i++) ev[i]=CreateEventW(NULL,TRUE,FALSE,NULL);
 CHECK(wake!=NULL,"create wake event");
 for(i=0;i<130;i++) if(ev[i]==NULL) break;
 CHECK(i==130,"create 130 distinct operation events");
 CHECK(raya_reaper_waitset_build(wake,ev,63,0,&s)==RAYA_WAITSET_READY &&
       s.handle_count==64 && s.operation_count==63,
       "one wake plus 63 operations fits Windows wait limit");
 CHECK(raya_reaper_waitset_build(wake,ev,130,0,&s)==RAYA_WAITSET_READY &&
       s.handle_count==64 && s.operation_count==63 && s.next_cursor==63,
       "first 130-operation window bounded to 63");
 for(round=0;round<3;round++) {
  CHECK(raya_reaper_waitset_build(wake,ev,130,cursor,&s)==RAYA_WAITSET_READY,
        "rotating waitset builds successfully");
  for(i=0;i<s.operation_count;i++) seen[s.operation_indices[i]]++;
  cursor=s.next_cursor;
 }
 for(i=0;i<130;i++) if(seen[i]==0) break;
 CHECK(i==130,"three rotations cover every operation event");
 duplicate[0]=ev[0];duplicate[1]=ev[0];
 CHECK(raya_reaper_waitset_build(wake,duplicate,2,0,&s)==RAYA_WAITSET_INVALID,
       "duplicate operation event rejected");
 CHECK(raya_reaper_waitset_build(wake,NULL,1,0,&s)==RAYA_WAITSET_INVALID,
       "missing operation array rejected");
 CHECK(raya_reaper_waitset_build(wake,ev,0,0,&s)==RAYA_WAITSET_READY &&
       s.handle_count==1 && s.operation_count==0,
       "empty operation set waits only on wake event");
 e=wake;
 CHECK(raya_reaper_waitset_build(e,&e,1,0,&s)==RAYA_WAITSET_INVALID,
       "wake event cannot also appear as operation event");
 {
  HANDLE hs[2]={wake,ev[0]};
  CHECK(SetEvent(ev[0]) && WaitForMultipleObjects(2,hs,FALSE,1000)==WAIT_OBJECT_0+1,
        "Windows wait identifies a signaled operation slot");
 }
 for(i=0;i<130;i++) if(ev[i]) CloseHandle(ev[i]);
 if(wake) CloseHandle(wake);
 printf("SUMMARY: %d failure(s)\n",failures);return failures?1:0;
}
