#ifndef RAYA_REAPER_MANAGED_WORKER_V2_H
#define RAYA_REAPER_MANAGED_WORKER_V2_H
#include <windows.h>
#include "reaper-integrated-v2.h"
#include "pipe-io-owner-v2.h"
#include "pipe-io-finalizer-v2.h"
#include "reaper-waitplan-v2.h"

typedef enum { RAYA_MANAGED_HANDOFF_NOT_TRANSFERRED=0, RAYA_MANAGED_HANDOFF_TRANSFERRED=1, RAYA_MANAGED_HANDOFF_TRANSFERRED_DEGRADED=2 } raya_managed_handoff_result;
typedef struct raya_managed_worker raya_managed_worker;
typedef struct raya_managed_operation raya_managed_operation;
/* Called exactly once, outside the worker lock, after all ownership commits.
   It may free the operation; the worker will not touch it afterward. */
typedef void (*raya_managed_release_fn)(raya_managed_operation *op, void *context);
typedef HANDLE (WINAPI *raya_managed_thread_factory_fn)(
    LPTHREAD_START_ROUTINE entry, LPVOID parameter, void *context);
typedef BOOL (WINAPI *raya_managed_get_overlapped_result_fn)(
    HANDLE file, LPOVERLAPPED overlapped, LPDWORD transferred,
    BOOL wait, void *context);
#ifdef RAYA_ADAPTER_TESTING
typedef BOOL (WINAPI *raya_managed_set_event_fn)(HANDLE event_handle, void *context);
typedef DWORD (WINAPI *raya_managed_wait_fn)(HANDLE handle, DWORD timeout_ms, void *context);
typedef BOOL (*raya_managed_wait_failure_test_fn)(DWORD *error, void *context);
typedef BOOL (WINAPI *raya_managed_exit_code_fn)(HANDLE thread, LPDWORD exit_code, void *context);
typedef BOOL (WINAPI *raya_managed_lifecycle_close_fn)(HANDLE handle, void *context);
typedef int (*raya_managed_handoff_degraded_fn)(raya_managed_operation *op, void *context);
typedef void (*raya_managed_before_reserve_test_fn)(raya_managed_worker *worker, raya_managed_operation *op, void *context);
#endif

typedef enum {
    RAYA_WORKER_INVENTORY_UNSPECIFIED=0,
    RAYA_WORKER_INVENTORY_DIRECT=1,
    RAYA_WORKER_INVENTORY_ADAPTER_ASSOCIATED=2
} raya_worker_inventory_kind;

typedef enum {
    RAYA_WORKER_RETAIN_STAGE_UNKNOWN=0,
    RAYA_WORKER_RETAIN_STAGE_DIRECT=1,
    RAYA_WORKER_RETAIN_STAGE_ADAPTER_ASSOCIATED=2,
    RAYA_WORKER_RETAIN_STAGE_PARTIAL_RECORD=3,
    RAYA_WORKER_RETAIN_STAGE_POST_TERMINAL=4
} raya_worker_retain_stage;

typedef enum {
    RAYA_RETAINED_INVENTORY_OK = 0,
    RAYA_RETAINED_INVENTORY_INVALID_ARGUMENT = 1,
    RAYA_RETAINED_INVENTORY_NOT_INITIALIZED = 2,
    RAYA_RETAINED_INVENTORY_NOT_QUIESCENT = 3,
    RAYA_RETAINED_INVENTORY_INSUFFICIENT_CAPACITY = 4,
    RAYA_RETAINED_INVENTORY_INCONSISTENT = 5,
    RAYA_RETAINED_INVENTORY_UNSUPPORTED_VERSION = 6,
    RAYA_RETAINED_INVENTORY_INTERNAL_ERROR = 7
} raya_retained_inventory_status;

#define RAYA_WORKER_RETAINED_RECORD_VERSION_1 1u
typedef struct {
    DWORD struct_size;
    DWORD version;
    BOOL identity_valid;
    raya_worker_inventory_kind inventory_kind;
    ULONGLONG worker_epoch;
    ULONGLONG worker_operation_id;
    BOOL adapter_identity_valid;
    ULONGLONG adapter_instance_id;
    ULONGLONG adapter_operation_id;
    DWORD first_retain_cause;
    raya_worker_retain_stage first_retain_stage;
} raya_worker_retained_record_v1;

struct raya_managed_operation {
    raya_integrated_op integrated; /* Must remain first: registry stores this address. */
    BOOL operation_initialized;
    BOOL identity_valid;
    ULONGLONG worker_epoch;
    ULONGLONG worker_operation_id;
    raya_worker_inventory_kind inventory_kind;
    BOOL adapter_identity_valid;
    ULONGLONG adapter_instance_id;
    ULONGLONG adapter_operation_id;
    DWORD first_retain_cause;
    raya_worker_retain_stage first_retain_stage;
    OVERLAPPED overlapped;
    HANDLE issuing_pipe;            /* Owned duplicate used for the original I/O. */
    void *buffer;                   /* Caller-owned storage retained through collection. */
    DWORD buffer_length;
    DWORD transferred;
    DWORD completion_error;
    DWORD incomplete_retries;
    BOOL read_operation;
    struct raya_managed_operation *retained_next;
    volatile LONG incomplete_observations;
    raya_pipe_owner owner;
    raya_pipe_finalizer finalizer;
    raya_managed_release_fn release;
    void *release_context;
};

struct raya_managed_worker {
    CRITICAL_SECTION lock;
    HANDLE wake_event;
    HANDLE thread;
    raya_reaper_integrated integrated;
    raya_managed_operation *retained_head;
    size_t retained_count;
    ULONGLONG worker_epoch;
    ULONGLONG next_operation_id;
    DWORD thread_exit_code;
    raya_close_handle_fn close_handle;
    void *close_context;
    raya_managed_get_overlapped_result_fn get_overlapped_result;
    void *completion_context;
#ifdef RAYA_ADAPTER_TESTING
    raya_managed_set_event_fn set_wake_event;
    void *wake_event_context;
    raya_managed_wait_failure_test_fn wait_failure_test;
    void *wait_failure_test_context;
    raya_managed_wait_fn lifecycle_wait;
    raya_managed_exit_code_fn lifecycle_exit_code;
    raya_managed_lifecycle_close_fn lifecycle_close;
    void *lifecycle_api_context;
    raya_managed_before_reserve_test_fn before_reserve_test;
    void *before_reserve_test_context;
#endif
    BOOL initialized;
    BOOL started;
    BOOL joined;
};

int raya_managed_operation_init(raya_managed_operation *op,
    HANDLE owned_issuing_pipe, HANDLE owned_completion_event,
    void *buffer, DWORD buffer_length,
    raya_managed_release_fn release, void *release_context);
int raya_managed_worker_start(raya_managed_worker *worker);
/* Completion API seam is copied before the worker thread starts. */
int raya_managed_worker_start_with_completion_api(
    raya_managed_worker *worker,
    raya_managed_get_overlapped_result_fn get_result, void *context);
/* Test seam for deterministic worker-thread creation failure injection. */
int raya_managed_worker_start_with_factory(raya_managed_worker *worker,
    raya_managed_thread_factory_fn factory, void *context);
int raya_managed_worker_set_close_callback(raya_managed_worker *worker,
    raya_close_handle_fn close_fn, void *context);
#ifdef RAYA_ADAPTER_TESTING
/* Test-only: configure wake signaling before any work is admitted. */
int raya_managed_worker_set_wake_callback(raya_managed_worker *worker,
    raya_managed_set_event_fn set_event, void *context);
/* Test-only: inject only WAIT_FAILED at the real Managed Worker wait boundary. */
int raya_managed_worker_start_with_wait_failure_test_hook(raya_managed_worker *worker,
    raya_managed_wait_failure_test_fn wait_failure_test, void *context);
/* Test-only lifecycle API seam. Configure after start, before shutdown/join. */
int raya_managed_worker_set_lifecycle_api(raya_managed_worker *worker,
    raya_managed_wait_fn wait_fn, raya_managed_exit_code_fn exit_code_fn,
    raya_managed_lifecycle_close_fn close_fn, void *context);
int raya_managed_worker_set_before_reserve_test_hook(raya_managed_worker *worker,
    raya_managed_before_reserve_test_fn hook, void *context);
/* Test-only: publish Adapter quarantine while Worker lock still protects handoff. */
raya_managed_handoff_result raya_managed_worker_handoff_with_hook(
    raya_managed_worker *worker, raya_managed_operation *op,
    raya_managed_handoff_degraded_fn on_degraded, void *context);
#endif
/* Assigns a process-local Worker identity after successful operation init. */
int raya_managed_worker_register_operation(raya_managed_worker *worker,
    raya_managed_operation *operation, raya_worker_inventory_kind kind,
    ULONGLONG adapter_instance_id, ULONGLONG adapter_operation_id);
/* Caller holds Worker lock. Publishes once; never allocates, frees, or calls out. */
int raya_managed_worker_publish_retained_locked(raya_managed_worker *worker,
    raya_managed_operation *operation, DWORD cause,
    raya_worker_retain_stage stage);
raya_retained_inventory_status raya_managed_worker_copy_retained_inventory_v1(
    raya_managed_worker *worker, DWORD requested_version,
    raya_worker_retained_record_v1 *records, size_t capacity,
    size_t *required_count);
int raya_managed_worker_admit(raya_managed_worker *worker);
int raya_managed_worker_finish(raya_managed_worker *worker);
int raya_managed_worker_reserve(raya_managed_worker *worker,
    raya_managed_operation *op);
raya_managed_handoff_result raya_managed_worker_handoff(
    raya_managed_worker *worker, raya_managed_operation *op);
typedef enum {
    RAYA_WORKER_OP_SUCCESS=1, RAYA_WORKER_OP_ALREADY_DONE=2,
    RAYA_WORKER_OP_RETRYABLE_FAILURE=3, RAYA_WORKER_OP_INVALID_STATE=4,
    RAYA_WORKER_OP_TERMINAL_FAILURE=5
} raya_worker_op_status;
typedef enum {
    RAYA_WORKER_EFFECT_NONE=0, RAYA_WORKER_EFFECT_STOPPING=1,
    RAYA_WORKER_EFFECT_SHUTDOWN_SIGNALLED=2,
    RAYA_WORKER_EFFECT_THREAD_REAPED=3, RAYA_WORKER_EFFECT_DESTROYED=4
} raya_worker_effect;
typedef enum {
    RAYA_WORKER_RETRY_NONE=0, RAYA_WORKER_RETRY_SAME_OPERATION=1,
    RAYA_WORKER_RETRY_RECONCILE=2
} raya_worker_retry_class;
typedef struct {
    DWORD struct_size;
    raya_worker_op_status status;
    raya_worker_effect effect;
    raya_worker_retry_class retry;
    DWORD step;
    DWORD win32_error;
    DWORD thread_exit_code;
    BOOL wake_event_present, thread_present;
} raya_worker_op_result;
void raya_worker_result_init(raya_worker_op_result *result);
raya_worker_op_status raya_managed_worker_shutdown_ex(
    raya_managed_worker *worker, raya_worker_op_result *result);
/* Caller serializes Join/Destroy. Failed wait/query/close preserves thread HANDLE. */
raya_worker_op_status raya_managed_worker_join_ex(
    raya_managed_worker *worker, DWORD timeout_ms, raya_worker_op_result *result);
raya_worker_op_status raya_managed_worker_destroy_ex(
    raya_managed_worker *worker, raya_worker_op_result *result);
/* Boolean compatibility wrappers. */
int raya_managed_worker_shutdown(raya_managed_worker *worker);
int raya_managed_worker_join(raya_managed_worker *worker, DWORD timeout_ms);
int raya_managed_worker_destroy(raya_managed_worker *worker);
#endif
