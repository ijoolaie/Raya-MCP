# Dedicated I/O Submitter — multi-producer native proof (2026-11-08)

## Scope

Added a second isolated native harness:

- `dedicated-io-submitter-multi-proof-test.c`
- `build-dedicated-io-submitter-multi-proof-test.cmd`

It does not modify the existing managed Worker, Integrated Reaper, legacy `pipe-io-v2.c`, Production, service configuration, tunnel, or IranPriceScout.

## Test topology

Each run creates:

- 70 independent overlapped byte-mode named pipes;
- 70 per-operation OVERLAPPED records and manual-reset completion events;
- seven concurrent producer threads;
- one Submitter thread as the sole ReadFile issuer;
- one Completion Worker thread as the sole GetOverlappedResult collector;
- a bounded FIFO submission queue with capacity 70.

The producers concurrently publish all 70 operation indices. The Submitter dequeues and issues one pending overlapped read for each operation. The main test waits for all seven producers to exit and all 70 I/O requests to be issued before sending any peer data. This ensures caller threads are gone while all operations are still pending.

The test then writes one byte to each peer. The Completion Worker waits for each exact operation event and calls GetOverlappedResult(FALSE) with that operation's issuing pipe HANDLE and OVERLAPPED. It signals the all-terminal acknowledgment only after all 70 operations have been positively collected. The Submitter is required to remain alive until that acknowledgment and then exits.

The queue uses a critical section and manual-reset event. Enqueue and reset/recheck are serialized under the same lock. Queue capacity is finite; this run intentionally does not fill the queue to its capacity and therefore does not test the backpressure result.

## Result

MSVC x64, `/W4 /WX`:

- 10/10 complete runs passed.
- 70 operations per run; 700 operations total.
- **6,720 assertions passed.**
- Seven producer threads exited before peer data was sent in every run.
- All 70 requests were issued exactly once per run and returned ERROR_IO_PENDING.
- All 70 completion events were collected successfully per run.
- Every operation transferred exactly one expected byte.
- Completion Worker and Submitter thread exit codes were both checked and were zero.
- Submitter thread ID was checked against its actual thread HANDLE.
- Submitter remained alive until all 70 terminal acknowledgments were recorded.
- No ERROR_OPERATION_ABORTED occurred.

## Limits

This is still a focused proof harness, not the integrated Submitter module. It does not yet cover:

- queue saturation/backpressure at capacities 1 and 2;
- more producers than queue slots under adversarial scheduling;
- producer admission racing STOPPING;
- cancellation, disconnect, immediate completion, or submission failure;
- injected SetEvent/ResetEvent/Wait failures;
- actual issuer-lease integration with the Managed Completion Worker and two-phase finalizer;
- per-process handle/thread baseline and soak tests;
- fairness bounds or operation ordering guarantees beyond successful exact-once issue/collection in these runs.

The completion harness waits on individual operation events sequentially rather than using the production Worker WaitPlan. No claim is made that this validates >63-event Worker rotation.

## Next gate

Add deterministic queue-capacity/backpressure and shutdown/admission race tests, then connect a Submitter prototype to the existing Worker only after the issuer lease and terminal acknowledgment contract has a reviewable exact-once implementation. Production remains **NO-GO**.
