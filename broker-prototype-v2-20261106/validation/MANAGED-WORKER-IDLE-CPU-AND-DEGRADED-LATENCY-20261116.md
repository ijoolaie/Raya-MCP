# Managed Worker idle CPU and degraded-detection measurements — 2026-11-16

## Scope and isolation

All builds and harness files were created in unique `%TEMP%` directories. The production branch was not changed: without `RAYA_ADAPTER_TESTING`, the Worker continues to use `WaitForMultipleObjects(..., INFINITE)`. The 100 ms wait is test-only.

## Idle CPU comparison (exploratory, inconclusive)

Built a minimal idle Worker harness twice with VS 2026 Build Tools 18.7.3 x64 and `/W4 /WX /TC`: finite-wait test build (`RAYA_ADAPTER_TESTING`) and INFINITE-wait baseline. Each trial started an otherwise idle Managed Worker, allowed 1 second to settle, sampled process kernel/user CPU time over 10 seconds, then shut down and destroyed the idle Worker normally.

Interleaved trials: finite, INFINITE, INFINITE, finite, finite, INFINITE. All six processes exited cleanly.

- Finite 100 ms: process CPU deltas 0 ms, 15.625 ms, 0 ms.
- INFINITE baseline: 0 ms, 0 ms, 0 ms.

The Windows process CPU-time accounting on this host reported in 15.625 ms quanta. These samples are too coarse and too few to establish a meaningful CPU delta. They do not prove zero overhead. A higher-resolution/longer-duration measurement (preferably thread CPU accounting or ETW) is required before choosing a production interval.

Nominal timeout cadence remains 10 expirations/second for a continuously idle worker, if each wait runs to expiry. Each expiry performs a lifecycle check under the Worker lock and rebuilds the wait plan; it does not collect or release operations.

## Degraded detection latency (30 trials)

A temporary test-harness copy instrumented the instant the injected Worker wake `SetEvent` failed using `QueryPerformanceCounter`, then sampled immediately after the Worker thread became signaled. The real named-pipe overlapped-I/O test was executed in 30 separate processes. The measured interval includes scheduler and test-harness observation delay; it is not a pure wait-kernel latency measurement.

- Trials: 30/30 completed successfully.
- Minimum: 40.898 ms.
- Median: 47.697 ms.
- p95 (nearest-rank): 62.959 ms.
- Maximum: 63.542 ms.

All trials retained the test's safety assertions: a single injected wake failure; Worker exited fail-closed with `ERROR_INVALID_STATE`; queued/outstanding/reserved stayed 1/1/1; no operation was claimed or collected; no ACK or callback occurred; operation buffer, issuing pipe and completion event remained valid. The 100 ms timer is test-only.

The measured latency below 100 ms is consistent with a timeout that was already part-way through its interval when the injected failure occurred. It must not be read as a 100 ms worst-case guarantee under scheduler stalls or lock contention.

## Multi-operation load regression

The finite-wait build passed 10/10 executions of the existing multi-native test: 809 assertions/run (8,090 total), 70 integrated live operations and 7 exited producers per run. Runtime was 771–980 ms. This is a correctness regression, not a matched throughput benchmark.

## Decision

1. Keep finite wait strictly test-only.
2. Do not claim CPU overhead is negligible; current process CPU measurements are inconclusive.
3. Do not approve a production timeout interval from these tests.
4. Before production consideration, obtain higher-resolution idle CPU data and compare completion throughput/latency against an INFINITE control under the same load and host conditions.
5. The timeout is observation only. It does not recover DEGRADED state or release retained/in-flight operations.

## Cleanup and safeguards

- Removed only the temporary idle harness: 16 files, 389,947 bytes.
- Removed only the temporary latency harness: 16 files, 450,250 bytes.
- No project source, pre-existing object/executable, report, or prior validation evidence was deleted.
- No commit/reset, no Production/source-project change, no TLC run.