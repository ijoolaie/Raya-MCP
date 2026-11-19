# Managed Worker independent wake observation — timeout prototype — 2026-11-16

## Objective
Validate that the Managed Worker can independently observe a sticky DEGRADED state after the wake-event SetEvent call fails, without receiving a second signal. This is a test-only prototype, not a production fix.

## Changes in Working Copy
1. `reaper-managed-worker-v2.c`
   - Under `RAYA_ADAPTER_TESTING` only, the wait interval is 100 ms instead of INFINITE.
   - When wait-plan resolution returns `RAYA_WAITPLAN_TIMEOUT`, the Worker checks lifecycle while holding `worker->lock`.
   - If DEGRADED, it exits fail-closed with `ERROR_INVALID_STATE`.
   - If not degraded, it releases the lock and rebuilds the wait plan.
   - Timeout never resolves an operation and never calls GetOverlappedResult, collection, finalization, release, ACK, or callback.
   - With `RAYA_ADAPTER_TESTING` absent, the compiled wait remains INFINITE.
2. `integrated-submit-adapter-v2-api-test.c`
   - In the real named-pipe overlapped-I/O wake-failure test, disabled the test-only second SetEvent signal.
   - The Worker must now exit within 1,000 ms after the failed wake; the test also asserts exactly one injected Worker wake SetEvent call.
   - The existing quarantine hook publishes the transferred operation in Adapter Retained before the Worker can cross the Worker-lock boundary.

## Test evidence
Toolchain: Visual Studio 2026 Build Tools 18.7.3 x64, `/W4 /WX /TC`.

### Test-enabled build
- `RAYA_ADAPTER_TESTING` defined.
- Build succeeded.
- 10/10 complete suite runs passed.
- 858 assertions / 68 cases per run; 8,580 assertions total.
- The targeted test uses a real named-pipe overlapped ReadFile, observes the injected SetEvent failure, provides no second signal, and observes Worker exit within the 1,000 ms test bound.

### Baseline build
- `RAYA_ADAPTER_TESTING` absent.
- Build succeeded.
- 10/10 complete suite runs passed.
- 724 assertions / 66 cases per run; 7,240 assertions total.
- This compile verifies the test-only finite timeout is excluded and the normal Worker wait remains INFINITE.

### Safety assertions retained by the targeted test
- Exactly one failed Worker wake SetEvent call.
- Worker exits with `ERROR_INVALID_STATE` after timeout observes DEGRADED.
- Core queue/outstanding/reserved remain 1/1/1; queued operation remains QUEUED.
- Registry remains empty; no operation was promoted/claimed/collected.
- Adapter Retained contains the operation; Worker Retained remains empty.
- Submitter remains RUNNING with one issuer lease; no ACK or callback.
- Operation buffer, issuing pipe, and completion event remain valid.
- The test does not signal the operation completion event and does not release the in-flight operation or its resources.

## Interpretation
This demonstrates that a finite wait can independently observe sticky DEGRADED state after a missed wake signal. It does not recover the Worker, complete the overlapped I/O, or repair the lost wake. It is not evidence that production quarantine or recovery is implemented.

The 100 ms value is a test parameter, not an approved production interval. A production design would need a latency/idle-wakeup tradeoff review and additional timing and load tests.

## Files changed
- `reaper-managed-worker-v2.c`
- `integrated-submit-adapter-v2-api-test.c`
- This report.

All edits are in the Working Copy:
`D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`

No source project or Production changes. No commits, resets, or TLC runs. No pre-existing project evidence or object files were deleted. Build outputs were isolated in two unique temporary directories and those exact directories were removed after testing.
