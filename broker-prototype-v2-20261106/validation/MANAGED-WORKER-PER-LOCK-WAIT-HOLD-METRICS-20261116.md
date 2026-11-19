# Managed Worker per-lock wait and hold-time measurement â€” 2026-11-16

## Method

A temporary copy of the working-copy sources was instrumented; no project source/header was edited. Instrumentation wrapped `EnterCriticalSection` and `LeaveCriticalSection` for the Adapter, Managed Worker and Submitter critical sections. `QueryPerformanceCounter` captured time immediately before acquisition, immediately after acquisition, and immediately before release. Per-thread nesting was tracked with thread-local stacks. Samples were accumulated without taking another critical section and summarized after all test threads had exited.

The existing multi-native integration test was changed only in the temporary copy to use 14 producers Ã— 5 submissions (70 live operations). Two binaries were built with VS 2026 Build Tools 18.7.3 x64 `/W4 /WX /TC`: finite test-only 100 ms Worker wait (`RAYA_ADAPTER_TESTING`) and INFINITE control. Five alternating runs per mode; all ten passed 830 assertions, 70 live operations and 14 producer exits. Total 8,300 assertions.

## Results

Times are nanoseconds. p50 and p95 use nearest-rank selection. Sample counts are lock acquisitions for both WAIT and HOLD metrics.

| Lock / metric | Finite p50 | Finite p95 | Finite max | INFINITE p50 | INFINITE p95 | INFINITE max | Samples per mode |
|---|---:|---:|---:|---:|---:|---:|---:|
| Adapter wait | 100 | 700 | 43,700 | 100 | 1,000 | 32,100 | 2,130 |
| Adapter hold | 100 | 500 | 1,400 | 100 | 600 | 4,200 | 2,130 |
| Worker wait | 100 | 2,900 | 135,600 | 100 | 2,800 | 125,100 | 8,622 / 8,610 |
| Worker hold | 400 | 6,900 | 29,200 | 400 | 6,700 | 34,700 | 8,622 / 8,610 |
| Submitter wait | 100 | 2,400 | 119,800 | 100 | 2,700 | 148,600 | 6,513 / 6,471 |
| Submitter hold | 500 | 9,400 | 78,400 | 500 | 8,800 | 43,700 | 6,513 / 6,471 |

## Interpretation

- Median lock waits are at or near the instrumentation floor (100 ns) in both builds. Do not treat these medians as precise uncontended wait costs.
- Worker has the highest acquisition volume, as expected from its queue, wait-plan and operation-state transitions. Its p95 hold time is about 6.7â€“6.9 Î¼s in both modes; observed maximum was 29.2 Î¼s finite and 34.7 Î¼s INFINITE.
- Submitter has the largest p95 hold time (9.4 Î¼s finite, 8.8 Î¼s INFINITE) and the largest observed hold maximum (78.4 Î¼s finite, 43.7 Î¼s INFINITE). This is consistent with the source audit finding that some event API calls occur while Submitter lock is held, but does not prove those calls caused the maxima.
- Adapter lock hold times are short in this workload (p95 0.5â€“0.6 Î¼s); maximum waits show occasional scheduler/contention outliers.
- No consistent difference between finite and INFINITE modes appears. The measured differences are small, overlapping and not a reason to change the wait interval.
- Instrumentation adds QPC calls and thread-local bookkeeping to each lock operation, so absolute values are perturbed. Relative comparisons are more useful than interpreting the smallest values. Only one host, one workload and five runs per mode were sampled.
- This is not a starvation proof, whole-program lock-order proof, or production performance certification. No lock refactor is recommended from these results alone.

## Cleanup and safeguards

- Removed only the owned temporary instrumented source/build directory: 148 files, 1,097,259 bytes.
- No original project source/header/build output, report, TLC state store, archive or prior evidence was deleted.
- No commit/reset; no Production/source-project edits; no TLC run.