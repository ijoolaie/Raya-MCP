#include "reaper-window-v2.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static int failures;
#define CHECK(x,n) do { if(x) printf("PASS: %s\n",n); else { printf("FAIL: %s\n",n); ++failures; } } while(0)
static int unique_and_in_range(const size_t *a,size_t n,size_t total) {
    size_t i,j;
    for(i=0;i<n;i++) {
        if(a[i]>=total) return 0;
        for(j=0;j<i;j++) if(a[i]==a[j]) return 0;
    }
    return 1;
}
static int exhaustive_coverage(void) {
    size_t total,cursor,next,n,i,rounds,seen[256],a[RAYA_REAPER_WINDOW_CAPACITY];
    for(total=1;total<=256;total++) {
        memset(seen,0,sizeof(seen)); cursor=0;
        rounds=(total+RAYA_REAPER_WINDOW_CAPACITY-1)/RAYA_REAPER_WINDOW_CAPACITY;
        for(i=0;i<rounds;i++) {
            n=raya_reaper_build_window(total,cursor,a,&next);
            if(n==0 || n>RAYA_REAPER_WINDOW_CAPACITY ||
               !unique_and_in_range(a,n,total)) return 0;
            for(size_t j=0;j<n;j++) seen[a[j]]++;
            cursor=next;
        }
        for(i=0;i<total;i++) if(seen[i]==0) return 0;
    }
    return 1;
}
int main(void) {
    size_t a[RAYA_REAPER_WINDOW_CAPACITY], next=999, n, i, seen[130]={0};
    n=raya_reaper_build_window(0,0,a,&next);
    CHECK(n==0 && next==0,"empty set produces empty window and zero cursor");
    n=raya_reaper_build_window(1,0,a,&next);
    CHECK(n==1 && a[0]==0 && next==0,"single operation window");
    n=raya_reaper_build_window(63,0,a,&next);
    CHECK(n==63 && unique_and_in_range(a,n,63) && next==0,"63 operations fit exactly");
    n=raya_reaper_build_window(64,0,a,&next);
    CHECK(n==63 && unique_and_in_range(a,n,64) && next==63,"64 operations leave one for next rotation");
    n=raya_reaper_build_window(64,next,a,&next);
    CHECK(n==63 && unique_and_in_range(a,n,64),"rotated 64-operation window remains unique");
    CHECK(a[0]==63 && next==62,"rotation wraps without duplicate indices");
    {
        size_t cursor=0, round, total=130;
        for(round=0;round<3;round++) {
            n=raya_reaper_build_window(total,cursor,a,&next);
            CHECK(n==63 && unique_and_in_range(a,n,total),"130-operation window is bounded and unique");
            for(i=0;i<n;i++) seen[a[i]]++;
            cursor=next;
        }
    }
    {
        int all=1;
        for(i=0;i<130;i++) if(seen[i]==0) all=0;
        CHECK(all,"three rotations discover every operation in a 130-item stable set");
    }
    n=raya_reaper_build_window(130,129,a,&next);
    CHECK(n==63 && a[0]==129 && a[1]==0 && a[62]==61 && next==62,
          "wraparound from final index is contiguous");
    n=raya_reaper_build_window(5,17,a,&next);
    CHECK(n==5 && a[0]==2 && a[4]==1 && next==2,
          "cursor beyond total is normalized");
    n=raya_reaper_build_window(5,0,NULL,&next);
    CHECK(n==0 && next==0,"null output array rejected safely");
    n=raya_reaper_build_window(5,0,a,NULL);
    CHECK(n==0,"null next cursor rejected safely");
    n=raya_reaper_build_window(SIZE_MAX,SIZE_MAX-1,a,&next);
    CHECK(n==63 && a[0]==SIZE_MAX-1 && a[1]==0 && next==62,
          "SIZE_MAX total wraps without arithmetic overflow");
    CHECK(exhaustive_coverage(),"exhaustive at-least-once coverage for totals 1..256");
    printf("SUMMARY: %d failure(s)\n",failures);
    return failures?1:0;
}
