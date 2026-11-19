# Dedicated I/O Submission Thread — Design Gate (2026-11-08)

## Status

Design proposal only. No implementation, production integration, service provisioning, tunnel change, or legacy pipe-I/O modification is authorized by this document.

The managed-pipe thread-exit characterization reproduced ERROR_OPERATION_ABORTED in 10/10 runs when the issuing producer thread exited while its overlapped operation was still pending. The current managed-pipe wrapper is therefore not safe for arbitrary short-lived callers.

## Decision

Introduce one lifecycle-managed, long-lived I/O Submission Thread (Submitter) per broker transport instance. Public caller threads enqueue requests; only the Submitter invokes ReadFile/WriteFile for those requests. The Submitter remains alive until every I/O it issued has been positively observed terminal by the Completion Worker.

This is an architectural hypothesis to validate with native tests, not yet a proven fix. The thread-exit experiment must be repeated with the Submitter issuing the I/O and the original caller exiting immediately after successful enqueue.

## Thread responsibilities

### Public producer threads

- Validate basic arguments and acquire an admission lease.
- Duplicate the caller's pipe HANDLE into an operation-owned HANDLE while the caller's original HANDLE is valid.
- Allocate the stable operation record, dedicated manual-reset completion event, and private buffer.
- Copy write input into the private buffer.
- Reserve the exact operation with the existing Integrated Reaper before enqueue.
- Enqueue the fully initialized request into a bounded Submitter queue.
- Return an enqueue result; they do not call ReadFile/WriteFile, wait for I/O completion, or own the issuer lifetime.
- On any pre-enqueue failure, unwind only resources for which no I/O has been issued, using the existing finalizer/lease rules.

### Dedicated Submitter thread

- Is created once during transport startup and is not a thread-pool task.
- Is the sole issuer of all overlapped pipe ReadFile/WriteFile calls for this transport instance.
- Dequeues requests FIFO (or another explicitly tested policy), marks each request ISSUE_IN_PROGRESS, and issues I/O using the operation's exact duplicated pipe HANDLE and embedded OVERLAPPED.
- For ERROR_IO_PENDING, transfers the already-reserved operation to the Completion Worker after the issue result is known.
- For immediate success, hands the terminal/signaled operation to the same Worker path; do not create a separate cleanup implementation.
- For immediate non-pending failure, records that the OS did not accept the operation, finalizes locally, and releases the issuer lease.
- Never exits merely because its queue is empty. It remains alive while any request it issued has not been positively collected terminal.
- Does not wait synchronously on each operation and does not run callbacks.

### Completion Worker

- Retains its existing bounded WaitPlan / Registry model and remains the only component that collects handed-off operations.
- Calls GetOverlappedResult(FALSE) using the exact issuing HANDLE and exact OVERLAPPED after the exact event is signaled.
- On ERROR_IO_INCOMPLETE or uncertain completion, retains ownership and does not acknowledge the Submitter's issuer lease.
- On positively terminal success or the narrowly permitted exact-pair ERROR_OPERATION_ABORTED, acknowledges that operation's issuer lease exactly once.
- Then performs terminal-hold, resource finalization, integrated release, and callback as already specified.
- Acknowledgment must use Submitter-owned lifecycle state, not dereference a freed operation. It must be ordered before the Submitter can observe zero outstanding issuer leases.

## Ownership and state model

Each request needs distinct states; do not overload the existing Reaper owner enum:

1. PRODUCER_BUILDING — no I/O issued; producer owns the private record.
2. QUEUED — stable record is in the bounded Submitter queue; Submitter owns issue responsibility.
3. ISSUE_IN_PROGRESS — Submitter is inside ReadFile/WriteFile; shutdown cannot destroy the Submitter.
4. ISSUED_PENDING — OS accepted the request with ERROR_IO_PENDING; issuer lease is outstanding.
5. ISSUED_IMMEDIATE — API returned immediate success; route through the same Worker collection/terminal protocol.
6. HANDED_TO_COMPLETION — Worker Registry owns completion collection; issuer lease remains outstanding.
7. TERMINAL_ACKNOWLEDGED — Worker has positively observed terminal completion and released the Submitter issuer lease exactly once.
8. FINALIZING — Worker closes operation-owned handles and commits two-phase release.
9. RELEASED — callback may run and operation storage may be freed.
10. RETAINED_UNCERTAIN — completion/ownership/handle close is uncertain; no issuer lease is silently dropped and clean shutdown is forbidden.

A request that fails before the OS accepts I/O must take a separate NOT_ISSUED cleanup path. A request that may have been accepted must never use that path.

## Required Submitter accounting

Maintain under one Submitter lifecycle lock (or a rigorously equivalent interlocked protocol):

- producer admission count;
- bounded queue count and capacity;
- active issue count (0 or 1 for a single Submitter);
- issuer lease count: incremented before entering the OS issue call and decremented only by either (a) a proven not-issued failure path or (b) the Worker's exactly-once terminal acknowledgment;
- shutdown state: RUNNING, STOPPING, STOPPED, or DEGRADED;
- a manual-reset state-change/drained event with a documented reset/set protocol.

The issuer lease must be acquired before ReadFile/WriteFile so a fast completion cannot race ahead of accounting. On immediate non-pending failure, the Submitter releases it. On immediate success or ERROR_IO_PENDING, it remains held until Worker terminal acknowledgment. A failure to classify the issue result is uncertain: retain the lease and enter DEGRADED.

The Submitter exit predicate is strictly:

- lifecycle is STOPPING;
- producer admissions are closed and active producer admissions are zero;
- submission queue is empty;
- active issue count is zero;
- issuer lease count is zero;
- no ownership/accounting invariant is degraded.

Queue-empty alone is never an exit condition.

## Queue and synchronization rules

- Queue capacity must be finite and configured at startup. Enqueue at capacity returns an explicit backpressure result without accepting ownership.
- Reserve operation and acquire admission before queue publication; publication is the ownership-transfer linearization point.
- The queue lock protects queue links, queue count, shutdown state, and the wake predicate. Never call ReadFile/WriteFile, GetOverlappedResult, CloseHandle, user callbacks, or blocking waits while holding it.
- Use a manual-reset wake event with a tested reset/enqueue protocol. The Submitter resets only while holding the same lock used by enqueue and rechecks the queue and shutdown predicates before waiting.
- The Submitter may dequeue one item and issue outside the lock; its active_issue count prevents shutdown from treating the queue as drained.
- Worker terminal acknowledgment must update issuer lease accounting and signal the Submitter under a race-safe protocol. Lost wake must not lose the lease or permit early exit.
- No per-operation threads, thread-pool I/O issuers, APC completion routines, or IOCP association in this first design. IOCP and event-low-bit suppression remain separate design gates.

## Shutdown protocol

1. Transition transport and Submitter admission to STOPPING under the admission lock; reject all new requests.
2. Wait for already admitted producers to either publish their request or unwind it as not issued.
3. Submitter drains every queued request, issuing or explicitly rejecting it; it does not abandon queue entries.
4. Completion Worker continues collecting all issued operations.
5. Submitter remains alive until every issued operation receives exactly one positive terminal acknowledgment.
6. Completion Worker finishes finalization/release; Worker and Submitter stop only after their independent drain predicates are true.
7. Join both threads with bounded waits. A timeout reports incomplete shutdown; it does not free thread state, operation records, events, handles, buffers, or locks.
8. If any completion or handle-close result is uncertain, enter sticky DEGRADED, retain ownership, and refuse clean STOPPED/destroy. Do not force thread exit to make shutdown appear successful.

The Submitter's thread HANDLE and synchronization objects remain valid until the thread has actually exited and been joined. No detached thread is allowed.

## Callback and API contract

- Public submit becomes asynchronous enqueue, not an inline I/O operation.
- Return values must distinguish ACCEPTED, BACKPRESSURE, REJECTED_NOT_ISSUED, and RETAINED_UNCERTAIN.
- Completion callback runs only after Worker terminal collection and finalizer/release commits, outside all internal locks.
- Callback context must remain valid until callback. Read data is callback-scoped unless the API explicitly transfers a separately owned result buffer.
- No callback may run for a request rejected before queue publication.
- Cancellation is a queued command/state transition. The Submitter or a dedicated cancellation policy must call CancelIoEx against the exact issuing HANDLE/OVERLAPPED; cancellation request success is not terminal completion. Worker must still collect the final result.
- A timeout is a caller-visible policy event, not permission to free or reuse the OVERLAPPED, buffer, event, or pipe HANDLE.

## Native acceptance gates (must precede implementation integration)

### A. Issuer lifetime proof

- Producer enqueues one read, receives ACCEPTED, and exits immediately.
- Submitter issues the operation; no peer data initially.
- Worker later receives peer data and reports successful completion.
- Repeat with write operations and immediate-success operations.
- Repeat 100+ times with randomized scheduling. No ERROR_OPERATION_ABORTED may be attributed solely to producer exit.
- Instrument the actual issuing thread ID and prove it remains alive through the terminal GetOverlappedResult observation.

### B. Queue and producer concurrency

- Queue capacity 1, 2, and a larger configured value; exact backpressure behavior.
- Many producers racing enqueue, STOPPING, and queue wake reset.
- Producer exits immediately after enqueue.
- Allocation, DuplicateHandle, CreateEvent, reserve, and enqueue failures at every boundary.
- Verify every accepted request is issued exactly once; every rejected request is never issued.

### C. Shutdown and cancellation

- Shutdown while queue is empty, nonempty, and while Submitter is inside ReadFile/WriteFile.
- Shutdown while pending operations exist and while Worker is in each collection/finalization state.
- CancelIoEx success, ERROR_NOT_FOUND completion-wins, ERROR_OPERATION_ABORTED, other cancellation errors, and cancellation drain expiry.
- Submitter must not exit while issuer lease count is nonzero.
- Join timeout followed by later successful join; no premature cleanup.

### D. I/O behavior and Worker integration

- Immediate success, ERROR_IO_PENDING, immediate non-pending failure.
- Peer disconnect during pending read and pending write.
- Spurious signaled event / ERROR_IO_INCOMPLETE retry.
- More than 63 operations and fairness across WaitPlan rotations.
- 70+ concurrent operations with randomized completion order.
- Exact issuing HANDLE/OVERLAPPED/event identity and exactly-once terminal acknowledgment.
- Injected wake SetEvent, ResetEvent, WaitForMultipleObjects, Registry, and accounting failures.
- Both handle-close failure orders; sticky HANDLE_CLOSE and no false clean shutdown.

### E. Resource and lifecycle checks

- Process handle-count baseline before/after repeated batches.
- Thread-count baseline; exactly one Submitter and one Completion Worker per transport.
- Long-duration soak and repeated start/stop.
- No per-operation thread creation.
- No resource release while any OS operation may still reference it.
- MSVC x64 /W4 /WX and complete existing regression suite.

## Explicit non-goals

- Do not modify legacy pipe-io-v2.c.
- Do not connect this design to Production or the live broker.
- Do not create a Windows service or protected roots.
- Do not change tunnel configuration/processes or IranPriceScout.
- Do not enable degraded-state recovery.
- Do not claim the issuer-thread behavior is fully understood from one test; validate on supported Windows versions and pipe configurations.

## Go / No-Go

Implementation may begin only after the design is reviewed against the existing Worker/Integrated ownership contracts and the issuer-lifetime proof harness is specified. Production remains **NO-GO** until all acceptance gates and the broader protected-filesystem broker gates independently pass.
