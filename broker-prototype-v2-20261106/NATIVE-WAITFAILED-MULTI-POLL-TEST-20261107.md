# Multi-operation Polling Recovery after Native WAIT_FAILED — 2026-11-07

## Objective
Extend the single-operation native WAIT_FAILED polling probe to more than one native wait window, using actual overlapped named-pipe reads and the existing Integrated Core, Registry, WaitPlan, WaitSet, and rotating window primitives.

## Test topology and sequence
- 70 independent byte-type named-pipe instances; each has its own server/client HANDLE pair, dedicated manual-reset completion event, OVERLAPPED, and one-byte read buffer.
- All 70 server-side overlapped ReadFile submissions returned ERROR_IO_PENDING.
- Every operation was admitted, reserved, handed off, and promoted into Integrated Core + Registry before recovery.
- The initial WaitPlan contained 63 operation events plus one wake event (64 handles total). The test closed only that disposable wake event, then observed native WaitForMultipleObjects return WAIT_FAILED and captured ERROR_INVALID_HANDLE immediately.
- Core was marked DEGRADED. All 70 operations remained registered, outstanding, and reserved.
- Peer writes were issued in reverse operation order. Recovery then repeatedly built bounded WaitPlans using the existing Registry cursor and 63-operation rotating WaitSet.
- For each signaled event, the test resolved the exact operation/event pair, claimed that pair, called GetOverlappedResult with that operation's exact issuing server HANDLE and OVERLAPPED, checked the returned byte, and only then performed terminal Integrated collection.

## Results
- MSVC x64 /W4 /WX: PASS.
- Independent process runs: 10/10 PASS.
- Assertions per run: 1,206.
- Total assertions: 12,060; failures: 0.
- Operations completed and collected: 70/70 per run.
- Polling required two bounded WaitPlan windows per run, proving that the 63-operation limit and rotation reach entries beyond the first window.
- Final Registry count, Core outstanding count, and reserved slot count: all zero.
- All 70 operation states: RELEASED.
- Lifecycle remains DEGRADED and raya_reaper_integrated_can_exit remains false. Successful collection does not clear the sticky degraded state or assert clean shutdown.
- Build script removes the executable and all object files.

## Safety properties demonstrated
- Real native overlapped named-pipe I/O was used for every operation; this is not a synthetic event-only rotation test.
- The operation completion events and issuing pipe handles were not invalidated to manufacture WAIT_FAILED.
- No operation was claimed or released until its registered event was observed signaled and exact-pair GetOverlappedResult succeeded.
- Each operation has a distinct pipe instance, client, server, OVERLAPPED, and completion event.
- WaitPlan resolution checked the operation/event identity against the live Registry before collection.
- The 63-operation rotating window reached all 70 entries across two plans.

## Limitations
- This is a deterministic, single-threaded test harness. All peer writes are issued before the polling pass; it does not establish concurrent producer/worker fairness or latency bounds.
- The polling loop is not integrated into a lifecycle-managed Reaper Worker.
- It does not test wake-event recovery/replacement, concurrent Registry mutation, multi-producer admission races, cancellation, disconnect, event/result inconsistency, shutdown races, CloseHandle failures, IOCP, or the full 28-group acceptance matrix.
- It does not link or runtime-test pipe-io-v2.c.
- The test does not make the degraded lifecycle eligible for clean shutdown.

## Gate
This is stronger native evidence for multi-window polling-only completion recovery, but it is not production Worker acceptance. The 28-group runtime gate remains incomplete and Production remains NO-GO.