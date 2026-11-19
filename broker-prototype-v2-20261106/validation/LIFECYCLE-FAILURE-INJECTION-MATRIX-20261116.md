# Lifecycle Failure-Injection Matrix — Submitter / Worker / Adapter (2026-11-16)

Status: read-only design and test-seam audit. No source changes, build, test, temporary harness, process termination, commit, or reset.

## Executive result

The existing code has useful but uneven injection seams:

- Submitter production API table already abstracts CreateEvent, CreateThread, SetEvent, ResetEvent, WaitForSingleObject, GetExitCodeThread, and CloseHandle. The API-test file's Submitter wrappers currently forward directly to Win32; SetEvent has only a narrowly scoped ACK failure switch, and CloseHandle has no Submitter-specific failure switch.
- Worker has a close callback, but it is used for operation/finalizer-owned handles. Worker Destroy closes its own wake_event with raw CloseHandle, and Worker Join uses raw WaitForSingleObject, GetExitCodeThread, and CloseHandle. Therefore the existing close callback does not inject Worker lifecycle teardown failures.
- Worker wake signaling has an RAYA_ADAPTER_TESTING callback seam.
- Adapter has an I/O close callback and admission/destroy concurrency coverage. Adapter Destroy's admission gate can be tested independently of component teardown.
- The current tests compose component lifecycle calls through a Boolean-only helper; they do not yet validate structured result snapshots or the complete chained cleanup failure matrix.

The next implementation unit should be test-seam-only and narrowly scoped: route Worker lifecycle Win32 operations through a copied API table (or dedicated lifecycle callbacks), and add deterministic per-operation/per-handle Submitter failure controls. Do not implement the lifecycle result API in the same change; first make the failure outcomes reproducible.

## Grounded implementation observations

### Submitter

The header's raya_submitter_win32_api contains injectable callbacks for all lifecycle operations. The implementation uses this table for shutdown SetEvent, Join wait/exit-code/close, and Destroy event closes.

Observed behavior:
- Shutdown transitions RUNNING to STOPPING before attempting wake_event and changed_event signals. A failure calls degrade_locked; that helper changes lifecycle to DEGRADED and attempts additional signals.
- Join returns FALSE for wait-not-object, GetExitCodeThread failure, CloseHandle failure, and nonzero exit code. After successful wait + exit-code read + close, it sets thread=NULL and joined=TRUE before returning whether exit code was zero.
- Destroy requires joined + STOPPED + empty queues/counters/leases. It closes wake_event first and changed_event second. If the first succeeds and second fails, wake_event is already NULL, lifecycle is degraded, and the ordinary precondition rejects retry.

Test seam status:
- API table: present and complete.
- Test wrappers: SetEvent can inject a targeted ACK failure only; Wait/GetExitCode/CloseHandle currently forward directly.
- Submitter close failure: not independently injectable in current API test wrapper.
- Submitter Shutdown signal failure: existing switch targets ACK path, not a deterministic first/second shutdown signal sequence.

### Worker

Observed behavior:
- Shutdown calls integrated shutdown and then signals wake_event through signal_wake_locked. Under RAYA_ADAPTER_TESTING a wake callback exists.
- Join uses raw WaitForSingleObject, GetExitCodeThread, CloseHandle.
- Destroy uses raw CloseHandle(wake_event), then deletes the lock and marks initialized false.
- set_close_callback configures close_handle used for operation/finalizer-owned handles, not the worker's lifecycle HANDLEs.

Test seam status:
- Wake failure: injectable under RAYA_ADAPTER_TESTING.
- Worker Join wait/exit-code/thread-close failures: no lifecycle injection seam.
- Worker Destroy wake_event close failure: no lifecycle injection seam.
- Existing operation close callback must not be mistaken for a Worker lifecycle close seam.

### Adapter

Observed behavior:
- Adapter API table provides injectable I/O operations and close callback.
- Adapter Destroy closes admission and may return false while active entrants/retained work drain. Existing concurrent API test exercises admission closure and later successful Destroy.
- Adapter Destroy does not itself stop or destroy Submitter/Worker.

Test seam status:
- Adapter I/O close failure: present.
- Admission close + BUSY + drain + retry: covered by existing concurrency test path.
- Structured effect snapshot: absent because *_ex API has not been implemented.

## Failure-injection matrix

Legend:
- Existing = current checked-in seam can deterministically inject the case.
- Partial = a related seam exists, but not at the lifecycle operation/handle in question.
- Missing = no checked-in seam found for this exact case.
- Required outcome is a test specification, not a claim that a test has passed.

| Component / operation | Injected condition | Expected structured disposition | Current seam |
|---|---|---|---|
| Submitter Shutdown | wake_event SetEvent fails before signal | DEGRADED/recovery-required; no ordinary retry claim | Partial |
| Submitter Shutdown | wake succeeds, changed_event fails | PARTIAL_EFFECT; record wake signal only; recovery-required | Missing |
| Submitter Shutdown | both signals succeed | STOP_REQUESTED, not joined/stopped | Existing APIs, no lifecycle assertion |
| Submitter Join | wait timeout | IN_PROGRESS; thread HANDLE retained; joined=false | Partial (wait callback exists, no test switch) |
| Submitter Join | WAIT_FAILED | OUTCOME/ERROR; preserve HANDLE and state | Partial |
| Submitter Join | GetExitCodeThread fails after signaled wait | exit-code unknown; define whether to retain HANDLE and retry | Partial |
| Submitter Join | thread HANDLE CloseHandle fails | no THREAD_HANDLE_CLOSED effect; joined=false | Partial |
| Submitter Join | nonzero exit code, close succeeds | COMPLETED_ABNORMAL_EXIT; joined=true; never Join again | Partial |
| Submitter Destroy | wake_event close fails | no close effect; explicit retry only if state remains safe | Partial |
| Submitter Destroy | wake close succeeds, changed close fails | PARTIAL_EFFECT; wake closed, changed retained; explicit remaining-handle retry required | Partial |
| Submitter Destroy | repeated changed close failure | preserve remaining handle/object/cleanup owner | Partial |
| Submitter Destroy | both closes succeed | COMPLETED; object storage must not be touched again | Existing APIs, no targeted lifecycle assertions |
| Worker Shutdown | wake signal fails | recovery cause recorded; not ordinary retry | Existing under RAYA_ADAPTER_TESTING |
| Worker Join | wait timeout / WAIT_FAILED | IN_PROGRESS or error; thread HANDLE retained | Missing |
| Worker Join | GetExitCodeThread fails | exit code unknown; retain/close policy explicit | Missing |
| Worker Join | thread HANDLE close fails | no joined flag, no handle-null | Missing |
| Worker Join | abnormal exit code, close succeeds | COMPLETED_ABNORMAL_EXIT; joined=true | Missing |
| Worker Destroy | wake_event close fails | retryable close failure only if all state/counters still safe | Missing |
| Worker Destroy | wake_event close succeeds | finalized; no further object access | Missing lifecycle seam |
| Adapter Destroy | entrants remain after admission closes | BUSY with ADMISSION_CLOSED effect | Existing concurrency coverage |
| Adapter Destroy | entrants drain, retry destroy | COMPLETED | Existing concurrency coverage |
| Adapter Destroy | concurrent second Destroy | one cleanup owner; other BUSY/OWNER_EXISTS | Needs exact duplicate-destroy assertion |
| Chained cleanup | Submitter partial Destroy after Adapter Destroy | cursor stops at Submitter remaining handle; Worker not prematurely destroyed/unregistered | Missing end-to-end |
| Chained cleanup | Worker close fails after Submitter finalized | cursor preserves Submitter terminal state and retries Worker only | Missing end-to-end |
| Chained cleanup | abnormal Join then Destroy | blocked until explicit policy decides whether abnormal exit is destroy-safe | Missing end-to-end |

## Required seam design (test-only first)

### 1. Submitter test wrapper improvements

Keep the production callback table unchanged. Extend only the test API state/wrappers with:
- fail_set_event_on_nth_call or fail_set_event_for_exact_handle;
- fail_wait_for_exact_thread;
- fail_get_exit_code_for_exact_thread;
- fail_close_for_exact_handle / nth close call;
- configurable exit code for a completed thread;
- call counters and captured error values.

Important: exact-handle targeting is preferable to a global fail_close Boolean because the Submitter closes thread, wake_event, and changed_event at different lifecycle steps. Every injected failure must set a deterministic Win32 last-error value.

### 2. Worker lifecycle API seam

Preferred: add a small Worker lifecycle Win32 API table copied before the thread starts, containing wait, get-exit-code, close-handle, and wake-signal functions. If ABI impact is unacceptable, add a private test-only lifecycle API setter under RAYA_ADAPTER_TESTING and keep the production path on direct Win32 calls.

Do not reuse the operation finalizer close callback for Worker lifecycle handles: the semantics and call sites are different.

### 3. No production behavior change in the seam-only step

- Do not add recovery from DEGRADED.
- Do not make partial Submitter Destroy retryable yet.
- Do not alter Adapter/Submitter ownership.
- Do not add the Orchestrator registry or stable handle yet.
- Do not claim full lifecycle safety from injection coverage alone.

## Test execution order after seams are implemented

1. Unit-level Submitter Shutdown: each signal failure position and success.
2. Submitter Join: timeout, WAIT_FAILED, exit-code failure, close failure, zero exit, abnormal exit.
3. Submitter Destroy: first close failure, second close failure, repeated second close failure, full success.
4. Worker Shutdown: wake failure and normal signal.
5. Worker Join: same five Join branches as Submitter.
6. Worker Destroy: close failure then retry; successful finalization.
7. Adapter Destroy: active entrant BUSY, drain, retry; duplicate cleanup owner.
8. Chained cleanup cursor: fail one step at a time and verify completed earlier steps are never repeated.
9. Owned and Borrowed Worker variants.
10. Concurrent caller stress only after lifecycle admission/cleanup-owner gate is implemented.

For each case assert: result code, effects bitmask, before/after state, handle presence/absence, joined flag, Win32 error validity/value, retry hint, close-call count, and no duplicate callback/release.

## Safety / resource rules

- No broad process termination.
- No Build/TLC run in this design step.
- No temp artifacts created.
- No files deleted.
- No source files modified.
- Production and Source Project untouched.

## Current blockers and priority

P0: Add deterministic lifecycle seams for Worker and exact-handle failure controls for Submitter.
P1: Implement and test the structured lifecycle result API using those seams.
P2: Define safe Submitter partial-Destroy retry with cleanup owner.
P3: Define DEGRADED shutdown recovery (or explicitly make it terminal/manual-recovery).
P4: Resolve Adapter/Submitter ownership contract.
P5: Implement Orchestrator stable handle, leases, cleanup owner and cursor.
P6: Run the chained Owned/Borrowed failure matrix.

## Evidence and scope

This document is a read-only audit and test design. It does not report any new test pass count.

Initial and final process inventory found no cl.exe, link.exe, java.exe, or tlc.exe processes. Disk space was checked before inspection. No build or test was run.

