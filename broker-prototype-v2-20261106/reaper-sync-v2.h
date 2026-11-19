#ifndef RAYA_REAPER_SYNC_V2_H
#define RAYA_REAPER_SYNC_V2_H
#include <windows.h>
#include "reaper-core-v2.h"
typedef struct {
    CRITICAL_SECTION lock;
    HANDLE wake_event;
    raya_reaper_core core;
    BOOL initialized;
} raya_reaper_sync;
typedef enum { RAYA_REAPER_TAKE_ERROR=-1, RAYA_REAPER_TAKE_TIMEOUT=0,
               RAYA_REAPER_TAKE_OPERATION=1, RAYA_REAPER_TAKE_EXIT=2 } raya_reaper_take_result;
typedef enum {
    RAYA_REAPER_HANDOFF_NOT_TRANSFERRED=0,
    RAYA_REAPER_HANDOFF_TRANSFERRED=1,
    RAYA_REAPER_HANDOFF_TRANSFERRED_DEGRADED=2
} raya_reaper_handoff_result;
int raya_reaper_sync_init(raya_reaper_sync *sync);
int raya_reaper_sync_admit(raya_reaper_sync *sync);
int raya_reaper_sync_finish(raya_reaper_sync *sync);
/* NOT_TRANSFERRED is the only result that leaves ownership with the caller. */
raya_reaper_handoff_result raya_reaper_sync_handoff_ex(
    raya_reaper_sync *sync, raya_reaper_op *op);
/* Compatibility wrapper: TRUE means ownership transferred, even if wake failed. */
int raya_reaper_sync_handoff(raya_reaper_sync *sync, raya_reaper_op *op);
raya_reaper_take_result raya_reaper_sync_take(raya_reaper_sync *sync,
                                               DWORD wait_ms,
                                               raya_reaper_op **out_op);
int raya_reaper_sync_terminal(raya_reaper_sync *sync, raya_reaper_op *op);
int raya_reaper_sync_shutdown(raya_reaper_sync *sync);
int raya_reaper_sync_mark_stopped(raya_reaper_sync *sync);
int raya_reaper_sync_destroy(raya_reaper_sync *sync);
#endif
