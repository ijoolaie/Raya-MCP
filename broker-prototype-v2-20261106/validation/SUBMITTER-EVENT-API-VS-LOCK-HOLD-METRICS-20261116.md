# Submitter Event API versus critical-section hold time — 2026-11-16

## Method

A temporary copy of the working-copy sources was instrumented; no project source/header was edited. In the temporary `dedicated-io-submitter-v2.c`, calls to the Submitter API function pointers `set_event` and `reset_event` were routed through timing wrappers that called the original function exactly once with the same handle and returned its result unchanged. QPC measured each callback duration. For every Submitter critical-section interval, the harness also accumulated the elapsed time spent inside those API callbacks and calculated the residual hold time (`total hold - API elapsed`). Lock wait and total hold were measured independently. No additional lock was taken by the instrumentation.

Two x64 binaries were built with VS 2026 Build Tools 18.7.3, `/W4 /WX /TC`: finite test-only Worker wait (`RAYA_ADAPTER_TESTING`, 100 ms) and INFINITE control. The temporary integration workload used 14 producers × 5 submissions, 70 live operations. Five alternating executions per mode; all 10 passed 830 assertions, 70 operations and 14 producer exits (8,300 assertions total).

## Results

Nanoseconds; p50/p95 use nearest-rank. `EVENT_API_PER_HOLD` is the sum of all wrapped SetEvent/ResetEvent durations inside one Submitter lock interval. `NON_API_HOLD` is the remaining elapsed time within that interval.

| Metric | Finite p50 | Finite p95 | Finite max | INFINITE p50 | INFINITE p95 | INFINITE max | Samples per mode |
|---|---:|---:|---:|---:|---:|---:|---:|
| Event API call | 700 | 28,200 | 8,260,600 | 800 | 26,700 | 379,500 | 2,246 / 2,248 |
| Submitter hold | 600 | 31,300 | 8,263,200 | 600 | 30,800 | 31,399,000 | 2,256 / 2,258 |
| Event API time per hold | 400 | 29,800 | 8,261,600 | 400 | 29,000 | 380,500 | 2,256 / 2,258 |
| Non-API time per hold | 200 | 1,200 | 76,000 | 200 | 1,100 | 31,375,100 | 2,256 / 2,258 |
| Submitter lock wait | 100 | 40,800 | 8,287,400 | 100 | 37,800 | 859,200 | 2,256 / 2,258 |

## Interpretation

- In this workload, the p95 of Event API elapsed time per lock interval (29.0–29.8 μs) accounts for most of the p95 total Submitter lock hold (30.8–31.3 μs). The non-API remainder p95 is 1.1–1.2 μs. This strongly indicates that elapsed time in `set_event`/`reset_event` calls is the dominant component of the upper-tail Submitter lock hold distribution for this specific workload.
- The source audit confirms these calls occur while the Submitter lock is held in enqueue, abort-admission, acknowledge, shutdown, worker-loop and degradation paths.
- Maxima are scheduler-sensitive outliers: for example, the INFINITE run's 31.399 ms maximum hold is almost entirely a 31.375 ms non-API interval. QPC measures wall elapsed time, not CPU execution time; a thread can be preempted while holding the lock. Do not interpret maxima as API execution cost.
- Finite and INFINITE distributions overlap. No reliable evidence here that the Worker wait mode changes Submitter lock behavior.
- Wrappers add QPC overhead and the test is one host/workload with five runs per mode. These measurements are diagnostic, not production performance certification.

## Engineering implication

If a future change is considered, first evaluate a carefully specified design to move event signaling outside the Submitter critical section without creating lost-wakeup races or changing the ordering/atomicity of state publication. In particular, `degrade_locked` currently signals both events while state is DEGRADED; a refactor must preserve reliable wakeup/notification even when one signal fails. Do not simply move calls out of the lock based on this measurement. No code change is recommended or performed here.

## Cleanup and safeguards

- Removed only the owned temporary instrumented source/build directory: 148 files, 1,097,259 bytes.
- No original project source/header/build output, report, TLC state store, archive or prior evidence was deleted.
- No commit/reset; no Production/source-project edits; no TLC run.