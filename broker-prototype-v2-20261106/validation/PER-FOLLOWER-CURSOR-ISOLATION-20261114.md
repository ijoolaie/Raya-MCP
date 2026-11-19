# Per-follower cursor isolation under concurrent responses (2026-11-14)

## Scope
- Isolated finite TLA+ experiment in the working copy only: D:\Work\project\Raya-MCP\broker-prototype-v2-20261106.
- Two follower identities, B and D, each have independent nextIndex and matchIndex entries.
- A leader transition from A/term 1 to C/term 2 resets both follower cursors and reuses request ID 1.
- The transition introduces delayed old-leader success/failure responses plus current-term success/failure responses from both B and D. TLC explores nondeterministic processing and discard order.
- Each response must be routed to the cursor keyed by its source peer. Failure handling uses a per-peer composite key and clamps that peer's nextIndex to matchIndex+1.
- No base model, source project, or Production changes.

## Positive run (route by response source)
- TLC exit 0; no error found.
- 322 states generated, 81 distinct, 0 queued; complete graph depth 9.
- Invariants passed: TypeOK, CursorLowerBound (for both peers), NoCrossPeerMutation, NoOldContextAcceptance.
- Temporal properties passed: EventuallyBothFailuresHandled and EventuallyPendingDrained (162 states checked for temporal branches).
- Delayed responses from term 1 / leader A are discarded after transition. Current responses from B and D update only their own cursor entries under all explored interleavings.

## Mutation control (wrong-peer routing)
- Expected invariant violation observed; TLC exit 1.
- 4 states generated, 4 distinct, 1 queued at counterexample discovery; depth 3.
- Counterexample trace pair: WitnessV2PerFollowerCursorIsolation_TTrace_1794642539.tla/.bin.
- After the leader transition, a valid success response from B is deliberately routed to D's cursor. matchIndex[D] rises from 0 to 2 while B's cursor remains unchanged; NoCrossPeerMutation fails.
- This confirms the monitor detects cross-follower cursor contamination, rather than merely detecting stale-term responses.

## Interpretation and limits
- The tested property is per-follower isolation: processing a response sourced by peer p may mutate only cursor[p].
- The model uses two peers and one response per logical type/peer. It abstracts the implementation's full maps, actual transport, concurrent workers, retries, elections, and persistent storage.
- The leader transition and cursor reset are modeled as one abstract action. This is bounded model evidence, not a full Raft proof, crash-persistence validation, or Production acceptance.
- No experimental clamp, deduplication, or routing change was integrated into the base model.

## Artifacts
- WitnessV2PerFollowerCursorIsolation.tla
- WitnessV2PerFollowerCursorIsolation.cfg
- WitnessV2PerFollowerCursorIsolation-WrongPeerRouting.cfg
- Mutation trace pair: WitnessV2PerFollowerCursorIsolation_TTrace_1794642539.tla/.bin
- This report

## Cleanup and safety
- Removed two superseded pre-final diagnostic/mutant trace pairs: 4 files, 16,098 bytes freed. The final mutation trace is retained.
- Root temporary *.tmp.cfg count: 0; active TLC Java process count: 0.
- No source project or Production files changed; no commit or reset performed.
