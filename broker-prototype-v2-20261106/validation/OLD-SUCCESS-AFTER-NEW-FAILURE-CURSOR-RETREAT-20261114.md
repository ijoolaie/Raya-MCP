# Old Success After New Failure and Cursor Retreat — 2026-11-14

## Question
Can a delayed success for request 1 safely arrive after request 2 has been sent and its failure has retreated the leader cursor?

## Model
The finite single-peer model starts with nextIndex=5, matchIndex=1, and a success response for request 1 pending alongside a failure response for request 2. The request-2 failure is handled first and clamps nextIndex to 4. Only after that may the delayed request-1 success be handled. The old success acknowledges prefix 3 and updates matchIndex/nextIndex monotonically (max semantics). A separate peerMatch variable represents the follower's currently retained prefix.

## Stable follower prefix
Config: WitnessV2OldSuccessAfterNewFailure-StablePeer.cfg
- TLC PASS, exit 0
- 3 states generated, 3 distinct
- 0 queued; complete graph depth 3
- TypeOK, CursorLowerBound, MatchWithinPeerPrefix, NoMatchBeyondPeerMonitor passed.
- Sequence includes request-2 failure (nextIndex 5 -> 4), then request-1 success (matchIndex 1 -> 3; nextIndex remains 4).
- This shows an older success can still safely contribute acknowledged progress in this bounded scenario when the follower retains the acknowledged prefix.

## Peer-prefix rollback counterfactual
Config: WitnessV2OldSuccessAfterNewFailure-PeerRollback.cfg
- Expected invariant violation, TLC exit 12
- 5 states generated, 5 distinct; 0 queued; depth 4
- Trace: WitnessV2OldSuccessAfterNewFailure_TTrace_1794645707.tla/.bin
- Sequence: request-2 failure retreats nextIndex 5 -> 4; peerMatch then regresses 3 -> 2; delayed request-1 success is accepted and raises matchIndex 1 -> 3. MatchWithinPeerPrefix fails because matchIndex=3 while peerMatch=2.
- CursorLowerBound remains true in this trace; the violated property is the acknowledged-prefix relationship, not the cursor floor.

## Interpretation
The model does not support a blanket rule to reject every old success response by request ID. In the stable-prefix case, the delayed success contains useful, valid acknowledgement information even though a newer request has failed. Monotonic max updates preserve that information without regressing the cursor.

The counterexample requires the follower to lose a prefix it had previously acknowledged. That is a crash/durability counterfactual, not evidence that the implementation actually loses log data. The model abstracts message creation and the follower log; it is not a full Raft proof, election model, persistence test, or Production acceptance test.

## Artifacts
- WitnessV2OldSuccessAfterNewFailure.tla
- WitnessV2OldSuccessAfterNewFailure-StablePeer.cfg
- WitnessV2OldSuccessAfterNewFailure-PeerRollback.cfg
- WitnessV2OldSuccessAfterNewFailure_TTrace_1794645707.tla/.bin
- this report: validation/OLD-SUCCESS-AFTER-NEW-FAILURE-CURSOR-RETREAT-20261114.md

## Change boundary and cleanup
Only the working copy was modified. No base model, source project, or Production files changed. No commits or resets. No project files were deleted. The trace is retained as counterexample evidence.
