# Mixed Old AppendEntries Responses Across Terms — TLC Recheck (2026-11-13)

## Scope
Standalone bounded TLA+ witness combining both stale response classes in one state space:
- OldSuccess: term 1, requestId 2, success=TRUE, matchIndex=5.
- OldFailure: term 1, requestId 2, success=FALSE, matchIndex=0.
Both remain in flight while the term-2 leader sends new requests with IDs 1 and 2. Thus both old responses collide with the new request ID 2.

The two new requests may be delivered in either order. Their term-2 successful responses may be handled in either order, and either or both old responses may remain pending until after current-term progress. The generic success handler requires response.term=currentTerm. The failure handler requires both response.term=currentTerm and response.requestId=lastRequestId. Old responses can alternatively be dropped without changing cursors.

The model includes explicit ghost flags acceptedOldSuccess and acceptedOldFailure. The invariants assert neither stale response was accepted.

## TLC result
- TLC version: 2026.11.06.222357 (revision d8fb231)
- Exit code: 0
- Result: Model checking completed; no error found
- States generated: 125
- Distinct states: 52
- States left on queue: 0
- Complete graph depth: 9
- Maximum outdegree: 4
- Invariants checked: 11 (TypeOK, CursorLowerBound, NextWithinLeaderLog, MatchWithinLeaderLog, OldSuccessNeverAccepted, OldFailureNeverAccepted, OldResponsesNeverAccepted, DeliveryRequiresSend, SentIdsMonotonic, AcceptedResponsesWereDelivered, AcceptedCurrentIdsBounded)
- Metadir: validation/mixed-old-responses-across-terms-20261113

## Interpretation
Within this finite state space, neither an old successful nor an old failed response is accepted by the term-2 leader, despite both sharing requestId 2 with a new-term request. Current-term success responses remain eligible to advance leader progress. The term check is essential for both response classes; the failure path also checks requestId equality.

## Limitations
- This is a bounded abstract witness, not a proof of the production broker or full Raft.
- Term 2 and the new leader are initial environmental assumptions; term advancement/election is not modeled in this combined witness.
- Failure handling is represented with its term and requestId guards; this model does not introduce a current-term failure response.
- Success handling follows the base model's term guard and does not add a requestId equality guard for successful responses.
- Network delivery is represented by finite message transitions.
- No crash/restart, persistence, quorum commit, liveness, or durable vote ledger claim is made.
- No production files or base models were changed.

## Files
- WitnessV2AppendEntriesMixedOldResponsesAcrossTerms.tla
- WitnessV2AppendEntriesMixedOldResponsesAcrossTerms.cfg
- This report
