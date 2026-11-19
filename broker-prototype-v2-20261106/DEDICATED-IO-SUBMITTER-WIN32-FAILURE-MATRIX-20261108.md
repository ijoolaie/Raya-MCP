# Dedicated I/O Submitter — Win32 API Injection and Failure Matrix
Date: 2026-11-08

## Scope

This change adds a per-instance Win32 API table to `dedicated-io-submitter-v2`.
The existing `raya_submitter_start` API remains source-compatible and selects
the real Win32 implementation. Tests can call
`raya_submitter_start_with_api` with a copied function table to inject failures.

Injected operations:
- `CreateEventW`
- `CreateThread`
- `SetEvent`
- `ResetEvent`
- `WaitForSingleObject`
- `GetExitCodeThread`
- `CloseHandle`

The default table calls the real Win32 APIs through small wrapper functions.
The module stores its own copy of the table; the caller's table need not remain
alive after startup.

## Failure handling changes

- Startup now creates and records each event separately.
- If a later startup step fails, each successfully created HANDLE is closed
  independently and its field is cleared only after a successful close.
- If cleanup close fails, the unclosed HANDLE and initialized critical section
  remain owned by a sticky `DEGRADED` state. The function returns failure and
  does not report a clean uninitialized state.
- Thread-creation failure uses the same independent close accounting.
- Runtime SetEvent, ResetEvent, and worker WaitForSingleObject failures retain
  the queue/lease state and make the lifecycle `DEGRADED`.
- A join wait failure does not close the thread HANDLE or set `joined`; a
  subsequent join can retry. Destroy remains prohibited until join succeeds.
- CloseHandle failure during destroy retains the remaining HANDLE(s) and
  prevents critical-section deletion.

The module does not attempt to retry an uncertain close automatically.
Retained degraded objects are intentionally not destroyable through the clean
shutdown API. The test harness manually releases its disposable test resources
after verifying retention; this is test cleanup, not a production recovery path.

## Native failure matrix

Build: MSVC x64, `/W4 /WX`, real Win32 events and threads with injected
single-call failures.

Covered:
1. First CreateEvent failure — no partially initialized state.
2. Second CreateEvent failure with successful first-event close — clean rollback.
3. Second CreateEvent failure plus CloseHandle failure — first HANDLE retained.
4. CreateThread failure plus one CloseHandle failure — failed-close HANDLE retained,
   independently closed second HANDLE recorded as closed.
5. SetEvent failure during shutdown — lifecycle degrades; queued/lease counters
   remain consistent; worker is not mistaken for cleanly stopped.
6. ResetEvent failure in the worker — sticky degraded state.
7. WaitForSingleObject failure in the worker — sticky degraded state.
8. WaitForSingleObject failure during join — thread HANDLE and unjoined state
   retained; a subsequent join succeeds.
9. CloseHandle failure during destroy — remaining HANDLEs retained and destroy
   rejected.

Failure matrix: 51 assertions per process x 25 runs = **1,275 assertions passed**.

## Regression runs

- Existing core lifecycle test: 18 assertions x 25 = 450 passed.
- Publish/ACK race: 22 assertions x 50 = 1,100 passed.
- Backpressure: 29 assertions x 25 = 725 passed.
- Multi-producer: 137 assertions x 10 = 1,370 passed.

Combined current run set: **4,920 assertions passed**.

## Limits and open work

- The failure matrix injects one selected API call at a time; it is not a
  combinatorial test of simultaneous failures.
- GetExitCodeThread is routed through the seam but its failure case is not yet
  injected in this matrix.
- SetEvent failure is exercised at shutdown; enqueue and ACK SetEvent call sites
  still need dedicated fault-position tests.
- No real overlapped ReadFile/WriteFile adapter is integrated with this module.
- No integrated Submitter + Managed Completion Worker + two-phase finalizer
  path, long soak, OS handle-count baseline, or full Win32 failure campaign.
- No Production wiring or service provisioning.

Production status remains **NO-GO**.

## Follow-up: SetEvent call-site and GetExitCodeThread failures

Additional deterministic fault-position tests were added:
- enqueue wake signal failure: the request remains queued and retained; lifecycle becomes DEGRADED; the test manually wakes the disposable worker only to verify its terminal degraded exit;
- admission-abort changed-event signal failure: admission accounting reaches zero exactly once, request becomes NOT_ISSUED, and lifecycle becomes DEGRADED;
- terminal ACK changed-event signal failure: issuer lease reaches zero exactly once, request becomes ACKED, and lifecycle becomes DEGRADED. The test manually wakes the disposable worker because the injected changed-event failure short-circuits the subsequent wake signal. This exposes a liveness limitation: degraded ownership is retained, but the worker may remain asleep until an independent wake occurs;
- GetExitCodeThread failure after the worker has exited: join returns failure while retaining the thread HANDLE and joined=false; a subsequent join succeeds.

Updated Failure Matrix: 92 assertions per process x 25 runs = **2,300 assertions passed**.

Regression reruns after the changes:
- Core lifecycle: 18 x 25 = 450 passed.
- Publish/ACK race: 22 x 50 = 1,100 passed.
- Backpressure: 29 x 25 = 725 passed.
- Multi-producer: 137 x 10 = 1,370 passed.

Combined current run set: **5,945 assertions passed**.

Open limitations: SetEvent failure in the NOT_ACCEPTED notification branch and in STOPPED notification is not yet fault-position tested; simultaneous multi-API failures are not covered. The ACK wake-short-circuit case is fail-closed but may require an independent wake for worker exit. Production remains NO-GO.

## Follow-up correction: degraded wake propagation

The 92-assertion matrix exposed a liveness issue: when ACK's first changed-event SetEvent failed, the short-circuit prevented the subsequent wake-event signal, leaving the Submitter asleep despite DEGRADED lifecycle. The same risk existed for other external callers entering the common degradation path.

`degrade_locked` now attempts both changed-event and wake-event signaling after setting the sticky DEGRADED state. This does not clear degradation or release ownership. If either signal remains unavailable, the object stays degraded and resources remain retained. The enqueue, admission-abort, and ACK fault-position tests now verify the worker exits degraded without the test manually signaling the wake event.

Final Failure Matrix after this fix: 89 assertions/process x 25 = **2,225 passed**.
Final regression reruns: core 450, ACK race 1,100, backpressure 725, multi-producer 1,370; **3,645 passed**.
Combined final run set: **5,870 assertions passed**.

The earlier 92-assertion/2,300 figure above records the pre-correction run and is superseded by this final run. Remaining untested SetEvent positions: NOT_ACCEPTED notification and STOPPED notification. GetExitCodeThread failure is covered and retryable; simultaneous multi-API failures remain out of scope.

## Follow-up: NOT_ACCEPTED and STOPPED notification fault positions

Two remaining SetEvent positions are now explicitly fault-injected.

- NOT_ACCEPTED notification: after a definite not-issued result, the Submitter
  decrements active_issue and issuer_leases exactly once and marks the request
  NOT_ISSUED. If signaling changed_event fails, it now enters DEGRADED through
  the shared helper, which attempts both changed_event and wake_event signaling;
  the not_issued callback still runs exactly once before the worker exits.
- STOPPED notification: if signaling changed_event after the drain fails, the
  worker transitions from STOPPED to DEGRADED, attempts both degradation
  signals, and exits with an error status. The object is not eligible for clean
  destroy even though the thread has terminated.

The failure harness asserts retained counters, request state, callback count,
worker termination, and non-clean join result for both injected failures.

Updated matrix: 108 assertions/process x 25 runs = **2,700 passed**.

All Submitter regressions were rerun after the source changes:
- Core lifecycle: 18 x 25 = 450 passed.
- Publish/ACK race: 22 x 50 = 1,100 passed.
- Backpressure: 29 x 25 = 725 passed.
- Multi-producer: 137 x 10 = 1,370 passed.

Combined final run set: **6,345 assertions passed**.

Remaining limitations: simultaneous multi-API failure combinations are not
covered; this is still callback-seam Submitter coverage, not the real overlapped
ReadFile/WriteFile adapter or integrated Completion Worker. Production remains
NO-GO.
