#ifndef RAYA_REAPER_WAITPLAN_V2_H
#define RAYA_REAPER_WAITPLAN_V2_H
#include "reaper-registry-v2.h"
#include "reaper-waitset-v2.h"
typedef struct {
    raya_reaper_waitset waitset;
    raya_reaper_op *operations[RAYA_REAPER_REGISTRY_CAPACITY];
    size_t registry_count;
} raya_reaper_waitplan;
typedef enum {
    RAYA_WAITPLAN_INVALID=0,
    RAYA_WAITPLAN_READY=1,
    RAYA_WAITPLAN_WAKE=2,
    RAYA_WAITPLAN_OPERATION=3,
    RAYA_WAITPLAN_TIMEOUT=4,
    RAYA_WAITPLAN_FAILED=5,
    RAYA_WAITPLAN_STALE=6
} raya_waitplan_result;
/* Build and resolve while holding the shared registry/ownership lock. */
raya_waitplan_result raya_reaper_waitplan_build(
    raya_reaper_registry *registry, HANDLE wake_event,
    raya_reaper_waitplan *out);
raya_waitplan_result raya_reaper_waitplan_resolve(
    const raya_reaper_registry *registry, const raya_reaper_waitplan *plan,
    DWORD wait_result, raya_reaper_op **out_op, HANDLE *out_event);
#endif
