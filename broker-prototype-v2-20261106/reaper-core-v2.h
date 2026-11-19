#ifndef RAYA_REAPER_CORE_V2_H
#define RAYA_REAPER_CORE_V2_H
#include <stddef.h>
#include <stdint.h>
#include "reaper-recovery-policy-v2.h"

typedef enum {
    RAYA_REAPER_UNINITIALIZED=0,
    RAYA_REAPER_ACCEPTING=1,
    RAYA_REAPER_STOPPING=2,
    RAYA_REAPER_FAILED=3,
    RAYA_REAPER_STOPPED=4,
    RAYA_REAPER_DEGRADED=5
} raya_reaper_lifecycle;

typedef enum {
    RAYA_REAPER_OP_CALLER_PENDING=1,
    RAYA_REAPER_OP_QUEUED=2,
    RAYA_REAPER_OP_WORKER_PENDING=3,
    RAYA_REAPER_OP_WORKER_TERMINAL=4,
    RAYA_REAPER_OP_RELEASED=5,
    RAYA_REAPER_OP_TERMINAL_HELD=6,
    RAYA_REAPER_OP_CALLER_TERMINAL=7
} raya_reaper_op_state;

typedef struct raya_reaper_op {
    struct raya_reaper_op *next;
    raya_reaper_op_state state;
} raya_reaper_op;

typedef struct {
    raya_reaper_lifecycle lifecycle;
    raya_reaper_op *head;
    raya_reaper_op *tail;
    size_t caller_active;
    size_t outstanding;
    size_t queued;
    uint32_t degradation_causes;
} raya_reaper_core;

/*
 * All functions are lock-held primitives. The caller must serialize every
 * access to the core and operation nodes with one external lock.
 * No function allocates, frees, waits, calls Win32, or proves I/O completion.
 */
int raya_reaper_core_start(raya_reaper_core *core);
int raya_reaper_admit(raya_reaper_core *core);
int raya_reaper_caller_finish(raya_reaper_core *core);
int raya_reaper_caller_terminal(raya_reaper_core *core, raya_reaper_op *op);
int raya_reaper_handoff(raya_reaper_core *core, raya_reaper_op *op);
raya_reaper_op *raya_reaper_dequeue(raya_reaper_core *core);
int raya_reaper_worker_terminal(raya_reaper_core *core, raya_reaper_op *op);
/* Two-phase path: terminal collection holds outstanding ownership until resource close commits. */
int raya_reaper_worker_terminal_hold(raya_reaper_core *core, raya_reaper_op *op);
int raya_reaper_worker_release(raya_reaper_core *core, raya_reaper_op *op);
int raya_reaper_begin_shutdown(raya_reaper_core *core);
int raya_reaper_can_exit(const raya_reaper_core *core);
int raya_reaper_mark_stopped(raya_reaper_core *core);
int raya_reaper_mark_degraded(raya_reaper_core *core);
int raya_reaper_mark_degraded_cause(raya_reaper_core *core, uint32_t cause);
#endif
