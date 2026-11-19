# Shutdown / Join / Destroy Concurrency Review — 2026-11-18

Scope: inspect managed Worker lifecycle implementation, lifecycle API contract, orchestrator cleanup-owner contract/cursor, and existing concurrency/lifecycle tests. Three targeted existing build scripts were run. No source, header, API, build script, or README changed.

## Implementation findings

### Shutdown
- `raya_managed_worker_shutdown_ex` takes the Worker critical section while transitioning ACCEPTING to STOPPING and signaling the wake event. Repeated calls while STOPPING/STOPPED are treated as idempotent at the state level, but each STOPPING call signals wake again.
- Admission/reservation/handoff also use the Worker lock, so shutdown closes admission atomically relative to those operations.
- Wake-signal failure marks WAKE_SIGNAL degradation while preserving the STOPPING effect; it is not a clean shutdown.

### Join
- `raya_managed_worker_join_ex` does not take the Worker lock around `joined`, `thread`, `WaitForSingleObject`, `GetExitCodeThread`, `CloseHandle`, or the subsequent field writes.
- Timeout, WAIT_FAILED, exit-code query failure, and thread-handle close failure preserve the thread handle and leave the worker unjoined so the same operation can be retried.
- After successful thread-handle close, it sets `thread=NULL` and `joined=TRUE`; abnormal thread exit is still a committed handle-reap effect, but returns TERMINAL_FAILURE.

### Destroy
- Destroy requires joined state, then locks only for the stopped/empty/zero-retained safety snapshot. It releases the lock before closing the wake handle, deleting the critical section, and clearing initialized state.
- A close failure preserves initialized state and wake handle for retry.
- Because the final close/delete/clear occurs outside the Worker lock, concurrent Destroy or Join/Destroy is unsafe. There is no internal lifecycle mutex/atomic claim that serializes these calls.

### Required serialization boundary
- `reaper-managed-worker-v2.h` explicitly documents: caller serializes Join/Destroy; failed wait/query/close preserves thread HANDLE.
- `raya-orchestrator-v1.h` requires one serialized cleanup owner. Its cleanup cursor uses IN_PROGRESS to reject a second operation on the same step, and predecessor steps gate Join/Destroy. The owner token and registry close protocol are the external serialization boundary; no component call is made while the Registry lock is held.
- The orchestrator's worker teardown path calls shutdown, join, and destroy in separate ordered steps. The caller must retain exclusive cleanup ownership until all context use ends.
- This is a contract, not an internal defense: direct callers that concurrently invoke Join/Destroy (or race Destroy against any other Worker API) violate the documented contract. No test should treat such use as supported.

## Targeted test execution

All scripts used existing build commands, MSVC 2026 Build Tools 18.7.3 x64, and the scripts' existing /W4 /WX flags. All three returned exit code 0.

1. `build-reaper-managed-worker-v2-lifecycle-seam-test.cmd`
   - 10 runs × 29 assertions = 290 assertions.
   - Covers retry preservation for timeout, WAIT_FAILED, GetExitCodeThread failure, thread HANDLE close failure; abnormal exit after successful handle reap; wake-handle close retry.
   - 0 failures.

2. `build-reaper-integrated-shutdown-concurrent-test.cmd`
   - 25 runs × 23 assertions = 575 assertions.
   - Covers admission/handoff race with shutdown: already admitted caller can hand off, late admission is rejected, queued operation drains, and integrated reaper reaches STOPPED.
   - 0 failures.

3. `build-managed-pipe-io-v2-concurrent-shutdown-test.cmd`
   - 10 runs × 583 assertions = 5,830 assertions.
   - 70 concurrent producers per run; all 700 submitted operations handed off and all 700 callbacks completed across the ten runs, with zero callback errors.
   - Covers shutdown while callers are admitted/in cancellation, preserving queued/registry/reservation/outstanding accounting until callbacks and worker drain complete.
   - 0 failures.

Total: 6,695 assertions across 45 executable runs; all passed.

## Artifact and disk checks

- Exact expected executable/object outputs were checked before the first build and absent.
- After each script, all exact expected executable/object outputs were checked; zero residual files remained. No unrelated files were deleted.
- Free space before: C: 13,777,756,160 bytes; D: 4,811,296,768 bytes.
- Free space after: C: 13,777,666,048 bytes; D: 4,811,296,768 bytes.
- No persistent test artifacts were created.

## Conclusion / residual risk

Shutdown admission/handoff synchronization is supported by repeated concurrent tests. Join/Destroy failure-retry semantics are covered by the lifecycle seam test. The orchestrator supplies an exclusive cleanup-owner/cursor protocol for ordered lifecycle work.

However, Join and Destroy are intentionally not safe for concurrent direct invocation; Destroy also cannot race any other Worker API. This relies on caller serialization and is not enforced by a Worker-internal lifecycle lock. Direct API consumers must honor the documented contract. No concurrent Join-vs-Join, Destroy-vs-Destroy, or Destroy-vs-API test was run because those calls are explicitly unsupported, and deliberately exercising them would be invalid as a correctness test.

Stage 5.8 review complete. No code changes.
