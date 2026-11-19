# Integrated Reaper Race and Wait-Window Acceptance — 2026-11-07

## Added test assets
- `reaper-integrated-reset-enqueue-race-test.c`
- `build-reaper-integrated-reset-enqueue-race-test.cmd`
- `reaper-waitplan-256-fairness-test.c`
- `build-reaper-waitplan-256-fairness-test.cmd`

## ResetEvent / enqueue race
Harness uses four real producer threads, one real worker thread, a shared `CRITICAL_SECTION`, one manual-reset wake event, 256 distinct operation events and the integrated Core/Registry state.

The worker performs `ResetEvent`, queue promotion, Registry snapshot and exit-predicate inspection while holding the same lock used by producers. Producers perform admit/reserve/handoff and `SetEvent(wake)` under that lock, then signal their operation completion event outside it. A start gate releases all threads together. The worker scans retained Registry entries and collects only signaled test events.

Results:
- MSVC x64 `/W4 /WX`: PASS.
- 25 independent process runs.
- 542 assertions per run; 0 failures.
- Every run: 256 handoffs, 256 promotions, 256 terminal collections.
- 13,550 assertions total.
- Wake reset count varied from 3 to 54 between runs, exercising differing producer/worker interleavings.
- All operation events, thread handles, gate/wake events and the critical section were cleaned up after successful drain.

This exercises the shared-lock lost-wake protocol under real concurrent scheduling. It is not a formal proof over all possible schedules and does not inject a failing `SetEvent` or `ResetEvent`.

## 256-operation WaitPlan fairness
Harness creates 256 distinct real manual-reset operation events, registers all of them, signals them, then uses actual `WaitForMultipleObjects` and WaitPlan resolution to drain each rotating window. Every returned wait slot is resolved to the exact original operation/event pair; each event is reset after observation.

Results:
- MSVC x64 `/W4 /WX`: PASS.
- 10 independent process runs.
- 2,067 assertions per run; 0 failures.
- Every run observed all 256 operations exactly once across 5 rotating windows.
- 20,670 assertions total.
- All event handles closed.

## Important implementation observation
The legacy `reaper-sync-v2.c` FIFO API is not the integrated Reaper path. In `raya_reaper_sync_handoff`, Core handoff can succeed and transfer ownership before `SetEvent` is attempted. If `SetEvent` fails, the function returns false even though ownership has already transferred and the core is marked DEGRADED. The Boolean return therefore conflates “handoff did not occur” with “handoff occurred but wake signaling failed.” A caller that treats false as proof it still owns the operation could incorrectly release memory still queued to the worker.

Do not use this legacy Boolean as an ownership oracle. Before any integration, replace or supplement it with an unambiguous outcome contract that distinguishes NOT_TRANSFERRED from TRANSFERRED_DEGRADED (or an equivalent explicit ownership result), and test the caller's behavior. No changes to this legacy API were made in this step.

## Limits and gate
- Operation completion is simulated with manual-reset events, not real overlapped I/O.
- No forced Win32 API failure injection was performed.
- Shutdown racing an actively admitted producer remains a separate test.
- Abandoned pre-handoff reservation cancellation remains unresolved.
- Full 28-group named-pipe acceptance matrix remains outstanding.
- No changes to `pipe-io-v2.c`, Production transport, service provisioning, tunnel, or IranPriceScout.

Production remains **NO-GO**. No commit/reset.
