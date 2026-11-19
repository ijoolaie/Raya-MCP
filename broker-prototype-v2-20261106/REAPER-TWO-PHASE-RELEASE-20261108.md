# Reaper two-phase terminal/release accounting — 2026-11-08

## Why this change was required

Source review found that the existing `raya_reaper_worker_terminal()` immediately decremented `core.outstanding` and changed the operation to RELEASED as soon as I/O completion was collected. The new HANDLE finalizer runs after terminal collection and can fail partially. If outstanding accounting is already released before CloseHandle succeeds, shutdown could incorrectly consider the worker drained while operation resources remain open or uncertain.

## Changes

- Added `RAYA_REAPER_OP_TERMINAL_HELD` as a distinct operation state.
- Added `raya_reaper_worker_terminal_hold()`: records positively collected terminal I/O without decrementing outstanding ownership.
- Added `raya_reaper_worker_release()`: decrements outstanding and changes the op to RELEASED only after the caller has completed resource finalization.
- Added `RAYA_INTEGRATED_RESERVATION_TERMINAL_HELD`.
- Added `raya_reaper_integrated_terminal_hold()`: validates exact registry op/event pairing, removes the terminally collected operation from the wait registry, and keeps its reservation/outstanding count held.
- Added `raya_reaper_integrated_release()`: commits the final accounting transition only after successful resource closure. A recorded HANDLE_CLOSE cause blocks release without overwriting that root cause.
- Kept the existing `raya_reaper_worker_terminal()` and `raya_reaper_integrated_collect()` compatibility paths for prior synthetic tests. These legacy helpers perform immediate release and MUST NOT be used by the future real resource-owning Worker. Runtime integration must use the staged terminal-hold -> finalizer -> release sequence.

## Required worker sequence

1. Under the shared lock, resolve and claim the exact registry operation/event pair.
2. Outside the lock, call GetOverlappedResult using the exact issuing pipe HANDLE and OVERLAPPED, with a positively observed signaled dedicated event.
3. Under the lock, record terminal collection using `raya_reaper_integrated_terminal_hold()`. The operation leaves the wait registry but remains outstanding and reserved.
4. Outside the lock, close the duplicated issuing pipe and event through the finalizer.
5. If either close fails, under the lock record `RAYA_RECOVERY_CAUSE_HANDLE_CLOSE`; retain the operation record, OVERLAPPED, buffer and all unresolved ownership; do not call release and do not claim clean shutdown.
6. If both closes succeed, commit finalizer release exactly once, then under the lock call `raya_reaper_integrated_release()`. Only then may the owner free the operation record, OVERLAPPED and private buffer exactly once.
7. Worker exit remains blocked while any terminal-held operation remains outstanding.

No callback that may block or call CloseHandle may execute while holding the Reaper lock. The integrating worker must ensure the operation record remains pinned while the lock is dropped between terminal hold and release.

## Verification

New test: `reaper-two-phase-release-test.c`, built with MSVC x64 `/W4 /WX`.
- Successful finalizer: terminal-held operation remains outstanding until both handles close and release commits; 25/25 runs passed, 43 assertions/run (1,075 assertions).
- Partial close failure: first close succeeds, second fails; HANDLE_CLOSE is sticky, release is rejected, outstanding/reservation remain held, and clean exit is false; covered in the same 43 assertions/run.

Regression checks all passed:
- Core: 34 checks.
- Integrated basic: 25 assertions.
- Integrated concurrency: 541 assertions, 256/256 handoffs and collections.
- Live named-pipe integrated harness: 545 assertions, 70 success + 1 aborted, zero worker error.
- Worker: 16 checks.
- Sync: 17 checks.

## Limits

This change adds the accounting seam, not the lifecycle-managed production Worker. Existing test harnesses still use compatibility immediate-release helpers; they do not exercise the complete finalizer sequence for every native operation. The two-phase test uses synthetic handles for close fault injection. The finalizer is not yet connected to the real named-pipe worker or `pipe-io-v2.c`. The full 28-group native acceptance matrix remains mandatory. Polling recovery remains disabled. Production remains NO-GO.
