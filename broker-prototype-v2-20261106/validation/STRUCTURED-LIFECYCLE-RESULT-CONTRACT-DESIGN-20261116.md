# Structured Lifecycle Result Contract — Design Gate
Date: 2026-11-16
Scope: Working Copy design only; no implementation, ABI change, build, or runtime test in this step.

## Executive decision

Adopt a versioned output-parameter result contract for lifecycle operations. The result must describe (1) operation disposition, (2) state/effects already committed, (3) resource identity/state, and (4) retry classification. A Boolean cannot represent the observed Join and Destroy outcomes.

Keep the legacy Boolean functions temporarily as compatibility wrappers, but all future Orchestrator cleanup logic must call the structured `_ex` functions. The Orchestrator must not infer effects from a Boolean return or from lifecycle enum alone.

## Source-grounded behavior observed

### Submitter

- Shutdown changes RUNNING to STOPPING before signaling wake_event and changed_event. A signaling failure can leave the component DEGRADED; subsequent calls do not establish a defined recovery path from DEGRADED.
- Join returns false for timeout/WAIT_FAILED, GetExitCodeThread failure, and thread CloseHandle failure. Those cases have materially different effects.
- If the thread is signaled, GetExitCodeThread succeeds, and CloseHandle succeeds, Join nulls the thread handle and sets joined=TRUE before returning `code == ERROR_SUCCESS`. Thus FALSE can mean the thread was fully reaped and the exit code was abnormal.
- Destroy closes wake_event and nulls it before attempting changed_event. If the second close fails, the first close is committed, changed_event remains, lifecycle is marked DEGRADED, and ordinary Destroy preconditions reject a retry because lifecycle is no longer STOPPED.

### Worker

- Join has the same result/effect ambiguity: after successful wait, exit-code retrieval, and thread-handle close, it nulls thread and sets joined=TRUE before returning whether the exit code was ERROR_SUCCESS.
- Join timeout, GetExitCodeThread failure, and thread CloseHandle failure preserve different states and must be distinguished.
- Destroy has one wake_event close. A failed close leaves the handle in place; a successful close nulls it, deletes the lock, and clears initialized. Caller admission/lifetime still requires external quiescence; this result contract does not create concurrent public API safety.

### Adapter

- Adapter Destroy closes admission and can return busy while entrants/retained work remain. It does not stop or destroy Submitter/Worker.
- Existing adapter contract requires serialized Destroy calls, stable storage, and caller-side quiescence. Structured component results do not remove these requirements.

## Proposed public result schema

Use a caller-provided, versioned output structure; do not return a C struct by value. Initialize every output deterministically on function entry. Preserve the input `struct_size` and `version` fields while zeroing the known output region.

```c
typedef enum {
  RAYA_LC_OP_SUCCESS = 1,
  RAYA_LC_OP_ALREADY_DONE,
  RAYA_LC_OP_RETRYABLE_FAILURE,
  RAYA_LC_OP_BLOCKED,
  RAYA_LC_OP_TERMINAL_FAILURE,
  RAYA_LC_OP_INVALID_ARGUMENT,
  RAYA_LC_OP_INVALID_STATE
} raya_lifecycle_operation_status;

typedef enum {
  RAYA_LC_EFFECT_NONE = 0,
  RAYA_LC_EFFECT_STATE_CHANGED,
  RAYA_LC_EFFECT_SIGNAL_COMMITTED,
  RAYA_LC_EFFECT_THREAD_JOINED,
  RAYA_LC_EFFECT_HANDLE_CLOSED,
  RAYA_LC_EFFECT_PARTIAL_DESTROY,
  RAYA_LC_EFFECT_OBJECT_DESTROYED,
  RAYA_LC_EFFECT_UNKNOWN
} raya_lifecycle_effect;

typedef enum {
  RAYA_LC_RETRY_NEVER = 0,
  RAYA_LC_RETRY_SAME_OPERATION,
  RAYA_LC_RETRY_AFTER_DRAIN,
  RAYA_LC_RETRY_AFTER_EXTERNAL_REPAIR,
  RAYA_LC_RETRY_REQUIRES_RECONCILIATION
} raya_lifecycle_retry_class;

typedef enum {
  RAYA_LC_RESOURCE_NONE = 0,
  RAYA_LC_RESOURCE_THREAD,
  RAYA_LC_RESOURCE_WAKE_EVENT,
  RAYA_LC_RESOURCE_CHANGED_EVENT
} raya_lifecycle_resource;

typedef struct {
  uint32_t struct_size;
  uint32_t version;
  raya_lifecycle_operation_status status;
  raya_lifecycle_effect effect;
  raya_lifecycle_retry_class retry_class;
  raya_lifecycle_resource failed_resource;
  uint32_t native_error;       /* GetLastError captured immediately on failure */
  uint32_t thread_exit_code;   /* valid only when exit_code_valid */
  uint32_t failed_step;        /* stable operation-specific step enum */
  uint8_t exit_code_valid;
  uint8_t thread_joined;
  uint8_t object_initialized;
  uint8_t reserved;
  HANDLE remaining_thread;
  HANDLE remaining_wake_event;
  HANDLE remaining_changed_event;
} raya_lifecycle_result;
```

The handles are diagnostic borrowed values only: never close them from the result and never treat them as independently owned. In a stable-handle/registry design, replace raw handle exposure with resource-presence bits and opaque object token; do not leak OS handles across the public facade.

Do not overload `effect` with operation outcome. For example, Join can have status=TERMINAL_FAILURE and effect=THREAD_JOINED when the thread exited abnormally but was successfully reaped.

## Stable step identifiers

Define explicit per-operation step enums (not source line numbers):
- Shutdown: VALIDATE, TRANSITION_TO_STOPPING, SIGNAL_WAKE, SIGNAL_CHANGED, MARK_DEGRADED.
- Join: VALIDATE, WAIT_THREAD, GET_EXIT_CODE, CLOSE_THREAD_HANDLE, COMMIT_JOINED.
- Destroy Submitter: VALIDATE, CHECK_QUIESCENCE, CLOSE_WAKE_EVENT, CLOSE_CHANGED_EVENT, DELETE_LOCK, COMMIT_DESTROYED.
- Destroy Worker: VALIDATE, CHECK_QUIESCENCE, CLOSE_WAKE_EVENT, DELETE_LOCK, COMMIT_DESTROYED.

Capture GetLastError immediately after the failing Win32 call, before lock operations or any other API can overwrite it. For non-Win32 blockers, native_error is zero and status/failed_step carries the reason.

## Normative result mapping

### Shutdown

| Observed outcome | Status | Effect | Retry class |
|---|---|---|---|
| STOPPING signals both events | SUCCESS | SIGNAL_COMMITTED | NEVER (idempotent repeat may return ALREADY_DONE) |
| Already STOPPED | ALREADY_DONE | NONE | NEVER |
| State/ownership precondition not met | BLOCKED | NONE | RETRY_AFTER_DRAIN only if explicitly stated |
| Signal failed; lifecycle moved/marked DEGRADED | TERMINAL_FAILURE | STATE_CHANGED or UNKNOWN | REQUIRES_RECONCILIATION |
| Called in DEGRADED with no defined recovery primitive | INVALID_STATE | NONE | NEVER |

Do not classify DEGRADED shutdown as a same-operation retry. A future recovery primitive must state which signal/state transition it repairs and prove idempotence.

### Join

| Observed outcome | Status | Effect | Retry class |
|---|---|---|---|
| Wait returns WAIT_TIMEOUT | RETRYABLE_FAILURE | NONE | SAME_OPERATION |
| Wait returns WAIT_FAILED | RETRYABLE_FAILURE only if handle validity remains established; otherwise REQUIRES_RECONCILIATION | NONE | Conditional |
| GetExitCodeThread fails after signaled wait | RETRYABLE_FAILURE | NONE | SAME_OPERATION after revalidation |
| Thread CloseHandle fails | RETRYABLE_FAILURE | NONE | SAME_OPERATION after revalidation |
| Close succeeds, exit code is ERROR_SUCCESS | SUCCESS | THREAD_JOINED | NEVER |
| Close succeeds, exit code is nonzero | TERMINAL_FAILURE | THREAD_JOINED | NEVER |
| Thread handle absent / already joined | ALREADY_DONE only if joined state is verifiably committed; otherwise INVALID_STATE | NONE | NEVER |

Important: the current functions do not retain a distinct result object after an abnormal exit; `thread_exit_code` plus `joined` are the committed evidence. The _ex API must return TERMINAL_FAILURE + THREAD_JOINED for that case, not RETRYABLE_FAILURE.

### Destroy

| Observed outcome | Status | Effect | Retry class |
|---|---|---|---|
| Not quiescent / outstanding resources | BLOCKED | NONE | AFTER_DRAIN |
| First close fails and handle remains | RETRYABLE_FAILURE | NONE | SAME_OPERATION after revalidation |
| Submitter wake close succeeds; changed close fails | TERMINAL_FAILURE or RETRYABLE_FAILURE only after a new partial-state retry contract exists | PARTIAL_DESTROY | REQUIRES_RECONCILIATION in current implementation |
| Worker wake close fails and handle remains | RETRYABLE_FAILURE | NONE | SAME_OPERATION after revalidation |
| All required handles close and lock/object teardown commits | SUCCESS | OBJECT_DESTROYED | NEVER |

Current Submitter second-close partial Destroy is explicitly NOT retryable under the checked-in implementation. Do not return RETRYABLE_FAILURE until code has a durable per-handle cleanup cursor and an owner gate.

## Wrapper compatibility

- Keep existing `int raya_submitter_shutdown/join/destroy` and Worker equivalents for existing callers during migration.
- Add `raya_submitter_shutdown_ex/join_ex/destroy_ex` and corresponding Worker APIs.
- Each legacy wrapper calls the _ex implementation exactly once and maps only `status == SUCCESS` or `ALREADY_DONE` to true. It must not discard side effects in internal cleanup: callers that need to continue teardown must use _ex.
- Avoid a second independent implementation path; wrappers must not duplicate lifecycle logic.
- The Orchestrator must use _ex exclusively and persist each result before advancing its cleanup cursor.

## Required internal state before implementation

The result API alone is insufficient for safe partial cleanup. Add durable per-object cleanup metadata under the relevant lifetime owner:
- cleanup owner token/generation and exclusive ownership flag;
- operation and step cursor;
- per-resource state: OWNED_OPEN, CLOSE_IN_PROGRESS, CLOSED, CLOSE_FAILED, UNKNOWN;
- last native error and attempt count;
- committed thread exit code and validity;
- object lifecycle and whether public admission is closed.

A failed close must leave resource state and pointer consistent. A successful close must atomically record CLOSED/null before attempting the next resource. The Submitter second-event failure case must resume at changed_event without revisiting wake_event. Do not delete the critical section or zero the object until all resources are terminal and no API lease remains.

## Test acceptance matrix for implementation

1. Shutdown: both signals success; wake failure; changed failure; DEGRADED re-entry; repeated STOPPED call.
2. Join: timeout; WAIT_FAILED; GetExitCode failure; thread CloseHandle failure; success exit; abnormal exit with HANDLE closed; repeated Join.
3. Submitter Destroy: wake close failure; changed close failure after wake success; retry of the second handle only; concurrent cleanup owner; repeated Destroy after commit.
4. Worker Destroy: wake close failure; retry; successful terminal destroy.
5. Adapter/Orchestrator: component returns TERMINAL_FAILURE + THREAD_JOINED; cursor advances despite abnormal exit; resource-close retry; owned vs borrowed Worker; concurrent close attempts; caller lease drain.
6. Every test asserts both status and effects/resource state, not just Boolean return.

## Non-goals and safety gates

- This design does not claim concurrent Submitter/Worker public API safety.
- No full Raft claim, Production readiness, or Phase 1 acceptance.
- No Production or Source Project changes.
- No implementation or build until this contract is reviewed and the cleanup-owner/registry boundary is agreed.

## Next implementation sequence

1. Agree exact result schema/versioning and whether public facade exposes raw handles or only resource-presence metadata.
2. Implement Submitter _ex result APIs and durable per-handle Destroy cursor behind existing compatibility wrappers.
3. Add Submitter _ex focused tests, including successful second-handle retry.
4. Implement Worker _ex APIs and equivalent tests.
5. Integrate Orchestrator cleanup owner, leases, and cursor; only then run chained Owned/Borrowed failure matrix.
