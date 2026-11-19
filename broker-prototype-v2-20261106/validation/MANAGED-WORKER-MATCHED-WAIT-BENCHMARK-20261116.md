# Managed Worker matched wait-mode benchmark and idle CPU follow-up — 2026-11-16

## Build and test setup

Two binaries were built from the same working-copy sources with VS 2026 Build Tools 18.7.3 x64, `/W4 /WX /TC`, in isolated temporary directories. The only intended compile-time difference was `RAYA_ADAPTER_TESTING`: enabled binary uses the test-only 100 ms wait; disabled binary uses the production `INFINITE` wait. No project source was edited for this benchmark.

## Matched multi-operation benchmark

Ran the existing `integrated-submit-adapter-v2-multi-native-test` 20 times per binary, alternating finite and INFINITE runs (40 total). All 40 runs passed with exit code 0.

| Metric | 100 ms finite | INFINITE |
|---|---:|---:|
| Runs | 20 | 20 |
| Minimum runtime | 835 ms | 805 ms |
| Median runtime | 995 ms | 987 ms |
| p95 (nearest-rank) | 1,070 ms | 1,161 ms |
| Maximum runtime | 1,308 ms | 1,202 ms |

Each run executed the same 809 assertions, 70 integrated live operations and 7 exited producers. The median difference is +8 ms (~0.8%) for finite wait. Runtime spread and sample size do not support attributing that small difference to the timeout; p95 and max are not consistently worse for finite mode. Treat this as a smoke-level matched benchmark, not a statistically powered throughput/latency conclusion.

## Longer idle CPU sampling (still inconclusive)

Built the minimal idle Worker harness in both modes and ran two 20-second samples per mode, interleaved finite, INFINITE, INFINITE, finite. All four workers shut down, joined and destroyed successfully. `GetProcessTimes` reported 0 ms kernel + user CPU delta in all four 20-second windows. This is not credible evidence of literally zero CPU; the host's process CPU accounting resolution/behavior is too coarse for this very low-activity process. A reliable CPU conclusion needs thread-level ETW/WPA or another higher-resolution profiler. No CPU overhead percentage is claimed.

## Interpretation and decision

- The finite-wait branch preserves the existing 70-operation multi-native regression under the same workload.
- The observed median runtime difference is small and inconclusive; no measurable regression or improvement can be attributed to the wait interval.
- Idle CPU cost remains unquantified. The periodic timeout still nominally wakes an idle worker up to 10 times/second.
- Keep 100 ms test-only. Do not approve a production timeout interval from these data.
- Prior 30-trial real named-pipe wake-failure instrumentation remains the available latency evidence: min 40.898 ms, median 47.697 ms, p95 62.959 ms, max 63.542 ms. It measures failure-to-thread-signaled observation including scheduling/test overhead, not a worst-case guarantee.
- Timeout remains observation-only; no recovery, collection, ACK, callback or release occurs on timeout.

## Cleanup and safeguards

- Removed only two temporary benchmark directories: 32 files, 864,431 bytes total.
- No source files, prior reports, pre-existing object/executable outputs, state stores or validation evidence were deleted.
- No commit/reset; no Production or source-project changes; no TLC run.