#ifndef RAYA_IO_LIFECYCLE_MODEL_H
#define RAYA_IO_LIFECYCLE_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum raya_io_state {
    RAYA_IO_CREATED = 0,
    RAYA_IO_PENDING,
    RAYA_IO_CANCEL_REQUESTED,
    RAYA_IO_COMPLETED,
    RAYA_IO_REAPED
} raya_io_state;

typedef enum raya_io_result {
    RAYA_IO_RESULT_SUCCESS = 0,
    RAYA_IO_RESULT_CANCELLED,
    RAYA_IO_RESULT_FAILED
} raya_io_result;

typedef struct raya_io_operation {
    raya_io_state state;
    raya_io_result result;
    uint32_t transferred;
    bool resources_owned;
    bool completion_observed;
    bool registered;
    unsigned cleanup_count;
} raya_io_operation;

typedef struct raya_io_registry {
    size_t active_count;
    size_t maximum_active;
    bool accepting;
} raya_io_registry;

void raya_io_operation_init(raya_io_operation *operation);
bool raya_io_mark_pending(raya_io_operation *operation);
bool raya_io_request_cancel(raya_io_operation *operation);
bool raya_io_observe_completion(
    raya_io_operation *operation,
    raya_io_result result,
    uint32_t transferred);
bool raya_io_reap(raya_io_operation *operation);

void raya_io_registry_init(raya_io_registry *registry, size_t maximum_active);
bool raya_io_registry_admit(
    raya_io_registry *registry,
    raya_io_operation *operation);
void raya_io_registry_stop_admission(raya_io_registry *registry);
bool raya_io_registry_reap(
    raya_io_registry *registry,
    raya_io_operation *operation);

#endif
