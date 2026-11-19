# Per-Follower Cursor Routing Guard — Combined Context Model — 2026-11-14

## Scope
Extended the combined term/leader/peer/request-ID-reuse model with an explicit routing selector and a ghost monitor for cross-peer cursor mutation. Active peer context changes from leader A / peers {B,D} / term 1 to leader C / peers {B,E} / term 2. Peer B is retained, D is replaced by E, and request IDs 1 and 2 are reused. Old successes and new successes/failures coexist in the pending set.

The response source is the cursor identity. The positive routing function targets m.src. The mutant deliberately routes each response to the other peer's cursor (B -> E and E -> B). Both success and failure handlers use the same routing function. The monitor records any update where target peer differs from response source.

## Positive TLC run
- PASS, exit 0
- 7,433 states generated
- 1,540 distinct states
- 0 states left on queue
- Complete graph depth 12
- TypeOK, CursorLowerBound, NoOldContextAcceptance, NoStaleFailureAcceptance, NoCrossPeerMutation passed
- EventuallyNewPeerFailuresHandled and EventuallyPendingDrained passed (two temporal branches).

## Wrong-peer routing mutant
- Expected invariant violation: NoCrossPeerMutation
- TLC exit 12
- 3 states generated, 3 distinct, depth 2; 1 state remained queued at discovery.
- Counterexample: an old success from source D was routed to E's cursor; matchIndex[E] changed from 0 to 4 while the response source was D. The monitor became TRUE and NoCrossPeerMutation failed.
- This counterexample is sufficient to show the routing monitor detects cross-peer mutation; it is not evidence of an actual production defect.

## Model boundaries
Bounded abstraction only. Leader/term/peer transition and cursor initialization are atomic; there is no election protocol, quorum/commit behavior, transport implementation, crash/restart, or persistence model. The model checks source-to-cursor isolation for the two active peers in this scenario, not arbitrary cluster sizes. Success freshness remains simplified and does not have a request-ID freshness guard. No full Raft proof or Production acceptance is claimed.

## Artifacts
- WitnessV2CombinedTermLeaderPeerRequestReuse.tla
- WitnessV2CombinedTermLeaderPeerRequestReuse.cfg
- WitnessV2CombinedTermLeaderPeerRequestReuse-WrongPeerRouting.cfg
- WitnessV2CombinedTermLeaderPeerRequestReuse_TTrace_1794644482.tla/.bin (wrong-peer routing mutant)
- validation/COMBINED-TERM-LEADER-PEER-REQUESTID-REUSE-20261114.md (combined context and request-ID results)

## Change boundary and cleanup
Only the working copy was edited. No base model, source project, or Production files changed. No commits or resets. No files were deleted in this extension. Root temporary cfg count and active TLC process count were checked after the run.
