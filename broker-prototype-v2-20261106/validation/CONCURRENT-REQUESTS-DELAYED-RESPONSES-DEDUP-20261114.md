# Concurrent requests and delayed AppendEntries responses (2026-11-14)

## Scope
- Working copy only: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`.
- Bounded synthetic TLA+ model; no base model, source project, or Production changes.
- TLC 2026.11.06.222357 (revision d8fb231), Java 25.0.2, one worker, 512 MiB heap.
- One leader/peer pair and one term; two request IDs (1 and 2) coexist in the pending response set.

## Scenario
The initial state represents request 1 as previously handled and request 2 as the latest request:
- An old failure for request 1 is still delayed (two distinct copies).
- A delayed success for request 1 can still arrive and raise matchIndex.
- Two copies of the current failure for request 2 are pending.
- lastRequestId is 2 and the old handled key is for request 1.

The model explores all interleavings of:
- processing the delayed success for request 1;
- discarding stale failure responses for request 1;
- accepting one current failure response for request 2;
- discarding its duplicate using the recorded composite key.

The cursor update is clamped at matchIndex+1. The ghost boolean currentFailureAccepted is set only when the request-2 failure is actually accepted. The temporal property therefore cannot pass merely because the old request-1 key already exists.

## Positive result
Configuration: `IncludeRequestId=TRUE`.
- TLC exit 0; PASS.
- 81 states generated, 32 distinct states, 0 queued.
- Complete graph depth 6.
- `TypeOK`, `CursorLowerBound`, and `EventuallyCurrentFailureHandled` passed.

This confirms, within this bounded model, that the old request's delayed response does not suppress acceptance of the current request's failure, and that the second duplicate does not apply another cursor decrement.

## Mutation control
Configuration: `IncludeRequestId=FALSE` (request ID omitted from the deduplication key).
- Expected temporal-property violation observed; TLC exit 13.
- The key for request 2 collides with the previously handled key for request 1.
- The current request-2 failure is discarded as a duplicate; currentFailureAccepted remains FALSE.
- Counterexample trace: `WitnessV2ConcurrentDelayedFailureDedup_TTrace_1794635992.tla/.bin`.

## Limits
- This is a finite abstract interleaving model, not a verification of the full incremental model.
- Distinct duplicate messages use copyId because TLC models messages as a set.
- The model does not prove durable deduplication across restart, transport delivery guarantees, crash recovery, election safety, or full Raft correctness.
- Cursor clamp and composite-key dedup remain experimental candidates only. No integration into the base model or Production was performed.

## Artifacts
- `WitnessV2ConcurrentDelayedFailureDedup.tla`
- `WitnessV2ConcurrentDelayedFailureDedup.cfg`
- `WitnessV2ConcurrentDelayedFailureDedup-WithoutRequestId.cfg`
- Mutation counterexample trace listed above
- This report
