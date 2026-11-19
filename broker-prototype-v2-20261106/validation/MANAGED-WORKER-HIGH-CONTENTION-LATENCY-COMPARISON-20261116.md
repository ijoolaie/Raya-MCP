# Managed Worker high-contention submit-to-callback latency comparison — 2026-11-16

## Test design

A temporary copy of `integrated-submit-adapter-v2-multi-native-test.c` was instrumented with `QueryPerformanceCounter` at immediately before each Adapter Submit and at callback entry. The test workload was changed only in the temporary copy from 7 producers × 10 submissions to 14 producers × 5 submissions (70 operations total). The existing workload gates completion until all 70 operations have been submitted, then writes to the 70 peer pipes with randomized order/jitter. This increases concurrent admission contention while preserving the existing operation ownership and completion assertions.

Two binaries were compiled with VS 2026 Build Tools 18.7.3 x64 and `/W4 /WX /TC`; the finite build enables the test-only 100 ms Worker wait and the control build leaves the wait `INFINITE`. Runs were alternated.

## Results

20 runs per mode; every run passed 830 assertions, 70 live operations, and 14 producer-thread exits. This yields 1,400 samples per mode (70 operations × 20 runs). Each process reported all 70 callback samples.

| Submit-to-callback latency | Finite 100 ms | INFINITE |
|---|---:|---:|
| Samples | 1,400 | 1,400 |
| Minimum | 11.544 ms | 13.997 ms |
| Median | 218.720 ms | 231.603 ms |
| p95 (nearest rank) | 432.906 ms | 432.216 ms |
| Maximum | 490.531 ms | 501.083 ms |

The median is 12.883 ms lower for the finite-wait test build in this run set; p95 is effectively the same (0.690 ms higher for finite), and maximum is 10.552 ms lower. These results do not establish a timeout benefit. The measured interval intentionally includes the period between submission and the test's later peer writes, concurrent producer scheduling, and callback delivery; it is an end-to-end workload latency measure, not an isolated Worker wake-to-completion latency. Do not infer that finite waits improve latency.

## Interpretation

- The 14-producer stress variant passed all 20 runs in each mode, with no assertion failures or missing callbacks.
- The distributions overlap substantially. No reliable performance advantage or regression can be attributed to the 100 ms timeout.
- This test does not deliberately hold Worker/Adapter critical sections, so it is a concurrency/admission stress test, not a deterministic lock-contention or lock-wait measurement.
- CPU overhead remains unquantified; prior 20-second `GetProcessTimes` samples were below useful accounting resolution.
- Keep finite wait test-only. No production timeout approval, recovery behavior, or correctness change is implied.

## Cleanup and source integrity

- Removed only the owned temporary benchmark directory: 17 files, 484,389 bytes.
- Temporary failed build directory from the initial malformed instrumentation was separately removed: 14 files, 82,920 bytes.
- Project source was not edited by this benchmark. Source SHA256 at audit: `integrated-submit-adapter-v2-multi-native-test.c` and `reaper-managed-worker-v2.c` were read for integrity verification (values recorded in execution log, not copied into this report).
- No project evidence, reports, existing `.obj`/`.exe`, state stores or archives were deleted.
- No commit/reset, no Production/source-project edits, no TLC run.