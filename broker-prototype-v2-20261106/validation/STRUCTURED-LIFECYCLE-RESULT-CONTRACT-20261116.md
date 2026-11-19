# Structured Lifecycle Result Contract — Submitter / Worker / Adapter (2026-11-16)

Status: design-only audit. No source edits, builds, tests, temporary copies, process termination, commits, or resets.

## Decision

Add versioned lifecycle APIs that return a structured result and a side-effect snapshot. Keep existing int-returning APIs as compatibility wrappers until all consumers and tests migrate. Do not change existing function return types in-place.

The current code already uses typed result enums for Submitter enqueue, Adapter submit, Worker handoff, reaper collect, and other subsystems. A typed lifecycle result is consistent with the repository's conventions.

## Why Boolean is insufficient

A FALSE return currently conflates materially different cases:

- operation rejected before any side effect;
- timeout while the operation remains in progress;
- a Win32 signal/close operation failed before changing the corresponding resource;
- one handle was closed but a later close failed;
- thread Join completed and closed the thread HANDLE, but the thread exit code was abnormal;
- component transitioned to DEGRADED and cannot follow the ordinary retry path;
- component state could not be established safely.

A retry controller must distinguish these outcomes or it may repeat destructive operations, skip required cleanup, or free live resources.

## Proposed common types (illustrative)

Use stable numeric enum values and explicit version/size fields:

- raya_lifecycle_api_version
- raya_lifecycle_operation: SHUTDOWN, JOIN, DESTROY
- raya_lifecycle_code:
  - COMPLETED
  - COMPLETED_ABNORMAL_EXIT
  - IN_PROGRESS
  - RETRYABLE_NO_EFFECT
  - PARTIAL_EFFECT
  - BLOCKED_STATE
  - BUSY
  - INVALID_ARGUMENT
  - INTERNAL_ERROR
  - OUTCOME_UNKNOWN
- raya_lifecycle_effect_flags:
  - ADMISSION_CLOSED
  - STOP_SIGNAL_SENT
  - THREAD_EXIT_OBSERVED
  - THREAD_HANDLE_CLOSED
  - WAKE_EVENT_CLOSED
  - CHANGED_EVENT_CLOSED
  - COMPONENT_STORAGE_FINALIZED
- raya_lifecycle_snapshot:
  - struct_size and api_version
  - operation and code
  - effect_flags
  - lifecycle_before and lifecycle_after
  - joined and initialized observations
  - thread_exit_code_valid and thread_exit_code
  - win32_error_valid and win32_error
  - retry_hint: NONE, RETRY_SAME_STEP, WAIT_AND_RETRY, RECOVERY_REQUIRED, NEVER_RETRY
  - state_consistent flag

The snapshot is a value object. Do not return internal pointers, HANDLE values, queue nodes, or references to mutable component fields.

The result code describes the operation's disposition. Effect flags describe what definitely happened. Retry hint is advisory and cannot override preconditions or state validation.

## API shape (illustrative)

- raya_submitter_shutdown_ex(submitter, timeout_ms, snapshot)
- raya_submitter_join_ex(submitter, timeout_ms, snapshot)
- raya_submitter_destroy_ex(submitter, snapshot)
- raya_managed_worker_shutdown_ex(worker, timeout_ms, snapshot)
- raya_managed_worker_join_ex(worker, timeout_ms, snapshot)
- raya_managed_worker_destroy_ex(worker, snapshot)
- raya_integrated_adapter_destroy_ex(adapter, snapshot)

Each function:
1. validates arguments and API version/struct_size;
2. initializes every output field deterministically;
3. performs at most one defined lifecycle transition;
4. records each irreversible effect immediately after it succeeds;
5. captures error code immediately after a failing Win32 call, before another API can overwrite thread-local last-error;
6. returns a snapshot consistent with the operation's linearization point;
7. does not claim a full object snapshot if external component APIs can mutate the same fields without a shared synchronization contract.

For asynchronous Shutdown, IN_PROGRESS is distinct from COMPLETED. A successfully sent wake signal means STOP_REQUESTED, not THREAD_EXITED or STOPPED.

## Component-specific semantics

### Submitter Shutdown
- COMPLETED only when the documented shutdown signal/state transition has completed.
- If wake_event succeeds but changed_event fails, report PARTIAL_EFFECT with STOP_SIGNAL_SENT (if applicable), lifecycle_after=DEGRADED, win32_error set, retry_hint=RECOVERY_REQUIRED.
- If the first signal fails before any effect, report RETRYABLE_NO_EFFECT only if the current state and API contract prove a retry is safe.
- Never report STOPPED solely because a signal was sent.

### Submitter Join
- If WaitForSingleObject times out, report IN_PROGRESS, no THREAD_HANDLE_CLOSED, joined=false.
- If wait succeeds, capture GetExitCodeThread result and close the thread HANDLE exactly once.
- If close succeeds, report THREAD_EXIT_OBSERVED and THREAD_HANDLE_CLOSED, joined=true, even when exit code is nonzero.
- Exit code zero => COMPLETED.
- Nonzero exit code => COMPLETED_ABNORMAL_EXIT. This is not a retryable Join failure.
- If GetExitCodeThread fails after wait, distinguish thread termination observed from exit-code validity; close behavior must be explicitly defined and tested.
- If CloseHandle fails, report PARTIAL_EFFECT or OUTCOME_UNKNOWN according to whether the handle is known valid and retained. Never set thread=NULL unless close succeeded.

### Submitter Destroy
- Report WAKE_EVENT_CLOSED immediately after successful close and nulling.
- Report CHANGED_EVENT_CLOSED only after that exact close succeeds.
- If wake_event closes and changed_event close fails, report PARTIAL_EFFECT, preserve initialized object and remaining handle, and require a dedicated serialized partial-destroy retry API/state. Current checked-in Destroy cannot safely retry this DEGRADED partial state.
- Final COMPLETED requires all owned handles closed, lock lifecycle finalized, and initialized=false.
- Do not expose an effect flag for a lock deletion unless the design has a safe way to report after the object storage is invalid. The Orchestrator should record finalization from the return contract before releasing the object.

### Worker Shutdown
- Distinguish signal sent from worker exit observed.
- If wake signaling fails and Worker enters DEGRADED, report PARTIAL_EFFECT or BLOCKED_STATE; do not authorize Join/Destroy as if normal STOPPING occurred.
- Recovery eligibility and exact retry rules must be separately specified; a structured result does not itself make DEGRADED recoverable.

### Worker Join
- Timeout => IN_PROGRESS; preserve thread HANDLE and joined=false.
- Successful wait + successful thread HANDLE close => joined=true and THREAD_HANDLE_CLOSED.
- Exit code zero => COMPLETED; nonzero => COMPLETED_ABNORMAL_EXIT.
- Abnormal exit must not be represented as a plain retryable FALSE.

### Worker Destroy
- Close failure with handle retained => RETRYABLE_NO_EFFECT for that specific close substep, provided all preconditions still hold.
- Success => WAKE_EVENT_CLOSED and finalization committed.
- If any operation/retained/caller count is nonzero or unknown => BLOCKED_STATE, not retryable close failure.

### Adapter Destroy
- Admission closure is a committed effect even if destroy returns BUSY due active entrants or retained operations.
- Report ADMISSION_CLOSED independently from COMPLETED.
- A BUSY result after admission closure is expected and retryable only after active entrants/retained work drain.
- On final success, report component finalized; caller must not inspect the destroyed Adapter object afterward.

## Compatibility and migration

1. Add a shared lifecycle result header with fixed-width fields and documented enum values.
2. Add *_ex APIs; old int APIs call the new API and map only COMPLETED to TRUE. Document that the legacy Boolean remains lossy.
3. Migrate Orchestrator and tests to *_ex APIs first.
4. Add explicit diagnostic snapshot functions for states that can be safely observed under component locks; never read public struct fields concurrently without synchronization.
5. After all direct component consumers migrate, make component headers private/opaque as planned by Architecture A.
6. Remove legacy APIs only in an explicitly versioned breaking change, not during this hardening step.

## ABI and correctness constraints

- Include struct_size and api_version to allow future extension without silently reading/writing beyond caller storage.
- Use fixed-width integer types for public snapshot fields; do not publish compiler-dependent bitfield layouts.
- Define which thread owns and may call each lifecycle function. A result structure does not make concurrent Destroy safe.
- Serialize partial Destroy retries with a component cleanup-owner gate.
- Capture GetLastError immediately on the failing call.
- Do not call callbacks while holding registry locks; do not hold a registry lock across component calls or waits.
- Avoid exposing raw Win32 HANDLEs in the public snapshot.
- Document that COMPLETED_ABNORMAL_EXIT means the thread HANDLE was reaped, not that component invariants are necessarily clean or Destroy is authorized.

## Acceptance tests

1. Legacy wrappers preserve existing documented TRUE/FALSE mapping.
2. Shutdown wake succeeds, secondary signal fails: PARTIAL_EFFECT with exact effect flags and error.
3. Shutdown first signal fails: no false effect flags; retry disposition only if safe.
4. Join timeout: IN_PROGRESS, thread HANDLE retained, joined=false.
5. Join success with exit code zero: COMPLETED and exactly one thread HANDLE close.
6. Join success with nonzero exit code: COMPLETED_ABNORMAL_EXIT, joined=true, thread HANDLE closed exactly once; second Join is not attempted.
7. GetExitCodeThread failure after thread signaled: exact exit-code-validity and close disposition.
8. Thread HANDLE CloseHandle failure: no false THREAD_HANDLE_CLOSED flag.
9. Submitter wake_event close succeeds then changed_event close fails: PARTIAL_EFFECT; wake closed, changed retained; retry only the remaining handle through explicit partial-state API.
10. Repeated changed_event close failure preserves remaining handle and object state.
11. Worker wake_event close failure is retryable only when Worker remains joined/STOPPED and all ownership counters are zero.
12. Adapter destroy returns BUSY after closing admission: ADMISSION_CLOSED remains set; new callers reject; retry after drain succeeds.
13. Two cleanup callers cannot execute the same *_ex Destroy concurrently.
14. Snapshot version/size mismatch is rejected without modifying component state.
15. Every result code is tested against the cursor transition table; no Boolean-only inference is permitted in Orchestrator.

## Open blockers

- Submitter partial-destroy retry is still only a temporary-source prototype, not checked-in.
- Worker and Submitter DEGRADED shutdown recovery remains unspecified.
- Abnormal thread exit disposition versus component Destroy eligibility must be decided.
- Adapter header's Submitter ownership comment conflicts with actual Adapter start behavior.
- Deterministic close injection for the exact thread/event handles is not fully integrated in checked-in APIs.
- No checked-in Orchestrator or Stable Handle facade exists.

## Scope and evidence

This report is design-only. The audit confirmed existing typed result conventions in Submitter, Adapter, Worker handoff, and reaper APIs. Existing function declarations remain int-returning for lifecycle calls. No implementation or test was performed in this step.

C:/D: free-space and process inventory were checked before inspection. No cl/link/java/tlc processes were present. No files outside this report were changed.

## Source hashes at audit time

- dedicated-io-submitter-v2.h: 2D4CBB91D3400AC5416FC1DE25D03012AC2D2644D6F848A42D278F38D5191655
- reaper-managed-worker-v2.h: F45DB7509C2045A37925A86BE06F4871109BA5348A383B75447ACD3C46423818
- integrated-submit-adapter-v2.h: 4E2CC45DCD9E4EA0EA47441158B40F25CCD900095182CD256A90C285FB8D4882
- dedicated-io-submitter-v2.c: D8ABAFD8D3B731DA141E8B1792534A00EB888055D0F15EB0EDFCCE2D01843941
- reaper-managed-worker-v2.c: FDCD768B31CD6FF30F435A5588D6D1000394A7B355A2E37C1A8117D6C78225E4
- integrated-submit-adapter-v2.c: 6E2427C09974EE47B382F80CD11B21FEAAC16D12ECABC6A6155A11E9E8F62FBE
