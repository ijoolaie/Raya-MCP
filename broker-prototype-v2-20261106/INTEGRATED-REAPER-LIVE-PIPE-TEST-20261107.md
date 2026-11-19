# Integrated Reaper — Live Named-Pipe Overlapped I/O Test (2026-11-07)

## Scope

Added a disposable runtime harness that connects the Integrated Reaper Core + Registry + WaitPlan to actual Windows byte-type named-pipe overlapped reads. The harness uses real Win32 `ReadFile`, `WaitForMultipleObjects`, `GetOverlappedResult`, `CancelIoEx`, named-pipe peer writes, and peer disconnect.

Files:
- `reaper-integrated-live-pipe-test.c`
- `build-reaper-integrated-live-pipe-test.cmd`

The test is isolated under `broker-prototype-v2-20261106`. It does not call or link `pipe-io-v2.c`, does not change the production transport, and does not provision a service or protected root.

## Runtime design

- Disposable local byte-type named pipe; server opened with `FILE_FLAG_OVERLAPPED`, synchronous client peer.
- 71 distinct pending server-side reads, each with its own OVERLAPPED structure and manual-reset event.
- The event stored in `OVERLAPPED.hEvent` has the low bit set; the Registry stores the raw event HANDLE.
- Each operation passes through Integrated admission, op-bound reservation, handoff, queue promotion, Registry membership, WaitPlan construction, exact op/event resolution, claim, and terminal accounting.
- One worker waits on the rotating WaitPlan (wake + at most 63 operation events) and calls `GetOverlappedResult` outside the ownership lock, using the exact issuing pipe HANDLE and OVERLAPPED pointer.
- 70 reads complete from real peer-written bytes.
- The 71st read is kept pending past a 50 ms test deadline, then cancelled with native `CancelIoEx`; its signaled event and exact result are collected as `ERROR_OPERATION_ABORTED`, and ownership is released only after collection.
- A separate disconnect case closes the peer while a read is pending. The signaled operation returns a failed result outside the narrow success/aborted classifier. It is deliberately classified UNCERTAIN; the Integrated state becomes DEGRADED and Registry, outstanding count, reservation, operation and event ownership are retained. The test does not falsely claim clean shutdown for this case.

## Results

- MSVC x64 `/W4 /WX`: PASS.
- 11 independent process runs after adding the native CancelIoEx-after-completion race check (the first run plus 10 repeats).
- 543 assertions per run; 0 failures.
- 5,973 assertions total across the 11 runs.
- Every run: 70 terminal-success reads, 1 terminal-aborted cancellation, native CancelIoEx on an already-collected operation returns FALSE/ERROR_NOT_FOUND, disconnect UNCERTAIN retention assertions pass, worker error 0 for the clean-drain case.
- Clean-drain path: Registry=0, outstanding=0, queued=0, caller_active=0, reserved_slots=0, Integrated can-exit true, then STOPPED.
- Build command exit code 0 for all 10 runs; no stderr.
- Build executable and object outputs created by this command are removed by its cleanup step.

## What this proves

This is the first runtime exercise of the Integrated Registry/WaitPlan ownership path against real named-pipe overlapped I/O completion events, including exact-handle result collection, native cancellation, disconnect uncertainty retention, >63-event scheduling, and drained shutdown.

## What this does NOT prove

- It is not a runtime test of `pipe-io-v2.c`; that implementation remains compile-only and still has the previously documented unsafe cleanup paths after uncertain `GetOverlappedResult` failures.
- It does not implement the lifecycle-managed production Reaper API, finite-poll recovery for all wake failures, a hardened shutdown coordinator, or cancellation ownership handoff from `one_io`.
- It does not inject `WaitForMultipleObjects` / `SetEvent` / `ResetEvent` failures into this live harness.
- It does not validate all 28 acceptance groups in `CANCELLATION-REAPER-DESIGN-20261106.md`.
- It does not establish supported Windows-version coverage or production security.

## Gate

The harness is test-only evidence. Do not connect it to Production and do not modify `pipe-io-v2.c` until the full 28-group runtime acceptance matrix passes and the production Reaper lifecycle/ownership integration is separately reviewed.

No Production, tunnel, or IranPriceScout changes. No service/root provisioning. No commit/reset. Production remains **NO-GO**.
