# Managed Worker idle thread-cycle measurement â€” 2026-11-16

## Method

To avoid the coarse process CPU accounting seen in `GetProcessTimes`, a temporary idle harness duplicated the actual Managed Worker thread handle and sampled `QueryThreadCycleTime` across a 30-second idle interval. Each trial allowed a 1-second settle period, sampled the worker thread cycle counter, waited 30 seconds, sampled again, then shut down, joined and destroyed the Worker. The test-only finite build used its existing 100 ms wait; the control build used `INFINITE`. Three interleaved samples per mode were collected in the order finite, INFINITE, INFINITE, finite, INFINITE, finite. No source code or timeout value was changed for this measurement.

## Results

| Trial | Mode | Worker thread cycles in 30 s |
|---:|---|---:|
| 1 | Finite 100 ms | 16,982,868 |
| 2 | INFINITE | 0 |
| 3 | INFINITE | 0 |
| 4 | Finite 100 ms | 17,296,579 |
| 5 | INFINITE | 0 |
| 6 | Finite 100 ms | 17,017,589 |

Finite mode median: 17,017,589 cycles/30 s (about 566,000 cycles/s). The three control samples each reported zero additional Worker-thread cycles over the 30-second interval. The finite values are consistent across trials; the control confirms the Worker remained asleep absent events to the precision of this counter.

At a nominal 10 timeout expirations per second, 30 seconds corresponds to about 300 timeout checks; the observed finite-mode total is approximately 57,000 Worker-thread cycles per nominal timeout. This is an approximate normalization, not a direct count of actual timeout expirations.

## Interpretation and limits

- Unlike the earlier process CPU samples, this thread-level cycle counter detects a small but nonzero cost for the 100 ms polling branch.
- Do not convert these cycle counts to CPU milliseconds or a CPU percentage without a validated effective frequency and independent profiler corroboration.
- The result quantifies only the idle Worker thread's execution cost on this host and build; it does not measure system-wide power, scheduler wake cost, multi-core utilization, or production workload impact.
- The periodic finite wait does incur overhead; the overhead is measurable but small in absolute cycle count. No production interval is approved by this test.
- Combined with matched throughput and stressed submit-to-callback results, there is no demonstrated performance advantage to finite wait. Its established benefit is fail-closed observation of a missed wake within a bounded polling interval in the test harness.

## Cleanup and safeguards

- Removed only the two owned temporary harness directories: 32 files, 778,294 bytes.
- All six harness processes exited normally after Worker shutdown/join/destroy.
- No source, report, prior test output, pre-existing object/executable, state store or archive was deleted.
- No commit/reset, no Production/source-project edits, no TLC run.