# Integrated Reaper Concurrent Producer/Worker Test — 2026-11-07

## Harness
Added `reaper-integrated-concurrency-test.c` and its build command.

- Four real producer threads, one real worker thread.
- 64 operations per producer; 256 total, matching Registry capacity.
- Each operation owns a distinct manual-reset completion event.
- One shared `CRITICAL_SECTION` serializes Core, integrated reservation state, Registry membership, WaitPlan build/resolve, claim and terminal accounting.
- Producers perform admit -> op-bound reserve -> handoff while holding the lock, then signal their simulated completion event.
- Worker resets the wake event under the same lock, promotes queued work into Registry, builds WaitPlan under lock, waits outside lock, resolves and claims the exact operation/event pair under lock, performs a test-only completion probe outside lock, then terminally collects under lock.
- Shutdown is requested after all producer threads join; worker drains all operations before exit.

## Results
MSVC `/W4 /WX` build succeeded. Ten independent process runs succeeded:
- 540 assertions per run, 0 failures.
- 256 handoffs and 256 terminal collections per run.
- 10/10 process runs passed (5,400 assertions total).
- All operation events, producer thread handles, worker handle and wake event closed; critical section deleted after successful drain.
- Build script removes generated executable and object files.

## What this establishes
This is the first actual multi-threaded test joining the integrated Core/Registry lifecycle with WaitPlan and one worker. It exercises concurrent producer admission/reservation/handoff, queue-to-registry promotion, event-to-operation mapping, claim, terminal accounting, wake signaling, shutdown drain, and full 256-entry capacity.

## Limitations
- Completion events are test signals, not real overlapped I/O completions; collector is a simulation.
- Fixed 4-producer/1-worker schedule; 10 repeated process runs are not a broad randomized stress campaign.
- Does not inject SetEvent/ResetEvent/WaitForMultipleObjects failures, event/result inconsistency, cancellation races, or shutdown during an actively admitted producer.
- Does not establish safe handling of abandoned pre-handoff reservations; those remain conservatively held.
- Does not integrate the production Reaper worker or modify `pipe-io-v2.c`.
- Full 28-group named-pipe acceptance matrix remains outstanding.

Production remains **NO-GO**. No production transport, tunnel, or IranPriceScout changes. No commit/reset.
