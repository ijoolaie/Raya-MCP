# Three concurrent request IDs with delayed AppendEntries responses (2026-11-14)

## Scope
- Working copy only: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`.
- Bounded synthetic TLA+ model; no base model, source project, or Production changes.
- TLC 2026.11.06.222357 (revision d8fb231), Java 25.0.2, one worker, 512 MiB heap.
- One term, one leader/peer pair, three request IDs (1, 2, 3).

## Scenario
The initial state has request 3 as the latest request:
- Two delayed failure copies for request 1, whose key is already in the handled set.
- Two delayed failure copies for request 2.
- Two copies of the current failure response for request 3.
- One delayed success for request 1 with matchIndex 2.
- nextIndex=5, matchIndex=1, lastRequestId=3.

The model explores all interleavings of stale-failure discard, delayed-success processing, current-failure acceptance, and duplicate-current-failure discard. The cursor decrement is clamped at matchIndex+1. A ghost boolean is set only when request 3's failure is accepted.

## Positive result
Configuration: `IncludeRequestId=TRUE`.
- TLC exit 0; PASS.
- 449 states generated, 128 distinct states, 0 queued.
- Complete graph depth 8.
- `TypeOK`, `CursorLowerBound`, and `EventuallyCurrentFailureHandled` passed.

This establishes, within this finite model, that delayed failures for requests 1 and 2 do not suppress request 3, that its duplicate is discarded after the first acceptance, and that a delayed success does not break the cursor lower bound.

## Mutation control
Configuration: `IncludeRequestId=FALSE` (request ID omitted from the deduplication key).
- Expected temporal-property violation; TLC exit 13.
- 449 states generated, 128 distinct states, 0 queued; depth 8.
- The old handled key collides with the current request's failure key, so request 3 is discarded and `currentFailureAccepted` remains FALSE.
- Counterexample trace: `WitnessV2ThreeConcurrentDelayedFailureDedup_TTrace_1794636122.tla/.bin`.

## Limits
- Finite abstract interleaving model, not a full-model verification.
- Duplicate messages are distinguished by model-only `copyId`, because TLC models messages as a set.
- Does not establish durable deduplication across restart, transport guarantees, crash recovery, election safety, or full Raft correctness.
- Cursor clamp and composite-key dedup remain experimental candidates; no base-model or Production integration was performed.

## Artifacts
- `WitnessV2ThreeConcurrentDelayedFailureDedup.tla`
- `WitnessV2ThreeConcurrentDelayedFailureDedup.cfg`
- `WitnessV2ThreeConcurrentDelayedFailureDedup-WithoutRequestId.cfg`
- Mutation counterexample trace listed above
- This report
