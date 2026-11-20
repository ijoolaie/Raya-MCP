#ifndef RAYA_INTEGRATED_SUBMIT_ADAPTER_V2_H
#define RAYA_INTEGRATED_SUBMIT_ADAPTER_V2_H
#include "dedicated-io-submitter-v2.h"
#include "managed-pipe-io-v2.h"

typedef enum {
 RAYA_ADAPTER_REJECTED=0,
 RAYA_ADAPTER_QUEUED=1,
 RAYA_ADAPTER_RETAINED=2
} raya_adapter_result;

typedef enum {
 RAYA_ADAPTER_DESTROY_SUCCESS=1,
 RAYA_ADAPTER_DESTROY_BUSY=2,
 RAYA_ADAPTER_DESTROY_INVALID=3
} raya_adapter_destroy_status;
typedef enum {
 RAYA_ADAPTER_DESTROY_EFFECT_NONE=0,
 RAYA_ADAPTER_DESTROY_EFFECT_ADMISSION_CLOSED=1,
 RAYA_ADAPTER_DESTROY_EFFECT_DESTROYED=2
} raya_adapter_destroy_effect;
typedef enum {
 RAYA_ADAPTER_DESTROY_RETRY_NONE=0,
 RAYA_ADAPTER_DESTROY_RETRY_RECONCILE=1
} raya_adapter_destroy_retry;
typedef struct {
 DWORD struct_size;
 raya_adapter_destroy_status status;
 raya_adapter_destroy_effect effect;
 raya_adapter_destroy_retry retry;
 DWORD step;
 size_t entrants, active, retained;
 BOOL admission_closed, lock_present;
} raya_adapter_destroy_result;

typedef enum {
 RAYA_ADAPTER_RETAIN_STAGE_UNKNOWN=0,
 RAYA_ADAPTER_RETAIN_STAGE_PRE_ISSUE_SETUP=1,
 RAYA_ADAPTER_RETAIN_STAGE_SUBMITTER_ADMISSION=2,
 RAYA_ADAPTER_RETAIN_STAGE_ISSUE_OR_HANDOFF=3,
 RAYA_ADAPTER_RETAIN_STAGE_HANDLE_CLOSE_FAILURE=4,
 RAYA_ADAPTER_RETAIN_STAGE_SUBMITTER_ACK=5,
 RAYA_ADAPTER_RETAIN_STAGE_OWNERSHIP_INVARIANT=6
} raya_adapter_retain_stage;

#define RAYA_ADAPTER_RETAINED_RECORD_VERSION_1 1u
typedef struct {
 DWORD struct_size;
 DWORD version;
 ULONGLONG adapter_instance_id;
 ULONGLONG adapter_operation_id;
 DWORD first_retain_cause;
 raya_adapter_retain_stage first_retain_stage;
} raya_adapter_retained_record_v1;

typedef struct raya_adapter_envelope raya_adapter_envelope;
typedef struct {
 HANDLE (WINAPI *create_event)(LPSECURITY_ATTRIBUTES,BOOL,BOOL,LPCWSTR,void *);
 BOOL (WINAPI *duplicate_handle)(HANDLE,HANDLE,HANDLE,LPHANDLE,DWORD,BOOL,DWORD,void *);
 BOOL (WINAPI *read_file)(HANDLE,LPVOID,DWORD,LPDWORD,LPOVERLAPPED,void *);
 BOOL (WINAPI *write_file)(HANDLE,LPCVOID,DWORD,LPDWORD,LPOVERLAPPED,void *);
 DWORD (WINAPI *get_last_error)(void *);
 BOOL (WINAPI *close_handle)(HANDLE,void *);
 void *context;
} raya_integrated_adapter_api;
typedef struct {
 CRITICAL_SECTION lock;
 ULONGLONG instance_id; /* process-local, non-reused while positive */
 ULONGLONG next_operation_id; /* monotonic within this Adapter instance */
 raya_integrated_adapter_api api;
 raya_dedicated_submitter *submitter;
 raya_managed_worker *worker;
 raya_adapter_envelope *retained_head;
 size_t retained_count;
 size_t active_count;
 volatile LONG lifecycle_state; /* high bit closes admission; low bits count API entrants */
#ifdef RAYA_ADAPTER_TESTING
 BOOL test_signal_worker_wake_on_quarantine;
 raya_adapter_envelope *quarantine_head;
 size_t quarantine_count;
#endif
 BOOL initialized;
} raya_integrated_submit_adapter;

/* Worker is borrowed and must outlive Adapter. Submitter storage is supplied by
   caller but must be uninitialized: Adapter initializes it during start.
   Adapter Destroy does not stop/join/destroy Submitter; after Adapter is
   destroyed, caller must quiesce and tear Submitter down before reusing storage. */
int raya_integrated_adapter_start(raya_integrated_submit_adapter *adapter,
 raya_dedicated_submitter *submitter, raya_managed_worker *worker,
 size_t queue_capacity);
/* Test seam: every callback must be non-NULL; context is borrowed through destroy. */
int raya_integrated_adapter_start_with_api(raya_integrated_submit_adapter *adapter,
 raya_dedicated_submitter *submitter, raya_managed_worker *worker,
 size_t queue_capacity, const raya_integrated_adapter_api *api);
/* Combined test seam for adapter I/O and Submitter lifecycle signaling. */
int raya_integrated_adapter_start_with_apis(raya_integrated_submit_adapter *adapter,
 raya_dedicated_submitter *submitter, raya_managed_worker *worker,
 size_t queue_capacity, const raya_integrated_adapter_api *adapter_api,
 const raya_submitter_win32_api *submitter_api);
/* Async submit-and-complete only: no timeout/cancellation API in this version.
   Write data is copied. Read data is valid only during completion callback. */
raya_adapter_result raya_integrated_adapter_submit(
 raya_integrated_submit_adapter *adapter, HANDLE pipe,
 raya_managed_pipe_direction direction, const void *write_data, DWORD length,
 raya_managed_pipe_completion_fn callback, void *callback_context);
size_t raya_integrated_adapter_retained_count(
 raya_integrated_submit_adapter *adapter);
raya_retained_inventory_status raya_integrated_adapter_copy_retained_inventory_v1(
 raya_integrated_submit_adapter *adapter, DWORD requested_version,
 raya_adapter_retained_record_v1 *records, size_t capacity,
 size_t *required_count);
/* Closes admission. Retry after active operations drain; does not stop either component.
   Serialize destroy calls; caller must keep adapter storage alive, join/quiesce
   all possible callers, and prevent new calls after successful destroy. */
void raya_adapter_destroy_result_init(raya_adapter_destroy_result *result);
raya_adapter_destroy_status raya_integrated_adapter_destroy_ex(
 raya_integrated_submit_adapter *adapter, raya_adapter_destroy_result *result);
int raya_integrated_adapter_destroy(raya_integrated_submit_adapter *adapter);
#ifdef RAYA_ADAPTER_TESTING
#include <stdint.h>
typedef struct {
 uintptr_t envelope_id;
 uintptr_t operation_address;
 ULONGLONG adapter_instance_id;
 ULONGLONG adapter_operation_id;
 DWORD degradation_cause;
} raya_adapter_test_quarantine_item;
typedef struct {
 uintptr_t envelope_id;
 ULONGLONG adapter_instance_id;
 ULONGLONG stable_operation_id;
 DWORD first_retain_cause;
 raya_adapter_retain_stage first_retain_stage;
 uintptr_t operation_id;
 uintptr_t request_id;
 unsigned flags; /* bit0 retained, bit1 worker-owned, bit2 terminal-released, bit3 worker-admitted, bit4 worker-finished */
 unsigned owner_state, owner_collection_attempts, owner_terminal_collections, owner_release_commits;
 unsigned finalizer_state, finalizer_close_attempts, finalizer_successful_closes, finalizer_release_commits;
 unsigned operation_state, collect_state, reservation_state, registry_matches;
 unsigned terminal_collection_recorded;
 DWORD completion_error, transferred_bytes;
 unsigned worker_lifecycle, worker_reserved_slots, worker_outstanding, worker_queued;
 unsigned worker_caller_active, worker_registry_count, worker_degradation_causes;
 raya_submitter_request_effect_state issue_effect, publish_effect;
 unsigned submitter_request_state, submitter_lifecycle, submitter_issuer_leases;
 unsigned submitter_active_issue, submitter_active_producers, submitter_queue_count;
} raya_adapter_test_retained_item;
typedef struct {
 uintptr_t operation_id; /* legacy address diagnostic */
 uintptr_t operation_address;
 BOOL identity_valid;
 ULONGLONG worker_epoch, worker_operation_id;
 raya_worker_inventory_kind inventory_kind;
 BOOL adapter_identity_valid;
 ULONGLONG adapter_instance_id, adapter_operation_id;
 DWORD first_retain_cause;
 raya_worker_retain_stage first_retain_stage;
} raya_worker_test_retained_item;
/* Read-only diagnostic snapshot. Requires closed adapter admission and both
   Worker joined; Submitter has no queued/active issue/producers. Caller must quiesce direct Submitter APIs. Returns 1 on a complete snapshot, 0 if not quiescent,
   -1 for insufficient buffers or inconsistent/cyclic list metadata. */
int raya_adapter_test_retained_snapshot(raya_integrated_submit_adapter *adapter,
 raya_adapter_test_retained_item *adapter_items,size_t adapter_capacity,
 size_t *adapter_count,raya_worker_test_retained_item *worker_items,
 size_t worker_capacity,size_t *worker_count);
 int raya_adapter_test_quarantine_snapshot(raya_integrated_submit_adapter *adapter,
  raya_adapter_test_quarantine_item *items,size_t capacity,size_t *count);
#endif
#endif

