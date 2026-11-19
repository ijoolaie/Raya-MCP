# Dedicated I/O Submitter — Concurrency Gates (2026-11-08)

## Scope
These are deterministic native protocol harnesses, not an integrated Submitter implementation. They validate the synchronization invariants that must be carried into the eventual module. They do not authorize Production integration.

## Manual-reset wake / lost-wake barrier
Files:
- `dedicated-io-submitter-lost-wake-test.c`
- `build-dedicated-io-submitter-lost-wake-test.cmd`

The consumer acquires the shared queue lock, verifies the queue is empty, resets the manual-reset wake event, signals a barrier while still holding the lock, and then releases the lock before waiting. The producer waits for that barrier, then enqueues under the same lock and signals the wake event. This forces the producer to contend at the critical reset-to-wait boundary. The consumer must wake, consume exactly one item, reset the event under the lock, and leave the queue empty.

Result: MSVC x64 `/W4 /WX`, 100/100 runs, 1,500 assertions passed. No lost wake observed.

## Admission versus STOPPING barrier
Files:
- `dedicated-io-submitter-admission-stop-test.c`
- `build-dedicated-io-submitter-admission-stop-test.cmd`

Four producer threads per run acquire admission leases through the same `try_admit` function while RUNNING, then block before publication. The coordinator waits on a counting semaphore until all four leases are definitely acquired and transitions to STOPPING under the shared lock. A subsequent call to that same `try_admit` function is rejected. The pre-admitted producers are then released; they publish while STOPPING and decrement their active lease exactly once. The test verifies that `can_exit` is false while accepted requests remain queued, drains each queue entry, and only then verifies `can_exit` becomes true.

Result: MSVC x64 `/W4 /WX`, 50/50 runs, 1,850 assertions passed.

## Limitations
- The wake harness uses a one-item test queue and isolated consumer/producer threads; it does not exercise the eventual Submitter's real queue implementation, shutdown, or native I/O.
- The admission harness validates lease/STOPPING ordering in a test model; it does not call the future public enqueue API or inject allocation/reservation failures.
- Neither harness tests wake SetEvent failure, ResetEvent failure, thread creation failure, queue saturation under concurrent producers, or integrated Worker acknowledgment.
- These results are protocol evidence only. The integrated Submitter remains unimplemented and Production remains **NO-GO**.

## Next gate
Implement the Submitter synchronization core only after translating these exact lock/lease invariants into a module-level state machine. Then add injected failure tests and integrate it with the existing managed Completion Worker and two-phase finalizer/release path. Do not alter legacy `pipe-io-v2.c`, Production, services, tunnel configuration, or IranPriceScout.
