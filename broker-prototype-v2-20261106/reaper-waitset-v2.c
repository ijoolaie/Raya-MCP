#include "reaper-waitset-v2.h"
#include <string.h>
static int valid_event(HANDLE h) { return h!=NULL && h!=INVALID_HANDLE_VALUE; }
raya_waitset_status raya_reaper_waitset_build(
    HANDLE wake, const HANDLE *events, size_t total, size_t cursor,
    raya_reaper_waitset *out) {
    size_t i,j,n,indices[63];
    if(out==NULL || !valid_event(wake) || (total!=0 && events==NULL))
        return RAYA_WAITSET_INVALID;
    memset(out,0,sizeof(*out));
    n=raya_reaper_build_window(total,cursor,indices,&out->next_cursor);
    out->handles[0]=wake;
    out->handle_count=1;
    for(i=0;i<n;i++) {
        HANDLE h=events[indices[i]];
        if(!valid_event(h) || h==wake) return RAYA_WAITSET_INVALID;
        for(j=0;j<i;j++) if(events[indices[j]]==h) return RAYA_WAITSET_INVALID;
        out->handles[i+1]=h;
        out->operation_indices[i]=indices[i];
    }
    out->operation_count=(DWORD)n;
    out->handle_count=(DWORD)n+1;
    return RAYA_WAITSET_READY;
}
