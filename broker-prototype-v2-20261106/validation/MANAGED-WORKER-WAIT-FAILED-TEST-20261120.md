# Managed Worker WAIT_FAILED Runtime Evidence — 2026-11-20

## Scope

Close the missing runtime-evidence gap for the actual Managed Worker thread's `WaitForMultipleObjects` failure branch. This is test-only evidence; it does not change Production wait behavior, ownership, retention, cleanup, or recovery semantics.

## Test-only seam

- Added `raya_managed_wait_failure_test_fn` and a WAIT_FAILED-only test hook/context to `raya_managed_worker`, all under `RAYA_ADAPTER_TESTING`.
- Added `raya_managed_worker_start_with_wait_failure_test_hook`. It installs the hook before the actual Worker thread is created, avoiding a race while configuring a live wait.
- The hook is deliberately narrow: it returns only a Boolean failure decision and an error code. TRUE makes the Worker wrapper set that error and produce WAIT_FAILED; FALSE performs the real Win32 wait. It cannot inject arbitrary wait indexes or other WAIT_* outcomes.
- The Worker invokes the hook at the exact `WaitForMultipleObjects` boundary only when configured. With the test macro absent, the production compilation path still calls Win32 `WaitForMultipleObjects(..., INFINITE)` directly. With the macro present and no hook configured, the pre-existing finite test wait remains unchanged.
- No callback, field, setter, or ABI is present in the non-test compilation path.

## Deterministic runtime schedule

The dedicated `--managed-worker-wait-failed` process:
1. Starts the real Managed Worker with the WAIT_FAILED-only hook.
2. Uses an explicit entered-event barrier to prove the callback's first invocation is waiting on the real Worker wait set before submission begins.
3. The first hook invocation declines injection; the Worker then calls the real Win32 wait, which returns the actual wake result.
4. Submits one real overlapped named-pipe READ through the existing Adapter/Submitter path. The injected `ReadFile` wrapper records exactly one call and the real OS result `ERROR_IO_PENDING`.
5. The Worker handles the wake, promotes the operation into its Registry, and reaches the same wait boundary again.
6. The second hook invocation returns TRUE with ERROR_INVALID_HANDLE. The Worker wrapper sets that last-error value and produces WAIT_FAILED.

No live handle is closed or invalidated to manufacture the failure. No operation state, counter, Registry entry, reservation, or retained list is mutated by the test callback.

## Observed and asserted postconditions

- Exactly two wait-boundary hook invocations; one real initial wait and one injected WAIT_FAILED.
- Exactly one `ReadFile` attempt returning `ERROR_IO_PENDING`.
- Worker lifecycle is `DEGRADED`; the only degradation cause is `WAIT_FAILED`.
- Worker thread exits with `ERROR_INVALID_HANDLE`; Join observes the exit, clears the thread handle, and records `joined=TRUE`. The Boolean Join result is false because the worker exit code is the injected failure.
- Core `queued=0`, `outstanding=1`, `caller_active=0`; reservation count is 1; Registry count is 1. The operation is Registry-owned, its owner state is `REAPER_PENDING`, and collection remains pending.
- The operation buffer, issuing-pipe handle, completion-event handle, and `OVERLAPPED.hEvent` remain present/valid.
- Worker retained inventory remains empty; Adapter active count is 1 with no Adapter retained record; user callback count is zero.
- Submitter remains RUNNING with zero queued requests, producers, and active issue calls, and one outstanding issuer lease.

This establishes the Managed Worker-level native `WAIT_FAILED` branch and the exact in-memory ownership snapshot at failure. It does not establish a Worker retained-inventory publication for this queue/Registry-owned operation.

## Isolation and cleanup boundary

The operation is still in-flight/uncertain and has no approved release/reconciliation path. The dedicated process deliberately does not destroy Adapter, Submitter, or Worker; it does not close operation handles, release reservations, unlink/free records, retry ACK, or replay I/O. The process boundary reclaims only this fixture's process-local resources at exit. This is a test-fixture boundary, not a Product recovery mechanism.

## Validation — 2026-11-20

Visual Studio 2026 Build Tools 18.7.3, x64, `/W4 /WX`:

- API build script: 10 isolated Managed Worker WAIT_FAILED runs × 31 assertions = 310 PASS.
- Strict dual-ACK: 10 × 120 assertions = 1,200 PASS.
- NOT_ISSUED notification isolation: 10 × 25 assertions = 250 PASS.
- Full Adapter API regression: 10 × 1,081 assertions across 68 cases = 10,810 PASS.
- Non-test-macro Managed Pipe native build: 10 runs × 176 assertions = 1,760 PASS. This confirms the new wait seam is excluded from the non-test compilation path.
- Both prescribed build scripts removed their declared EXE/OBJ outputs.

The first non-test-macro native-suite attempt passed eight executions and then failed while creating its second named-pipe fixture; a complete rerun passed all ten executions. The initial transient failure is retained here rather than omitted.

## Disposition

The Managed Worker-level runtime WAIT_FAILED evidence gap is closed. Stage 5.18 remains PARTIAL because the separate, already-confirmed wake-signal failure case leaves transferred-but-unprocessed work Adapter-retained and Worker-queue/reservation-owned but absent from the Worker v1 retained inventory. That cross-plane inventory/ownership-contract gap is not changed or repaired by this test. Invariant-guard runtime fault injection remains intentionally excluded where it would require manufactured internal corruption.

Stage 5.16 and 5.19 remain COMPLETE within their recorded criteria. Stage 5.20 remains INCOMPLETE; Gate D remains NO ACTION APPROVED; Gate E remains BLOCKED; Gate F remains pending full Stage 5/Phase 1 owner acceptance and PR review. Stage 5/Phase 1 remain NOT ACCEPTED; Production remains NO-GO.
