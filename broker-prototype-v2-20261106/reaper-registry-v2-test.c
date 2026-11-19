#include "reaper-registry-v2.h"
#include <stdio.h>
#include <string.h>
static int failures;
#define CHECK(x, label) do { if (x) printf("PASS: %s\n", label); else { printf("FAIL: %s\n", label); ++failures; } } while (0)
int main(void) {
    raya_reaper_registry r;
    raya_reaper_op ops[4];
    raya_reaper_op *out_ops[4];
    HANDLE events[4];
    size_t count=99, cursor=99;
    memset(&r,0,sizeof(r)); memset(ops,0,sizeof(ops));
    events[0]=(HANDLE)(ULONG_PTR)0x101;
    events[1]=(HANDLE)(ULONG_PTR)0x102;
    events[2]=(HANDLE)(ULONG_PTR)0x103;
    events[3]=(HANDLE)(ULONG_PTR)0x104;
    CHECK(raya_reaper_registry_add(&r,&ops[0],events[0]),"register first operation");
    CHECK(raya_reaper_registry_add(&r,&ops[1],events[1]),"register second operation");
    CHECK(!raya_reaper_registry_add(&r,&ops[0],events[2]),"reject duplicate operation pointer");
    CHECK(!raya_reaper_registry_add(&r,&ops[2],events[0]),"reject duplicate event handle");
    CHECK(!raya_reaper_registry_add(&r,&ops[2],NULL),"reject null event handle");
    CHECK(raya_reaper_registry_snapshot(&r,out_ops,events,4,&count,&cursor) &&
          count==2 && out_ops[0]==&ops[0] && out_ops[1]==&ops[1],
          "snapshot preserves operation/event pairing");
    CHECK(raya_reaper_registry_remove(&r,&ops[0]),"remove exact operation");
    CHECK(!raya_reaper_registry_remove(&r,&ops[0]),"reject duplicate removal");
    CHECK(raya_reaper_registry_snapshot(&r,out_ops,events,4,&count,&cursor) &&
          count==1 && out_ops[0]==&ops[1] && events[0]==(HANDLE)(ULONG_PTR)0x102,
          "compaction preserves remaining pairing");
    CHECK(!raya_reaper_registry_snapshot(&r,out_ops,events,0,&count,&cursor),
          "reject undersized snapshot buffers");
    CHECK(raya_reaper_registry_remove(&r,&ops[1]),"remove final operation");
    CHECK(raya_reaper_registry_snapshot(&r,NULL,NULL,0,&count,&cursor) &&
          count==0 && cursor==0,"empty snapshot needs no arrays");
    printf("SUMMARY: %d failure(s)\n",failures);
    return failures ? 1 : 0;
}
