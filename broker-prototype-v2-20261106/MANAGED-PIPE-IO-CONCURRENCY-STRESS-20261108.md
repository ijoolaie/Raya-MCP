# Managed Pipe I/O Concurrency Stress — 2026-11-08

## Test A — 70-producer shutdown/handoff barrier

Files:
- `managed-pipe-io-v2-concurrent-shutdown-test.c`
- `build-managed-pipe-io-v2-concurrent-shutdown-test.cmd`

The harness creates 70 independent overlapped byte-mode named pipes and 70
producer threads. Each producer submits one pending read through the managed
wrapper. An injected cancellation seam records that the exact I/O is still
`ERROR_IO_INCOMPLETE`, then blocks all producers at the cancellation boundary.

The coordinator waits until all 70 operations have acquired admission and
reservation, transitions the Worker to `STOPPING`, and only then releases
all producers to complete the bounded drain and hand off their operations.
Every producer must report `HANDED_OFF`. The Worker must retain all 70
outstanding/reserved operations across the transition. The test then writes
one byte to each peer, requires exactly 70 successful callbacks, closes the
original server handles after completion, releases the issuing threads, joins
the Worker, and destroys it cleanly.

MSVC x64 `/W4 /WX`:
- 583 assertions per process × 10 runs = **5,830 assertions passed**.
- 70/70 producers reached the pending-I/O barrier on every run.
- 70/70 operations handed off after shutdown began.
- 70/70 callbacks succeeded; zero callback errors.
- Snapshot after handoff: caller_active=0, outstanding=70, reserved=70,
  queued + registry=70.
- Worker reached clean STOPPED and was destroyed on every run.

The issuing producer threads are deliberately held alive until callbacks
complete. This is required by the behavior characterized in Test B.

## Test B — issuing-thread exit characterization

Files:
- `managed-pipe-io-v2-thread-exit-test.c`
- `build-managed-pipe-io-v2-thread-exit-test.cmd`

A producer thread submits one pending read, receives `HANDED_OFF`, and exits
immediately. Its cancellation seam returns `ERROR_NOT_FOUND` without calling
`CancelIoEx`. The test does not send peer data. The Worker receives a terminal
completion and the callback reports `ERROR_OPERATION_ABORTED` with zero bytes.

MSVC x64 `/W4 /WX`:
- 18 assertions per process × 10 runs = **180 assertions passed**.
- `ERROR_OPERATION_ABORTED` observed in 10/10 runs.
- Before the producer exits, and immediately after the injected cancel seam,
  `GetOverlappedResult(FALSE)` reports `ERROR_IO_INCOMPLETE`.
- The test contains no OS cancellation call; the injected seam only sets
  `ERROR_NOT_FOUND` and returns FALSE.

## Engineering conclusion

The Worker ownership handoff and shutdown race passed when issuing threads
remained alive. However, handing an operation to the Worker does **not** make
the underlying Windows overlapped request independent of the thread that
issued it. Immediate issuer-thread exit consistently resulted in
`ERROR_OPERATION_ABORTED`.

This is a production-blocking API/lifecycle constraint. The current wrapper
must not be used by arbitrary short-lived producer or thread-pool threads.
Before production, redesign submission so overlapped I/O is issued by a
dedicated long-lived submission thread (or another proven mechanism that
preserves issuer lifetime), and add acceptance tests for issuer shutdown,
submission queue drain, cancellation, and worker shutdown. Merely keeping a
callback context or `OVERLAPPED` alive is insufficient.

## Status

- Shutdown/handoff stress: PASS under explicit issuer-thread lifetime.
- Issuer-thread exit: reproducible cancellation; design blocker.
- Production broker: **NO-GO**.
- Legacy `pipe-io-v2.c`, Production, service provisioning, tunnel, and
  IranPriceScout were not modified.
