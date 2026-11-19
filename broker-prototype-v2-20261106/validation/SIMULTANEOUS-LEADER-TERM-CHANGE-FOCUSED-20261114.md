# Simultaneous Leader and Term Change — 2026-11-14

## Objective

Extend the two-peer/two-request-ID focused response model to a simultaneous transition from leader A / term 1 to leader D / term 2. Verify that delayed responses addressed to A cannot mutate D's cursors, even when a delayed old-leader response carries the current term and reuses a request ID.

## Model scope

Test-only model: `WitnessV2LeaderAndTermChangeFocused.tla`.

- Peers B and C each have independent `nextIndex` and `matchIndex`.
- Leader A at term 1 changes to leader D at term 2.
- Both peers have request IDs 1 and 2; D's current request ID is 2 for each peer.
- Delayed term-1 messages are addressed to D. This deliberately isolates the term guard: after the transition their destination matches D, but their term is stale.
- Delayed term-2 messages addressed to A deliberately isolate the leader-identity guard: their term is current, but their destination is the previous leader.
- Current term-2 messages addressed to D include success and failure responses for both request IDs and both peers.
- Failure deduplication key is (term, destination leader, source peer, requestId).
- On transition, D's cursors are initialized independently for B and C; the old leader's pending messages remain in the same message set.
- Each handler selects the cursor by message source peer and records whether an old-term, old-leader, or stale-request failure was accepted.

This is an abstract finite-state test, not a complete election or replication model. It does not model real AppendEntries delivery, follower logs, quorum/commit, transport, crash/restart, or durable persistence.

## Positive run

Config: `WitnessV2LeaderAndTermChangeFocused.cfg`.

Active invariants:
- TypeOK
- CursorLowerBound
- PeerCursorIsolation
- NoOldTermAcceptance
- NoOldLeaderAcceptance
- NoStaleFailureAcceptance

Active temporal properties:
- EventuallyCurrentFailuresHandled
- EventuallyAllResponsesDrained

Result:
- TLC exit code 0 — **PASS**
- 24,585 states generated
- 4,100 distinct states
- 0 states left on queue
- Complete graph depth 14
- All three temporal-checking branches completed
- No invariant or temporal-property violation

## Mutation controls

All three mutations were run against the same final model revision.

### Leader identity guard disabled

Config: `WitnessV2LeaderAndTermChangeFocused-WithoutLeaderGuard.cfg`.

- TLC exit code 12 — expected invariant violation.
- 3 states generated; 3 distinct; depth 2.
- Counterexample: after A→D / term 1→2, a term-2 success addressed to A is accepted by D's handler.
- B's `matchIndex` changes from 0 to 6 and B's `nextIndex` from 5 to 7; C's cursors remain unchanged.
- Violated invariant: `NoOldLeaderAcceptance`.
- Retained trace pair: `WitnessV2LeaderAndTermChangeFocused_TTrace_1794665729.tla/.bin`.

### Term guard disabled

Config: `WitnessV2LeaderAndTermChangeFocused-WithoutTermGuard.cfg`.

- TLC exit code 12 — expected invariant violation.
- 5 states generated; 5 distinct; depth 3.
- Counterexample: after the transition, a term-1 success addressed to D is accepted despite the current term being 2.
- Violated invariant: `NoOldTermAcceptance`.
- Retained trace pair: `WitnessV2LeaderAndTermChangeFocused_TTrace_1794665732.tla/.bin`.

### Request-ID guard disabled

Config: `WitnessV2LeaderAndTermChangeFocused-WithoutRequestIdGuard.cfg`.

- TLC exit code 12 — expected invariant violation.
- 9 states generated; 9 distinct; depth 3.
- Counterexample: a term-2 failure for peer B with requestId 1 is accepted while requestId 2 is the latest; B's `nextIndex` retreats from 5 to 4 while C remains at 7.
- Violated invariant: `NoStaleFailureAcceptance`.
- Retained trace pair: `WitnessV2LeaderAndTermChangeFocused_TTrace_1794665735.tla/.bin`.

The three mutation controls therefore independently detect missing term, leader-identity, and latest-request-ID guards in this abstraction.

## Findings and limits

1. In the bounded two-peer/two-request-ID scenario, the positive model passes after simultaneous leader and term transition.
2. The term guard is independently necessary even when the delayed response destination matches the new leader.
3. The leader-identity guard is independently necessary even when a delayed old-leader response carries the current term.
4. The latest-request-ID guard remains necessary for failures from the current leader and term.
5. The leader-guard counterexample changes only B's cursor; C's cursor remains unchanged in that transition. This supports the peer-indexed update behavior in this model, not an implementation-level proof.
6. This result does not close the previously inconclusive full-model Term2/RequestId2 exploration and is not a full Raft proof or Production acceptance.

## Artifacts

- `WitnessV2LeaderAndTermChangeFocused.tla`
- `WitnessV2LeaderAndTermChangeFocused.cfg`
- `WitnessV2LeaderAndTermChangeFocused-WithoutLeaderGuard.cfg`
- `WitnessV2LeaderAndTermChangeFocused-WithoutTermGuard.cfg`
- `WitnessV2LeaderAndTermChangeFocused-WithoutRequestIdGuard.cfg`
- Three final mutation trace pairs listed above
- This report

## Integrity and cleanup

- Two initial parse-only iterations failed before model checking (first an invalid primed-variable assignment operator, then a missing set-closing brace); both were corrected before the final four runs.
- An early diagnostic iteration produced two superseded trace pairs; those four obsolete files were removed after the final runs (24,537 bytes freed). The three final counterexample trace pairs were retained.
- Zero TLC processes remained after the tests.
- Zero root-level `*.tmp.cfg` files were present.
- Main incremental base model SHA256 remains `C85CF1906D92DF31A62F8DA63392673809D3A314F30012242FB2B6E54B203A57`.
- No base model/config, source project, or Production files changed.
- No commits or resets.
