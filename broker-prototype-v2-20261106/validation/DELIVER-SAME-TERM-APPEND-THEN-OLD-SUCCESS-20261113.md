# Deliver same-term AppendEntries before delayed success response — TLC (2026-11-13)

## Purpose
Exercise the role guard using the concrete message-ordering shape from the base model rather than an abstract demotion action.

## Modeled sequence
- Node A is a leader in term 3.
- The message set contains an already in-flight successful APPEND_RESP from F to A (term 3, requestId 4, matchIndex 8).
- A same-term APPEND message from B to A is also pending (term 3, prevIndex 0, prevTerm 0, empty entries).
- DeliverSameTermAppend models the base DeliverAppend effect relevant to this test: A remains in term 3, becomes Follower, the APPEND is removed, and an APPEND_RESP from A to B is created. The older F-to-A success response remains in flight.
- Only after the APPEND has been delivered is the old success response eligible for the response-handling step in this witness.

## Positive model
Files:
- WitnessV2AppendEntriesDeliverSameTermAppendThenOldSuccess.tla
- WitnessV2AppendEntriesDeliverSameTermAppendThenOldSuccess.cfg

The response handler retains roleA = "Leader" and the term equality guard.
TLC result:
- Exit code 0
- 5 states generated
- 4 distinct states
- 0 states left on queue
- Complete graph depth 3
- Maximum outdegree 2
- Invariants: TypeOK, CursorLowerBound, OldSuccessNotAppliedAfterDemotion
- No invariant violation

## Negative control
Files:
- WitnessV2AppendEntriesDeliverSameTermAppendThenOldSuccessWithoutRoleGuard.tla
- WitnessV2AppendEntriesDeliverSameTermAppendThenOldSuccessWithoutRoleGuard.cfg

Only the role guard was removed; the term equality guard remains.
TLC result:
- Expected invariant violation, exit code 12
- Violated invariant: OldSuccessNotAppliedAfterDemotion
- 4 states generated, 4 distinct
- Counterexample depth 3

Counterexample:
1. A is Leader in term 3; the old F-to-A success response and B-to-A same-term APPEND are both queued.
2. DeliverSameTermAppend demotes A to Follower without changing term; the old response remains queued.
3. Without the role guard, A as Follower processes the old success and matchIndex changes 0 -> 8.

Trace:
WitnessV2AppendEntriesDeliverSameTermAppendThenOldSuccessWithoutRoleGuard_TTrace_1794568824.tla/.bin

## Interpretation and scope
This more concrete bounded witness confirms the role guard is independently necessary even when the response term still matches the node's current term. It complements the earlier abstract same-term-demotion witness.

The DeliverSameTermAppend action abstracts only the relevant state effects of DeliverAppend; it does not model full logs, commit-index advancement, election safety, or a full Raft protocol. This is not a full Raft proof. The base model and Production source were not modified. No commits, resets, deletions, or cleanup were performed.
