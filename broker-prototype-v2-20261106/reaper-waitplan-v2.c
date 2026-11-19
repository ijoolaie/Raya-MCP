#include "reaper-waitplan-v2.h"
#include <string.h>
raya_waitplan_result raya_reaper_waitplan_build(
    raya_reaper_registry *r, HANDLE wake, raya_reaper_waitplan *out) {
    HANDLE events[RAYA_REAPER_REGISTRY_CAPACITY];
    size_t count=0, cursor=0;
    if (r==NULL || out==NULL) return RAYA_WAITPLAN_INVALID;
    memset(out,0,sizeof(*out));
    if (!raya_reaper_registry_snapshot(r,out->operations,events,
          RAYA_REAPER_REGISTRY_CAPACITY,&count,&cursor))
        return RAYA_WAITPLAN_INVALID;
    if (raya_reaper_waitset_build(wake,events,count,cursor,
                                  &out->waitset)!=RAYA_WAITSET_READY)
        return RAYA_WAITPLAN_INVALID;
    out->registry_count=count;
    r->cursor=out->waitset.next_cursor;
    return RAYA_WAITPLAN_READY;
}
raya_waitplan_result raya_reaper_waitplan_resolve(
    const raya_reaper_registry *r, const raya_reaper_waitplan *p,
    DWORD wr, raya_reaper_op **out_op, HANDLE *out_event) {
    DWORD slot;
    size_t i, index;
    raya_reaper_op *op;
    HANDLE event_handle;
    if (out_op!=NULL) *out_op=NULL;
    if (out_event!=NULL) *out_event=NULL;
    if (r==NULL || p==NULL || out_op==NULL || out_event==NULL ||
        p->waitset.handle_count==0 || p->waitset.handle_count>64 ||
        p->waitset.operation_count>63 ||
        p->waitset.handle_count!=p->waitset.operation_count+1)
        return RAYA_WAITPLAN_INVALID;
    if (wr==WAIT_TIMEOUT) return RAYA_WAITPLAN_TIMEOUT;
    if (wr==WAIT_FAILED) return RAYA_WAITPLAN_FAILED;
    if (wr==WAIT_OBJECT_0) return RAYA_WAITPLAN_WAKE;
    if (wr<WAIT_OBJECT_0+1 ||
        wr>=WAIT_OBJECT_0+1+p->waitset.operation_count)
        return RAYA_WAITPLAN_INVALID;
    slot=wr-WAIT_OBJECT_0-1;
    index=p->waitset.operation_indices[slot];
    if (index>=p->registry_count) return RAYA_WAITPLAN_INVALID;
    op=p->operations[index];
    event_handle=p->waitset.handles[slot+1];
    if (op==NULL || event_handle==NULL ||
        event_handle==INVALID_HANDLE_VALUE) return RAYA_WAITPLAN_INVALID;
    for (i=0;i<r->count;i++) {
        if (r->entries[i].op==op &&
            r->entries[i].event_handle==event_handle) {
            *out_op=op;
            *out_event=event_handle;
            return RAYA_WAITPLAN_OPERATION;
        }
    }
    return RAYA_WAITPLAN_STALE;
}
