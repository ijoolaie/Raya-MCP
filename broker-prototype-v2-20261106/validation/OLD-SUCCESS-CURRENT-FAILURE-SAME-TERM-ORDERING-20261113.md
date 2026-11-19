# Same-term delayed success followed by current failure — TLC recheck (2026-11-13)

## Question
The base model handles success responses when their term equals the leader's current term, without checking requestId. Does an older success response remain safe when a newer request's failure response is also in flight, regardless of delivery order?

## Bounded witness
Artifacts:
- WitnessV2AppendEntriesOldSuccessThenCurrentFailureSameTerm.tla
- WitnessV2AppendEntriesOldSuccessThenCurrentFailureSameTerm.cfg

Initial state:
- currentTerm = 2
- lastRequestId = 2
- leaderNext = 6
- leaderMatch = 0
- delayed success: term 2, requestId 1, matchIndex 5
- current failure: term 2, requestId 2

The model allows either response to be handled first. The success handler mirrors the base model's monotonic max updates. The failure handler requires both current term and current requestId, and clamps nextIndex to at least leaderMatch + 1.

## TLC result
- TLC 2026.11.06.222357 (revision d8fb231)
- Exit code: 0
- 5 states generated
- 4 distinct states
- 0 states left on queue
- Complete graph depth: 3
- Maximum outdegree: 2
- Invariants checked: TypeOK, CursorLowerBound, CursorUpperBound, MatchBound, BothResponsesHandledConsistent
- No invariant violation

Both response orders are represented in the explored graph. Once both are handled, the invariant requires leaderMatch = 5 and leaderNext = 6. The lower-bound invariant also holds in intermediate states: nextIndex never falls below matchIndex + 1.

## Interpretation
For this specific bounded scenario, accepting the delayed same-term success without a requestId equality check is consistent with monotonic progress:
- the success response raises matchIndex to 5 and nextIndex cannot be lowered below 6 afterward;
- the newer failure response cannot undo that confirmed match progress;
- if the failure is handled first, the later success restores nextIndex to at least matchIndex + 1;
- if success is handled first, the subsequent failure is clamped at that same lower bound.

This is evidence for the safety of the base model's particular max/clamp update pattern in this scenario. It is not evidence that every same-term stale success is safe under every possible log, request pipeline, or role/term transition.

## Limits
This is a finite response-order witness, not a proof of full Raft or Production behavior. It assumes the delayed success's matchIndex=5 is truthful evidence that the follower matched through index 5, and fixes one leader/current term. It does not model log conflicts, leader changes, peer identity, message generation, crashes, persistence, commit advancement, or implementation code. No base model or Production source was changed.

An intermediate TLC attempt after adding the combined-state invariant had a TLA+ parse error caused by an incorrectly escaped conjunction; that run did not check the model. The file was corrected, then the final TLC run above completed successfully. The earlier successful run without the combined-state invariant is also retained.
