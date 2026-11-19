# Dedicated I/O Submitter — issuer-lifetime proof harness (2026-11-08)

## Scope

A minimal native Windows characterization harness was added before implementing the Submitter module:

- `dedicated-io-submitter-proof-test.c`
- `build-dedicated-io-submitter-proof-test.cmd`

It is isolated from the existing managed-pipe wrapper and does not modify the Completion Worker, Integrated Reaper, legacy `pipe-io-v2.c`, or Production.

## Test sequence

1. Create a real overlapped byte-mode named pipe and duplicate the server HANDLE for the issuer.
2. Start a long-lived Submitter thread. It waits for a producer signal, issues one overlapped ReadFile using its own thread, then remains blocked on a terminal-ack event.
3. Start a short-lived producer thread. It publishes the request-ready signal and exits immediately.
4. Confirm the producer thread has exited before the I/O is completed.
5. Confirm ReadFile returned ERROR_IO_PENDING and the Submitter thread is still alive. Confirm the recorded issuer thread ID equals GetThreadId(Submitter HANDLE).
6. Send one byte from the peer.
7. A separate Completion Worker thread waits for the exact operation event and calls GetOverlappedResult(FALSE) using the exact duplicated issuing HANDLE and OVERLAPPED.
8. Only after successful terminal collection does the Completion Worker signal terminal acknowledgment.
9. Submitter exits after observing the acknowledgment; all test handles are closed.

## Result

MSVC x64, `/W4 /WX`:

- 28 assertions per process × 25 runs = **700 assertions passed**.
- All 25 runs observed ERROR_IO_PENDING.
- The producer exited before peer data was sent in every run.
- The issuing Submitter remained alive while the operation was pending in every run.
- A different Completion Worker thread successfully collected the exact operation.
- The Submitter exited only after terminal acknowledgment.
- No ERROR_OPERATION_ABORTED occurred in this scenario.

## What this proves — and does not prove

This supports the core lifetime hypothesis: the short-lived producer need not be the I/O issuer; a dedicated issuer thread can remain alive while another thread performs terminal GetOverlappedResult collection.

This is only a focused primitive test. It does **not** yet prove:

- a real bounded concurrent submission queue or its wake/reset race protocol;
- integration with the existing Managed Completion Worker, Registry, terminal-hold, finalizer, and release path;
- multiple outstanding requests issued by one Submitter;
- immediate success/failure, cancellation, peer disconnect, or shutdown races;
- issuer lease accounting under concurrent terminal acknowledgments;
- handle/thread leak freedom or long-duration behavior;
- behavior across all supported Windows versions and pipe configurations.

The harness uses a single producer signal and a single operation; it is not the production Submitter implementation.

## Next gate

Before implementing the full Submitter, expand the proof harness to multiple outstanding operations and concurrent producers, then integrate the issuer-lease accounting with the existing Worker using exact-once terminal acknowledgment. Keep Production **NO-GO**.
