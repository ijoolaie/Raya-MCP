# Managed Worker finite-wait impact assessment — 2026-11-16

## Scope

Assessed the existing test-only 100 ms finite wait prototype. No production wait interval was changed. The non-test build continues to call `WaitForMultipleObjects(..., INFINITE)`; the finite timeout branch remains guarded by `RAYA_ADAPTER_TESTING`.

## Multi-operation regression

Built the existing `integrated-submit-adapter-v2-multi-native-test.c` with VS 2026 Build Tools 18.7.3 x64, `/W4 /WX /TC`, and `RAYA_ADAPTER_TESTING`, in a unique temporary directory. Ten consecutive executions passed:

- 809 assertions per run; 8,090 assertions total.
- 70 integrated live operations per run.
- 7 producer threads exited per run.
- Completion window exceeded 63 per run.
- Exit code 0 on all 10 runs.
- Runtime range 771–980 ms; median 886.5 ms.

This exercises the finite-wait test build under concurrent multi-operation load. It is not a throughput benchmark against a matched INFINITE-wait binary.

## Idle wakeup and CPU implications

A 100 ms timeout creates a maximum nominal polling cadence of 10 timeout wakeups per second for an otherwise idle worker, assuming each wait expires. Each timeout path reacquires the Worker lock, checks the sticky lifecycle state, unlocks, and rebuilds the wait plan. No operation completion, `GetOverlappedResult`, collection, finalizer, release, ACK, or callback is performed on timeout.

The run above does not isolate idle CPU consumption. No defensible CPU percentage or CPU-time delta was measured, so none is claimed. A proper CPU comparison requires a dedicated idle-worker harness, equal-duration INFINITE and finite-wait builds, process kernel/user CPU-time deltas, and repeated trials on the same host.

## Detection latency

The previous real named-pipe regression proves the worker observes sticky DEGRADED without a second wake signal and exits fail-closed; its test currently uses a 1,000 ms upper-bound wait. This assessment did not instrument the exact interval from failed `SetEvent` to worker exit, so it does not claim measured median/p95 latency. Under an ideal scheduler, a 100 ms periodic wait bounds the wait-expiry component to approximately 100 ms, but scheduler delay and lock contention add to that.

## Decision

- Keep 100 ms strictly test-only.
- Do not select a production polling interval from these results.
- Before production consideration, measure idle CPU cost and exact degraded-detection latency (at least 30 trials, reporting min/median/p95/max), then compare normal multi-operation throughput and completion latency against an INFINITE-wait control.
- The timeout path is observation only, not recovery; the worker remains fail-closed and queued operations remain retained/owned according to the existing tested state.

## Safety and cleanup

- Build and executable were isolated in a unique `%TEMP%` directory.
- Removed only that directory after all 10 runs: 15 files, 288,789 bytes.
- No project source, report, existing object, or prior evidence file was deleted or modified by cleanup.
- No commit/reset, no Production change, no TLC run.