# Native WAIT_FAILED Polling Recovery Test — 2026-11-07

## Purpose
Verify a safe polling-only recovery path after a real `WaitForMultipleObjects` failure, using a real overlapped named-pipe read. This is an isolated single-worker test harness, not integration into the existing production-candidate Worker and not a test of `pipe-io-v2.c`.

## Scenario
1. Create a byte-type named pipe and issue one overlapped server-side `ReadFile`; submission returns `ERROR_IO_PENDING`.
2. Admit, reserve, hand off, and promote the operation into Integrated Core + Registry.
3. Build a real WaitPlan with a disposable wake event and the operation's dedicated event.
4. Close only the wake event. The operation event and pipe HANDLE remain valid.
5. Call native `WaitForMultipleObjects`; it returns `WAIT_FAILED`. Capture `GetLastError()` immediately; it is `ERROR_INVALID_HANDLE`.
6. Mark Core `DEGRADED`, verify the operation remains registered/outstanding/reserved, and verify its event is still unsignaled.
7. The peer writes one byte. The test's polling-only loop probes the registered operation event with zero-time `WaitForSingleObject`.
8. Once signaled, resolve the exact operation/event pair, claim it, and collect the real result using `GetOverlappedResult` with the exact issuing server HANDLE and OVERLAPPED.
9. Only after successful completion collection does Integrated collect release the Registry entry and reservation.

## Results
- MSVC x64 `/W4 /WX`: PASS.
- Independent process runs: 25/25 PASS.
- Assertions: 34 per run; 850 total.
- Failures: 0.
- Native wait failure: `WAIT_FAILED / ERROR_INVALID_HANDLE (6)`.
- Native overlapped I/O: `ERROR_IO_PENDING` followed by successful `GetOverlappedResult`, exactly one byte with matching payload.
- Polling loop found the signaled operation on the first post-write probe in each run.
- After terminal collection: Registry=0, outstanding=0, reserved_slots=0, operation RELEASED.
- Lifecycle remains `DEGRADED`; `can_exit` remains false. The test intentionally does not claim a clean shutdown after a wait-set integrity failure.
- Build script removes executable and object files.

## Safety properties demonstrated
- No operation event or pipe handle is closed to manufacture the wait failure.
- No completion resources are released while the operation is still pending.
- Polling probes only the registered event; completion is accepted only after event signal and a successful exact-pair `GetOverlappedResult`.
- A successful terminal collection does not erase the sticky degraded lifecycle or imply that the wait subsystem is healthy.

## Limitations
- Single operation, single test thread, and deterministic peer write; no multi-producer/Worker stress.
- The polling loop is test harness code; it is not yet wired into a lifecycle-managed Reaper Worker.
- No bounded rotation across 256 entries, wait-set rebuild, wake-event recovery, or fairness test in this harness.
- No event/result inconsistency, cancellation, disconnect, shutdown race, CloseHandle failure, IOCP, or full 28-group matrix.
- `pipe-io-v2.c` is unchanged and remains unaccepted.

## Gate
This demonstrates a narrow native recovery behavior only. The production Reaper lifecycle and 28-group acceptance gate remain incomplete. Production remains **NO-GO**.
