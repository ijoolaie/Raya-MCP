#include "reaper-waitplan-v2.h"
#include <stdio.h>
#include <string.h>
static int failures;
#define CHECK(x, label) do { if (x) printf("PASS: %s\n", label); else { printf("FAIL: %s\n", label); ++failures; } } while (0)
int main(void) {
    raya_reaper_registry r;
    raya_reaper_op ops[70], *found=NULL;
    HANDLE events[70], found_event=NULL, wake;
    raya_reaper_waitplan p;
    size_t i;
    memset(&r,0,sizeof(r)); memset(ops,0,sizeof(ops));
    wake=CreateEventW(NULL,TRUE,FALSE,NULL);
    CHECK(wake!=NULL,"create real wake event");
    for (i=0;i<70;i++) {
        events[i]=CreateEventW(NULL,TRUE,FALSE,NULL);
        if (events[i]==NULL) break;
        if (!raya_reaper_registry_add(&r,&ops[i],events[i])) break;
    }
    CHECK(i==70,"register 70 real operation events");
    CHECK(raya_reaper_waitplan_build(&r,wake,&p)==RAYA_WAITPLAN_READY,
          "build integrated registry snapshot and 63-slot wait plan");
    CHECK(p.waitset.handle_count==64 && p.waitset.operation_count==63 &&
          p.registry_count==70,"plan respects Windows 64-handle limit");
    CHECK(raya_reaper_waitplan_resolve(&r,&p,WAIT_OBJECT_0,
          &found,&found_event)==RAYA_WAITPLAN_WAKE,
          "resolve wake slot independently");
    SetEvent(p.waitset.handles[1]);
    {
        DWORD wr=WaitForMultipleObjects(p.waitset.handle_count,
                                        p.waitset.handles,FALSE,1000);
        CHECK(wr==WAIT_OBJECT_0+1,"wait detects first operation event");
        CHECK(raya_reaper_waitplan_resolve(&r,&p,wr,&found,&found_event)==
              RAYA_WAITPLAN_OPERATION && found==&ops[0] &&
              found_event==events[0],"map wait slot to exact operation/event pair");
    }
    CHECK(raya_reaper_waitplan_build(&r,wake,&p)==RAYA_WAITPLAN_READY &&
          p.waitset.operation_count==63,"rotation builds next overlapping 63-event window");
    SetEvent(p.waitset.handles[1]);
    {
        DWORD wr=WaitForMultipleObjects(p.waitset.handle_count,
                                        p.waitset.handles,FALSE,1000);
        CHECK(wr==WAIT_OBJECT_0+1,"second rotation detects first event in its window");
        CHECK(raya_reaper_waitplan_resolve(&r,&p,wr,&found,&found_event)==
              RAYA_WAITPLAN_OPERATION && found==&ops[63] &&
              found_event==events[63],"rotation maps slot to original registry index");
    }
    CHECK(raya_reaper_waitplan_resolve(&r,&p,WAIT_TIMEOUT,&found,&found_event)==
          RAYA_WAITPLAN_TIMEOUT,"resolve timeout without operation selection");
    CHECK(raya_reaper_registry_remove(&r,&ops[63]),"remove selected entry for stale-result test");
    CHECK(raya_reaper_waitplan_resolve(&r,&p,WAIT_OBJECT_0+1,&found,&found_event)==
          RAYA_WAITPLAN_STALE,"reject result whose operation was removed after snapshot");
    CHECK(raya_reaper_waitplan_resolve(&r,&p,WAIT_FAILED,&found,&found_event)==
          RAYA_WAITPLAN_FAILED,"preserve wait failure as nonterminal outcome");
    for (i=0;i<70;i++) if (events[i]!=NULL) CloseHandle(events[i]);
    if (wake!=NULL) CloseHandle(wake);
    printf("SUMMARY: %d failure(s)\n",failures);
    return failures ? 1 : 0;
}
