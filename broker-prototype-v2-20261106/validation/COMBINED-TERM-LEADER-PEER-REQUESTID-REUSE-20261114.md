# Combined Term, Leader, Peer and Request-ID Reuse — 2026-11-14

## Scope
Bounded TLA+ model in the working copy only. The transition changes term 1/A with active peers {B,D} to term 2/C with active peers {B,E}. Peer B is retained, D is removed, and E is added. Request IDs 1 and 2 are reused in the new context. Old-term successes remain pending while new-term successes and failures for IDs 1 and 2 are introduced. Thus old and new responses coexist during nondeterministic delivery.

The model checks:
- response context: term, destination leader, and source membership in the active peer set;
- per-peer cursor updates keyed by response source;
- failure request-ID freshness (only the latest ID 2 is accepted);
- composite failure deduplication key (term, leader, peer, requestId);
- failure cursor clamp and CursorLowerBound;
- eventual handling of each new peer's current failure and eventual draining of pending responses.

## TLC results
Positive configuration (both guards enabled):
- PASS, exit 0
- 7,433 states generated; 1,540 distinct states
- 0 states left on queue; complete graph depth 12
- TypeOK, CursorLowerBound, NoOldContextAcceptance, NoStaleFailureAcceptance passed
- EventuallyNewPeerFailuresHandled and EventuallyPendingDrained passed (two temporal branches).

Mutation: CheckContextGuard=FALSE:
- Expected invariant violation, exit 12
- 5 states generated, 5 distinct, depth 3
- After the context transition, an old-term success from retained peer B is accepted and raises matchIndex[B] from 0 to 4. NoOldContextAcceptance fires.

Mutation: CheckRequestIdGuard=FALSE:
- Expected invariant violation, exit 12
- 9 states generated, 9 distinct, depth 3
- In term 2, stale failure requestId 1 from peer B is accepted although lastRequestId[B]=2. NoStaleFailureAcceptance fires and nextIndex[B] decreases 5 to 4.

## Retained evidence
- WitnessV2CombinedTermLeaderPeerRequestReuse.tla
- WitnessV2CombinedTermLeaderPeerRequestReuse.cfg
- WitnessV2CombinedTermLeaderPeerRequestReuse-WithoutContextGuard.cfg
- WitnessV2CombinedTermLeaderPeerRequestReuse-WithoutRequestIdGuard.cfg
- WitnessV2CombinedTermLeaderPeerRequestReuse_TTrace_1794644077.tla/.bin (context-guard mutant)
- WitnessV2CombinedTermLeaderPeerRequestReuse_TTrace_1794644072.tla/.bin (request-ID mutant)

## Limitations
This is a bounded abstraction, not a full Raft proof. The leader/term/peer transition and cursor initialization are atomic environment steps; there is no election protocol, quorum/commit behavior, transport implementation, crash/restart, or persistence model. Peer-specific cursor state is represented by maps and responses update the source peer's entry, but no separate wrong-routing mutant was included in this combined run. The composite dedup key is modeled, but duplicate copies of the same failure response are not generated in this combined scenario; duplicate idempotence remains covered by earlier focused tests. Success response acceptance intentionally has no request-ID freshness guard; success updates are monotonic max() operations. The model does not establish production readiness or durable vote/response handling.

## Cleanup and change boundary
Four superseded diagnostic trace files from earlier model iterations were removed after the final model was narrowed: 20,761 bytes freed. The two final mutant trace pairs were retained. Root temporary cfg count: 0. TLC processes: 0 at final check. No base model, source project, or Production files changed. No commits or resets.