# Mixed success/failure responses with nondeterministic delivery order (2026-11-14)

## Scope
- Working copy only: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`.
- Bounded synthetic TLA+ model; no base model, source project, or Production changes.
- TLC 2026.11.06.222357 (revision d8fb231), Java 25.0.2, one worker, 512 MiB heap.
- One term, one leader/peer pair, request IDs 1, 2, and 3.

## Scenario and explored interleavings
Pending messages include:
- Failure response for request 1 (already represented in handled-key history).
- Failure response for request 2 (stale relative to latest request 3).
- Two duplicate failure responses for current request 3.
- Success responses for requests 1, 2, and 3, with matchIndex values 2, 3, and 4.

TLC explores all reachable interleavings of success processing, stale failure discard, current failure acceptance, and duplicate failure discard. Success updates are monotonic for matchIndex/nextIndex. Failure decrement is clamped to matchIndex+1. A ghost flag records actual acceptance of the current request-3 failure.

## Positive result
Configuration: `IncludeRequestId=TRUE`.
- TLC exit 0; PASS.
- 449 states generated, 128 distinct states, 0 queued.
- Complete graph depth 8.
- `TypeOK`, `CursorLowerBound`, and `EventuallyCurrentFailureHandled` passed.

The bounded graph includes every interleaving in this finite state space. Delayed successes can advance matchIndex before the current failure is processed; the clamp preserves the lower bound. The first current failure is accepted and its duplicate is discarded.

## Mutation control
Configuration: `IncludeRequestId=FALSE`.
- Expected temporal-property violation; TLC exit 13.
- 449 states generated, 128 distinct states, 0 queued; depth 8.
- With request ID omitted, the old handled key collides with the current failure key. The request-3 failure is discarded and the acceptance ghost remains FALSE.
- Counterexample trace: `WitnessV2MixedSuccessFailureOrdering_TTrace_1794636296.tla/.bin`.

## Limits and interpretation
- This is a finite abstract interleaving model, not a full incremental-model proof.
- Duplicate messages are distinguished by model-only `copyId`, because TLC models messages as a set.
- Success-response acceptance here intentionally models the existing simplified abstraction and does not add request-ID freshness checks to success responses.
- Does not prove durable deduplication across restart, transport guarantees, crash recovery, election safety, or full Raft correctness.
- Cursor clamp and composite-key dedup remain experimental candidates only. No base-model or Production integration was performed.

## Artifacts
- `WitnessV2MixedSuccessFailureOrdering.tla`
- `WitnessV2MixedSuccessFailureOrdering.cfg`
- `WitnessV2MixedSuccessFailureOrdering-WithoutRequestId.cfg`
- Mutation counterexample trace listed above
- This report
