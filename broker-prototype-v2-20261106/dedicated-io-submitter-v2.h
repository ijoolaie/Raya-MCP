#ifndef RAYA_DEDICATED_IO_SUBMITTER_V2_H
#define RAYA_DEDICATED_IO_SUBMITTER_V2_H
#include <windows.h>
#include <stddef.h>

typedef struct {
 HANDLE (WINAPI *create_event)(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR);
 HANDLE (WINAPI *create_thread)(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD);
 BOOL (WINAPI *set_event)(HANDLE);
 BOOL (WINAPI *reset_event)(HANDLE);
 DWORD (WINAPI *wait_for_single_object)(HANDLE, DWORD);
 BOOL (WINAPI *get_exit_code_thread)(HANDLE, LPDWORD);
 BOOL (WINAPI *close_handle)(HANDLE);
} raya_submitter_win32_api;

typedef enum {
 RAYA_SUBMITTER_RUNNING=1, RAYA_SUBMITTER_STOPPING=2,
 RAYA_SUBMITTER_STOPPED=3, RAYA_SUBMITTER_DEGRADED=4
} raya_submitter_lifecycle;
/* Provisional first-failure diagnostics; NONE means no DEGRADED transition recorded. */
typedef enum {
 RAYA_SUBMITTER_DEGRADED_REASON_NONE=0,
 RAYA_SUBMITTER_DEGRADED_REASON_UNCLASSIFIED=1,
 RAYA_SUBMITTER_DEGRADED_REASON_STARTUP_ROLLBACK=2,
 RAYA_SUBMITTER_DEGRADED_REASON_DESTROY_CLOSE=3,
 RAYA_SUBMITTER_DEGRADED_REASON_WAKE_RESET=4,
 RAYA_SUBMITTER_DEGRADED_REASON_WAKE_WAIT=5,
 RAYA_SUBMITTER_DEGRADED_REASON_QUEUE_INVARIANT=6,
 RAYA_SUBMITTER_DEGRADED_REASON_ISSUE_UNCERTAIN=7,
 RAYA_SUBMITTER_DEGRADED_REASON_PUBLISH_UNCERTAIN=8,
 RAYA_SUBMITTER_DEGRADED_REASON_NOTIFICATION=9,
 RAYA_SUBMITTER_DEGRADED_REASON_ACK_INVARIANT=10,
 RAYA_SUBMITTER_DEGRADED_REASON_CHANGED_RESET=11,
 RAYA_SUBMITTER_DEGRADED_REASON_CHANGED_WAIT=12
} raya_submitter_degraded_reason;
typedef enum {
 RAYA_SUBMITTER_REQ_BUILDING=0, RAYA_SUBMITTER_REQ_ADMITTED=1,
 RAYA_SUBMITTER_REQ_QUEUED=2, RAYA_SUBMITTER_REQ_ISSUING=3,
 RAYA_SUBMITTER_REQ_ISSUED=4, RAYA_SUBMITTER_REQ_ACKED=5,
 RAYA_SUBMITTER_REQ_NOT_ISSUED=6, RAYA_SUBMITTER_REQ_UNCERTAIN=7
} raya_submitter_request_state;
typedef enum {
 RAYA_SUBMITTER_ISSUE_NOT_ACCEPTED=0,
 RAYA_SUBMITTER_ISSUE_ACCEPTED=1,
 RAYA_SUBMITTER_ISSUE_UNCERTAIN=2
} raya_submitter_issue_result;
typedef enum {
 RAYA_SUBMITTER_OK=1, RAYA_SUBMITTER_BACKPRESSURE=2,
 RAYA_SUBMITTER_REJECTED=3, RAYA_SUBMITTER_RETAINED=4
} raya_submitter_result;

typedef enum {
 RAYA_SUBMITTER_EFFECT_NOT_ATTEMPTED=0,
 RAYA_SUBMITTER_EFFECT_IN_PROGRESS=1,
 RAYA_SUBMITTER_EFFECT_COMMITTED=2,
 RAYA_SUBMITTER_EFFECT_UNKNOWN=3
} raya_submitter_request_effect_state;
typedef struct raya_submitter_request {
 struct raya_submitter_request *next;
 void *item;
 raya_submitter_request_state state;
 /* Submitter-owned after admission. Callbacks must not modify these fields. */
 ULONGLONG request_id; /* Monotonic within one Submitter instance; zero is invalid. */
 raya_submitter_request_effect_state issue_effect;
 raya_submitter_request_effect_state publish_effect;
} raya_submitter_request;

typedef raya_submitter_issue_result (*raya_submitter_issue_fn)(
 raya_submitter_request *request, void *context);
typedef int (*raya_submitter_publish_fn)(
 raya_submitter_request *request, void *context);
typedef void (*raya_submitter_not_issued_fn)(
 raya_submitter_request *request, void *context);

typedef enum {
 RAYA_SUBMITTER_OP_SUCCESS=1, RAYA_SUBMITTER_OP_ALREADY_DONE=2,
 RAYA_SUBMITTER_OP_RETRYABLE_FAILURE=3, RAYA_SUBMITTER_OP_INVALID_STATE=4,
 RAYA_SUBMITTER_OP_TERMINAL_FAILURE=5
} raya_submitter_op_status;
typedef enum {
 RAYA_SUBMITTER_EFFECT_NONE=0, RAYA_SUBMITTER_EFFECT_SIGNALLED=1,
 RAYA_SUBMITTER_EFFECT_THREAD_JOINED=2, RAYA_SUBMITTER_EFFECT_HANDLE_CLOSED=3,
 RAYA_SUBMITTER_EFFECT_PARTIAL_DESTROY=4, RAYA_SUBMITTER_EFFECT_DESTROYED=5
} raya_submitter_effect;
typedef enum {
 RAYA_SUBMITTER_RETRY_NONE=0, RAYA_SUBMITTER_RETRY_SAME_OPERATION=1,
 RAYA_SUBMITTER_RETRY_RECONCILE=2
} raya_submitter_retry_class;
typedef struct {
 DWORD struct_size;
 raya_submitter_op_status status;
 raya_submitter_effect effect;
 raya_submitter_retry_class retry;
 DWORD step;
 DWORD win32_error;
 DWORD thread_exit_code;
 BOOL wake_event_present, changed_event_present, thread_present;
} raya_submitter_op_result;

typedef struct {
 CRITICAL_SECTION lock;
 raya_submitter_win32_api api;
 HANDLE wake_event;
 HANDLE changed_event;
 HANDLE thread;
 raya_submitter_request *head, *tail;
 size_t queue_count, queue_capacity;
 size_t active_producers, active_issue, issuer_leases;
 raya_submitter_lifecycle lifecycle;
 raya_submitter_issue_fn issue;
 raya_submitter_publish_fn publish;
 raya_submitter_not_issued_fn not_issued;
 void *callback_context;
 DWORD thread_exit_code;
 ULONGLONG next_request_id; /* Zero means the ID space is exhausted. */
 BOOL initialized, started, joined;
 /* High bit closes public operational API admission; low 31 bits count entrants. */
 volatile LONG api_gate;
 /* 0=not started, 1=wake_event closed; caller serializes teardown. */
 DWORD destroy_progress;
 /* First DEGRADED transition only; diagnostic evidence, not recovery authorization. */
 raya_submitter_degraded_reason degraded_reason;
 raya_submitter_lifecycle degraded_from_lifecycle;
 DWORD degraded_first_error;
 size_t degraded_queue_count, degraded_active_producers;
 size_t degraded_active_issue, degraded_issuer_leases;
 ULONGLONG degraded_request_id;
 raya_submitter_request_effect_state degraded_issue_effect;
 raya_submitter_request_effect_state degraded_publish_effect;
} raya_dedicated_submitter;

/* Start requires exclusive, unpublished storage; no concurrent API caller. */
int raya_submitter_start_with_api(raya_dedicated_submitter *s, size_t capacity,
 const raya_submitter_win32_api *api, raya_submitter_issue_fn issue,
 raya_submitter_publish_fn publish, raya_submitter_not_issued_fn not_issued,
 void *context);
int raya_submitter_start(raya_dedicated_submitter *s, size_t capacity,
 raya_submitter_issue_fn issue, raya_submitter_publish_fn publish,
 raya_submitter_not_issued_fn not_issued, void *context);
int raya_submitter_admit(raya_dedicated_submitter *s,
 raya_submitter_request *request, void *item);
raya_submitter_result raya_submitter_enqueue(raya_dedicated_submitter *s,
 raya_submitter_request *request);
int raya_submitter_abort_admission(raya_dedicated_submitter *s,
 raya_submitter_request *request);
/* Returns 0 if ACK state is terminal but lifecycle signaling fails; caller must retain request storage. */
int raya_submitter_acknowledge(raya_dedicated_submitter *s,
 raya_submitter_request *request);
/* Lifecycle calls bypass the operational gate and require one serialized cleanup
   owner after admission close + zero entrants. Legacy direct calls remain for
   compatibility but are not safe to race with other lifecycle calls. */
int raya_submitter_shutdown(raya_dedicated_submitter *s);
void raya_submitter_result_init(raya_submitter_op_result *result);
raya_submitter_op_status raya_submitter_shutdown_ex(raya_dedicated_submitter *s,
 raya_submitter_op_result *result);
/* Join timeout/WAIT_FAILED preserves the thread HANDLE and all object state; retry is allowed. */
int raya_submitter_join(raya_dedicated_submitter *s, DWORD timeout_ms);
raya_submitter_op_status raya_submitter_join_ex(raya_dedicated_submitter *s,
 DWORD timeout_ms, raya_submitter_op_result *result);
int raya_submitter_destroy(raya_dedicated_submitter *s);
/* Caller must quiesce all Submitter callers and serialize teardown/retries. */
raya_submitter_op_status raya_submitter_destroy_ex(raya_dedicated_submitter *s,
 raya_submitter_op_result *result);
/* Snapshot is diagnostic only; it never grants ownership or permits freeing. */
/* Closes admission to admit/enqueue/abort/acknowledge/snapshot. Returns 1 only
   when closed AND drained; returns 0 with active_count when callers remain.
   Idempotent. Caller must prevent new raw-pointer invocations after reclamation. */
int raya_submitter_admission_close(raya_dedicated_submitter *s, LONG *active_count);
int raya_submitter_snapshot(raya_dedicated_submitter *s,
 raya_submitter_lifecycle *lifecycle, size_t *queued,
 size_t *active_producers, size_t *active_issue, size_t *issuer_leases);
#endif
