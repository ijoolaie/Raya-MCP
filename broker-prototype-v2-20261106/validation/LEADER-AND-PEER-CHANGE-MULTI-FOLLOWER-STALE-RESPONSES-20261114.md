# Leader and peer change: stale responses from multiple followers (2026-11-14)

## Scope
- Isolated finite TLA+ experiment in the working copy only: D:\Work\project\Raya-MCP\broker-prototype-v2-20261106.
- Initial context: term 1, leader A, active peer B; old success and failure responses from B are pending.
- Transition: term 2, leader C, active peer D; request counter is reset and then request ID 1 is reused.
- Pending response set also includes same-term responses addressed to C but originating from the now-inactive peer B, plus valid current responses from D.
- Success/failure handling checks term and destination leader. CheckPeerGuard toggles the active-source-peer guard. Failure handling also checks latest request ID and a composite deduplication key.
- No base model, source project, or Production changes.

## Positive run (peer guard enabled)
- TLC exit 0; no error found.
- 681 states generated, 148 distinct, 0 queued; complete graph depth 9.
- Invariants passed: TypeOK, CursorLowerBound, NoOldContextAcceptance, NoWrongPeerAcceptance.
- Temporal properties passed: EventuallyCurrentFailureHandled and EventuallyPendingDrained (296 states checked for temporal branches).
- Old-term/old-leader responses are ineligible. Same-term responses from B are also rejected after active peer changes to D. Responses from D are eligible even though request ID 1 has been reused.
- All response interleavings in this finite model preserve the cursor lower bound.

## Mutation control (peer guard disabled)
- Expected invariant violation observed; TLC exit 1.
- 6 states generated, 6 distinct, 3 queued at counterexample discovery; depth 3.
- Counterexample trace pair: WitnessV2LeaderPeerChangeStaleResponses_TTrace_1794642335.tla/.bin.
- After the context transition to term 2 / leader C / active peer D, a success response from B carrying term 2 and destination C is accepted when the source-peer guard is disabled. Its matchedThrough=4 raises the new cursor's matchIndex to 4, although B is not the active peer. NoWrongPeerAcceptance is violated.
- This isolates the source-peer guard: term and destination checks alone do not establish that a response belongs to the peer state currently being updated.

## Interpretation and limits
- Response identity for this experiment is (term, destination leader, source peer, request ID). Each component scopes the response to the state it may update.
- The model has one scalar cursor for one selected active peer; it does not model the full per-follower maps of the implementation or simultaneous cursor updates for multiple peers.
- The leader/peer transition is abstracted; no election protocol, quorum, network transport, follower log contents, persistence, or actual role-transition code is modeled.
- This is bounded model evidence only, not a full Raft proof, crash-persistence validation, or Production acceptance. No experimental guard/dedup logic was integrated into the base model.

## Artifacts
- WitnessV2LeaderPeerChangeStaleResponses.tla (5,165 bytes)
- WitnessV2LeaderPeerChangeStaleResponses.cfg (234 bytes)
- WitnessV2LeaderPeerChangeStaleResponses-WithoutPeerGuard.cfg (163 bytes)
- Mutation trace pair: WitnessV2LeaderPeerChangeStaleResponses_TTrace_1794642335.tla/.bin
- This report

## Cleanup and safety
- No test-generated files were deleted; the final mutation trace is retained as evidence.
- Root temporary *.tmp.cfg count: 0; active TLC Java process count: 0.
- No source project or Production files changed; no commit or reset performed.
