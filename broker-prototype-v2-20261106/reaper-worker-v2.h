#ifndef RAYA_REAPER_WORKER_V2_H
#define RAYA_REAPER_WORKER_V2_H
#include "reaper-sync-v2.h"
typedef int (*raya_reaper_collect_fn)(raya_reaper_op *op, void *context);
typedef struct {
    raya_reaper_sync sync;
    HANDLE thread;
    raya_reaper_collect_fn collect;
    void *context;
    DWORD worker_exit_code;
    BOOL started;
    BOOL joined;
} raya_reaper_worker;
int raya_reaper_worker_start(raya_reaper_worker *worker,
                             raya_reaper_collect_fn collect, void *context);
int raya_reaper_worker_shutdown(raya_reaper_worker *worker, DWORD join_timeout_ms);
int raya_reaper_worker_destroy(raya_reaper_worker *worker);
int raya_reaper_worker_admit(raya_reaper_worker *worker);
int raya_reaper_worker_finish(raya_reaper_worker *worker);
raya_reaper_handoff_result raya_reaper_worker_handoff_ex(
    raya_reaper_worker *worker, raya_reaper_op *op);
/* Compatibility wrapper: TRUE means ownership transferred. */
int raya_reaper_worker_handoff(raya_reaper_worker *worker, raya_reaper_op *op);
#endif
