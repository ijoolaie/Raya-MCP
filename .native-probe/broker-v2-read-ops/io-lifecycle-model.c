#include "io-lifecycle-model.h"

void raya_io_operation_init(raya_io_operation *operation)
{
    if (operation == NULL)
        return;

    operation->state = RAYA_IO_CREATED;
    operation->result = RAYA_IO_RESULT_FAILED;
    operation->transferred = 0;
    operation->resources_owned = true;
    operation->completion_observed = false;
    operation->registered = false;
    operation->cleanup_count = 0;
}

bool raya_io_mark_pending(raya_io_operation *operation)
{
    if (operation == NULL || operation->state != RAYA_IO_CREATED ||
        !operation->resources_owned)
        return false;

    operation->state = RAYA_IO_PENDING;
    return true;
}

bool raya_io_request_cancel(raya_io_operation *operation)
{
    if (operation == NULL || !operation->resources_owned ||
        operation->completion_observed)
        return false;

    if (operation->state == RAYA_IO_CANCEL_REQUESTED)
        return true;

    if (operation->state != RAYA_IO_PENDING)
        return false;

    operation->state = RAYA_IO_CANCEL_REQUESTED;
    return true;
}

bool raya_io_observe_completion(
    raya_io_operation *operation,
    raya_io_result result,
    uint32_t transferred)
{
    if (operation == NULL || !operation->resources_owned ||
        operation->completion_observed)
        return false;

    if (operation->state != RAYA_IO_CREATED &&
        operation->state != RAYA_IO_PENDING &&
        operation->state != RAYA_IO_CANCEL_REQUESTED)
        return false;

    operation->result = result;
    operation->transferred = transferred;
    operation->completion_observed = true;
    operation->state = RAYA_IO_COMPLETED;
    return true;
}

bool raya_io_reap(raya_io_operation *operation)
{
    if (operation == NULL || operation->state != RAYA_IO_COMPLETED ||
        !operation->resources_owned || !operation->completion_observed ||
        operation->cleanup_count != 0)
        return false;

    operation->resources_owned = false;
    operation->cleanup_count = 1;
    operation->state = RAYA_IO_REAPED;
    return true;
}

void raya_io_registry_init(raya_io_registry *registry, size_t maximum_active)
{
    if (registry == NULL)
        return;

    registry->active_count = 0;
    registry->maximum_active = maximum_active;
    registry->accepting = maximum_active != 0;
}

bool raya_io_registry_admit(
    raya_io_registry *registry,
    raya_io_operation *operation)
{
    if (registry == NULL || operation == NULL || !registry->accepting ||
        registry->active_count >= registry->maximum_active)
        return false;

    raya_io_operation_init(operation);
    operation->registered = true;
    registry->active_count++;
    return true;
}

void raya_io_registry_stop_admission(raya_io_registry *registry)
{
    if (registry != NULL)
        registry->accepting = false;
}

bool raya_io_registry_reap(
    raya_io_registry *registry,
    raya_io_operation *operation)
{
    if (registry == NULL || operation == NULL ||
        registry->active_count == 0 || !operation->registered ||
        !raya_io_reap(operation))
        return false;

    operation->registered = false;
    registry->active_count--;
    return true;
}
