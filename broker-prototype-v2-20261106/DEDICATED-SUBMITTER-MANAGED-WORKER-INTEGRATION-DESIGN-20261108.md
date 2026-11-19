# Dedicated I/O Submitter + Managed Completion Worker Integration Design
Date: 2026-11-08
Status: design only — no integration code or Production wiring

## Objective

Move overlapped ReadFile/WriteFile issuance from arbitrary caller threads to
the lifecycle-managed Dedicated I/O Submitter thread, while retaining the
existing Managed Completion Worker as the sole owner of completion collection,
terminal resource closure, and final release.

This addresses the reproduced Windows issuer-thread lifetime constraint:
the Submitter thread remains alive until every accepted request receives a
terminal ACK from the Completion Worker.

## Ownership objects

A heap-allocated request envelope is the stable cross-thread object. It owns:
- one `raya_submitter_request`;
- one `raya_managed_operation` (including exact OVERLAPPED, duplicated issuing
  pipe, completion event, private buffer, pipe-owner state and finalizer);
- the user's completion callback and callback context;
- immutable operation direction/length metadata.

The envelope and all embedded records remain valid until either:
1. a definitely-not-issued request has completed caller-terminal cleanup; or
2. a handed-off operation has been terminally collected, finalized, and its
   Submitter issuer lease has been acknowledged.

The Submitter queue stores the address of the embedded request. The Worker
Registry stores the address of the embedded integrated operation. Both must
refer to the same stable envelope for its entire lifetime.

## Admission and reservation sequence

The public submit API must not issue I/O on the caller thread.

1. Validate arguments and allocate the envelope and private buffer.
2. Create the completion event and duplicate the caller's pipe HANDLE.
3. Initialize the managed operation and finalizer against those owned handles.
4. Acquire a Managed Worker caller admission lease.
5. Acquire a Submitter admission lease.
6. Reserve the exact managed operation in the Worker Integrated Registry.
7. Enqueue the embedded Submitter request.
8. If enqueue reports BACKPRESSURE, return a retryable pending token while
   retaining both admission/reservation ownerships. The caller may retry enqueue
   or explicitly abort. Do not silently drop or auto-free the envelope.
9. If either admission/reservation step fails before enqueue, unwind only the
   ownerships positively acquired, using exact-once abort/finish and ordered
   finalizer closure. Any failed close or uncertain accounting retains the
   envelope and marks the relevant subsystem degraded.

The two subsystems have independent locks and lifecycle states. The integration
must never hold both locks simultaneously. Cross-subsystem acquisition is
ordered Worker admission -> Submitter admission -> Worker reservation -> queue
publication. Each partial failure has an explicit rollback path.

## Submitter issue callback

The Submitter's sole issue callback runs on its long-lived thread:
- call ReadFile or WriteFile using the operation's exact duplicated issuing pipe,
  private buffer, length, and exact OVERLAPPED;
- TRUE means synchronously completed/accepted;
- FALSE + ERROR_IO_PENDING means asynchronously accepted;
- a documented immediate non-pending failure is definitely not accepted;
- any outcome whose acceptance cannot be proved returns ISSUE_UNCERTAIN.

The callback must not wait for completion or for a terminal ACK. It returns
promptly so the Submitter can publish the request and continue draining its
bounded queue.

## Publication to the Managed Completion Worker

For both synchronous success and ERROR_IO_PENDING, publication calls
`raya_managed_worker_handoff` for the exact operation.

- TRANSFERRED means the Worker now owns completion collection.
- TRANSFERRED_DEGRADED also means ownership transferred; the Submitter must not
  misreport it as NOT_ACCEPTED. The operation remains retained by the degraded
  Worker and the issuer lease is intentionally not acknowledged unless a
  terminal callback actually occurs.
- NOT_TRANSFERRED after the OS accepted I/O is an ownership uncertainty. The
  request, operation, handles, OVERLAPPED and buffer must remain retained; no
  caller-side finalization or free is permitted.

The Worker must observe the event for both immediate-success and pending I/O.
Native proof is required for the immediate-success event behavior on supported
pipe types before enabling this path.

## Definitely-not-issued path

If the issue callback proves that the OS did not accept the I/O:
1. Submitter drops its issuer lease and calls the not-issued callback once.
2. The integration transitions the pipe-owner record through the caller
   submission-failed terminal path.
3. Under the Worker lock, terminalize the exact caller-owned reservation.
4. Outside the lock, close the duplicated issuing pipe and event through the
   finalizer; commit release only after both closes succeed.
5. Under the Worker lock, unreserve the exact operation and finish the Worker
   caller admission lease.
6. Invoke the user's completion callback once with the immediate Win32 error,
   then free the envelope.

Any failed accounting or close retains the envelope and suppresses free.
Because the public API already accepted/enqueued the request, this completion
callback is distinct from an API-level REJECTED result before enqueue.

## Terminal ACK and release ordering

For an accepted I/O:
1. Managed Completion Worker observes the exact event.
2. It calls GetOverlappedResult(FALSE) with the exact issuing HANDLE and exact
   OVERLAPPED.
3. Only positively terminal success/aborted outcomes proceed.
4. Worker performs integrated terminal-hold.
5. Finalizer closes the issuing pipe duplicate and completion event.
6. Pipe-owner release and Integrated release commit.
7. The operation release callback calls Submitter acknowledge on the embedded
   request. ACK may race the Submitter publish callback's return; the existing
   Submitter ISSUING/ISSUED barrier handles this race.
8. Only after ACK succeeds may the envelope and request record be freed and the
   user's completion callback be invoked.

If ACK fails or its state is uncertain, the envelope must remain retained even
though the Worker has committed its own terminal release. A separate adapter
retained-envelope registry is required so this case cannot become an orphaned
heap allocation with no diagnostic owner.

The user callback must run outside both subsystem locks.

## Shutdown ordering

The coordinator stops new admission to both subsystems, then:
1. calls Submitter shutdown (pre-admitted queue entries still drain);
2. calls Managed Worker shutdown (pre-reserved handoffs remain admissible);
3. joins Submitter while Worker continues collecting and ACKing;
4. joins Managed Worker after all handed-off operations are terminal;
5. destroys both only after both report clean STOPPED and all envelope counts
   and ownership counters are zero.

A timeout is not permission to free. Degraded state, outstanding issuer leases,
unacknowledged requests, Worker retained operations, or uncertain HANDLE closure
means no clean shutdown and no destroy.

## Cancellation and deadlines

The first integrated adapter must not claim to preserve the current
caller-thread timeout/cancel semantics. The Submitter API currently has no
operation-specific cancellation queue or cancellation ACK contract.

Before exposing cancellation:
- add an exact-request cancellation API;
- define the race with immediate completion and Worker collection;
- ensure CancelIoEx uses the exact issuing pipe/OVERLAPPED pair;
- keep the Submitter thread alive until terminal collection/ACK;
- test ERROR_NOT_FOUND completion-wins and ERROR_OPERATION_ABORTED;
- bound cancellation drain without freeing a still-pending operation.

Until then, initial adapter scope is submit-and-complete with explicit
application-level deadlines outside the adapter.

## Required acceptance before integration is enabled

1. Real byte-mode named-pipe adapter tests for synchronous immediate success,
   ERROR_IO_PENDING, immediate non-pending failure, disconnect, and payload
   integrity.
2. Producer threads exit immediately after enqueue; Submitter remains the sole
   issuer and stays alive through ACK.
3. 70+ operations across Worker WaitPlan windows, with exact event/HANDLE/
   OVERLAPPED pairing and bounded fairness.
4. Queue saturation/backpressure with concurrent producers and explicit abort.
5. Publish/ACK race, including Worker callback arriving before publish returns.
6. Worker handoff failure after OS acceptance retains all resources and never
   reports NOT_ACCEPTED.
7. CloseHandle failure for each owned handle retains the envelope and prevents
   clean shutdown.
8. Submitter and Worker shutdown races with queued, issuing, handed-off, and
   terminal-held operations.
9. OS handle-count baseline and long soak with no unexplained handle growth.
10. Fault injection for CreateEvent, DuplicateHandle, ReadFile, WriteFile,
    SetEvent, ResetEvent, waits, GetOverlappedResult, CancelIoEx, and CloseHandle.

## Current blockers

- No real ReadFile/WriteFile adapter exists for Dedicated Submitter.
- Submitter ACK is not yet wired to Managed Worker release callback.
- Definitely-not-issued caller-terminal cleanup is not factored into a reusable
  integration API.
- Adapter-level retained-envelope registry does not exist.
- Cancellation/deadline semantics are not integrated.
- Immediate-success overlapped event behavior is not yet covered through the
  complete Submitter -> Worker path.
- No combined Submitter + Managed Worker lifecycle or shutdown harness exists.

No changes to legacy `managed-pipe-io-v2.c` are proposed in this design phase.
Production remains NO-GO.

## Implementation update (2026-11-08)

An initial vertical slice now exists in `integrated-submit-adapter-v2.c/.h`.
It starts the Dedicated Submitter with adapter-owned callbacks, submits real
byte-mode named-pipe ReadFile/WriteFile operations from the Submitter thread,
hands accepted I/O to the Managed Worker, and acknowledges the Submitter only
after Worker finalization/release. Details and exact test scope are recorded in
`INTEGRATED-SUBMIT-ADAPTER-IMPLEMENTATION-20261108.md`.

Native adapter suite: 1,020 assertions/process x10 processes = **10,200 passed**
across 400 cases (pending reads, pre-buffered reads, peer disconnect, writes).
All cases also verify producer-thread exit before I/O completion, callback once,
clean two-component shutdown, zero retained envelopes, and successful destroy.

A first integration run exposed and corrected a double-decrement: successful
Integrated handoff itself consumes the Worker caller-active lease. The adapter
no longer calls Worker finish after handoff. Adapter suite and Submitter,
Managed Worker, and legacy managed-pipe regressions all pass after correction.

Still outstanding: instrumented synchronous-immediate ReadFile proof, integrated
70+ operation WaitPlan test, integrated producer/backpressure stress, full
Win32 fault injection, handoff-failure-after-OS-acceptance test, handle-count
soak, cancellation/deadline integration. Production remains NO-GO.
