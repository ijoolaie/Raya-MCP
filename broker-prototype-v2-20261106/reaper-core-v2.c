#include "reaper-core-v2.h"
#include <string.h>

int raya_reaper_core_start(raya_reaper_core *core) {
    if (core == NULL || core->lifecycle != RAYA_REAPER_UNINITIALIZED)
        return 0;
    memset(core, 0, sizeof(*core));
    core->lifecycle = RAYA_REAPER_ACCEPTING;
    return 1;
}
int raya_reaper_admit(raya_reaper_core *core) {
    if (core == NULL || core->lifecycle != RAYA_REAPER_ACCEPTING ||
        core->caller_active == SIZE_MAX) return 0;
    ++core->caller_active;
    return 1;
}
int raya_reaper_caller_finish(raya_reaper_core *core) {
    if (core == NULL || core->caller_active == 0) return 0;
    --core->caller_active;
    return 1;
}
int raya_reaper_caller_terminal(raya_reaper_core *core, raya_reaper_op *op) {
    if (core == NULL || op == NULL || core->caller_active == 0 ||
        op->state != RAYA_REAPER_OP_CALLER_PENDING || op->next != NULL)
        return 0;
    op->state = RAYA_REAPER_OP_CALLER_TERMINAL;
    return 1;
}
int raya_reaper_handoff(raya_reaper_core *core, raya_reaper_op *op) {
    if (core == NULL || op == NULL ||
        core->lifecycle != RAYA_REAPER_ACCEPTING &&
        core->lifecycle != RAYA_REAPER_STOPPING ||
        core->caller_active == 0 || core->outstanding == SIZE_MAX ||
        op->state != RAYA_REAPER_OP_CALLER_PENDING || op->next != NULL)
        return 0;
    op->state = RAYA_REAPER_OP_QUEUED;
    if (core->tail != NULL) core->tail->next = op;
    else core->head = op;
    core->tail = op;
    ++core->queued;
    ++core->outstanding;
    --core->caller_active;
    return 1;
}
raya_reaper_op *raya_reaper_dequeue(raya_reaper_core *core) {
    raya_reaper_op *op;
    if (core == NULL || core->head == NULL) return NULL;
    op = core->head;
    if (op->state != RAYA_REAPER_OP_QUEUED || core->queued == 0) {
        core->degradation_causes |= RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT;
        core->lifecycle = RAYA_REAPER_DEGRADED;
        return NULL;
    }
    core->head = op->next;
    if (core->head == NULL) core->tail = NULL;
    op->next = NULL;
    op->state = RAYA_REAPER_OP_WORKER_PENDING;
    --core->queued;
    return op;
}
int raya_reaper_worker_terminal(raya_reaper_core *core, raya_reaper_op *op) {
    if (core == NULL || op == NULL || core->outstanding == 0 ||
        op->state != RAYA_REAPER_OP_WORKER_PENDING || op->next != NULL)
        return 0;
    op->state = RAYA_REAPER_OP_WORKER_TERMINAL;
    --core->outstanding;
    op->state = RAYA_REAPER_OP_RELEASED;
    return 1;
}
int raya_reaper_worker_terminal_hold(raya_reaper_core *core, raya_reaper_op *op) {
    if (core == NULL || op == NULL || core->outstanding == 0 ||
        op->state != RAYA_REAPER_OP_WORKER_PENDING || op->next != NULL)
        return 0;
    op->state = RAYA_REAPER_OP_TERMINAL_HELD;
    return 1;
}
int raya_reaper_worker_release(raya_reaper_core *core, raya_reaper_op *op) {
    if (core == NULL || op == NULL || core->outstanding == 0 ||
        op->state != RAYA_REAPER_OP_TERMINAL_HELD || op->next != NULL)
        return 0;
    --core->outstanding;
    op->state = RAYA_REAPER_OP_RELEASED;
    return 1;
}
int raya_reaper_begin_shutdown(raya_reaper_core *core) {
    if (core == NULL || core->lifecycle != RAYA_REAPER_ACCEPTING)
        return 0;
    core->lifecycle = RAYA_REAPER_STOPPING;
    return 1;
}
int raya_reaper_can_exit(const raya_reaper_core *core) {
    return core != NULL && core->lifecycle == RAYA_REAPER_STOPPING &&
           core->caller_active == 0 && core->queued == 0 &&
           core->head == NULL && core->tail == NULL &&
           core->outstanding == 0;
}
int raya_reaper_mark_stopped(raya_reaper_core *core) {
    if (!raya_reaper_can_exit(core)) return 0;
    core->lifecycle = RAYA_REAPER_STOPPED;
    return 1;
}
int raya_reaper_mark_degraded_cause(raya_reaper_core *core, uint32_t cause) {
    if (core == NULL || core->lifecycle == RAYA_REAPER_STOPPED ||
        core->lifecycle == RAYA_REAPER_UNINITIALIZED || cause == 0 ||
        (cause & ~(uint32_t)(RAYA_RECOVERY_CAUSE_WAKE_SIGNAL |
          RAYA_RECOVERY_CAUSE_WAIT_FAILED | RAYA_RECOVERY_CAUSE_COMPLETION_UNCERTAIN |
          RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT | RAYA_RECOVERY_CAUSE_HANDLE_CLOSE |
          RAYA_RECOVERY_CAUSE_WAITSET_INVALID)) != 0) return 0;
    core->degradation_causes |= cause;
    core->lifecycle = RAYA_REAPER_DEGRADED;
    return 1;
}
int raya_reaper_mark_degraded(raya_reaper_core *core) {
    return raya_reaper_mark_degraded_cause(core, RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
}
