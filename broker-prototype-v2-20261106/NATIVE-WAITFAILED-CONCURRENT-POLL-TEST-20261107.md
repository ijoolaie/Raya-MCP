# Concurrent Polling Recovery after Native WAIT_FAILED — 2026-11-07

## Objective
Exercise the degraded polling recovery path with a dedicated worker thread while a separate producer thread context (the test's main thread) completes real overlapped named-pipe operations gradually. Verify rotating 63-entry WaitPlans continue to make progress beyond the first window.

## Harness
- 70 independent byte-type named-pipe instances, each with a dedicated server/client pair, overlapped read, completion event, and one-byte payload.
- All reads were submitted and promoted into Integrated Core + Registry before the worker started.
- The worker's first native wait used a deliberately invalid wake HANDLE value; WaitForMultipleObjects returned WAIT_FAILED and the worker captured ERROR_INVALID_HANDLE (6) immediately.
- The worker marked the lifecycle DEGRADED, switched to polling mode using a valid recovery wake handle for WaitPlan construction, and continued rebuilding bounded rotating plans.
- The main thread wrote one byte to operations in a staggered permutation, sleeping 2 ms between writes while the worker was polling concurrently.
- For each observed signaled operation event, the worker resolved and claimed the exact Registry operation/event pair under the critical section, called GetOverlappedResult with the exact issuing server HANDLE and OVERLAPPED, and collected only successful terminal results under the lock.
- The worker exited only after all 70 operations had been terminally collected. The degraded lifecycle was deliberately not reset.

## Results
- MSVC x64 `/W4 /WX`: PASS.
- Independent process runs: 10/10 PASS.
- Assertions per run: 714.
- Total assertions: 7,140; failures: 0.
- Completed operations: 70/70 in every run.
- WaitPlan builds: 123–134 per run.
- Rotation observations: 109–120 per run.
- Native wait error: ERROR_INVALID_HANDLE (6) in every run.
- Final Registry count, Core outstanding count, and reserved slots: all zero.
- Lifecycle remains DEGRADED; clean exit remains denied.
- Build script removes executable and object files.

## What this demonstrates
- Actual worker-thread polling concurrent with staggered peer completions, not a single-threaded precompleted batch.
- Repeated WaitPlan snapshots and 63-entry rotating windows continue while operations are concurrently removed from the Registry.
- The operation/event identity is revalidated against the current Registry before collection, so stale entries in an earlier plan are not blindly trusted.
- Successful terminal collection requires exact issuing HANDLE/OVERLAPPED completion evidence.

## Limitations
- This is a test-only worker harness, not the lifecycle-managed production Reaper Worker.
- The producer is one main thread, not multiple concurrent producers; it tests producer/worker overlap but not producer admission/handoff contention.
- Polling uses a 1 ms sleep only when a pass makes no progress; no hard fairness or latency bound is claimed.
- The invalid wake handle is injected as a deliberately invalid test HANDLE value. Wake-event recreation and a production-grade lost-wake protocol are not implemented here.
- No cancellation, disconnect, event/result inconsistency, shutdown race, CloseHandle failure, IOCP, or full 28-group acceptance coverage.
- Does not link or runtime-test pipe-io-v2.c.

## Gate
This is additional concurrent runtime evidence for the polling-only degraded recovery path. It does not clear the 28-group runtime acceptance gate. Production remains NO-GO.