# Old Success Response Across Terms — TLC Recheck (2026-11-13)

## Scope
Standalone bounded TLA+ witness for a successful AppendEntries response from an older term that remains in flight after the leader has advanced to term 2. The stale response deliberately reuses requestId 2, which is also used by a new-term request.

The term-2 leader starts with leaderNext=3 and leaderMatch=1. It can send two requests (IDs 1 and 2), deliver them independently, and receive successful responses with matchIndex 2 and 5. Message delivery/response handling is nondeterministic. The generic HandleSuccess action accepts only responses whose term equals currentTerm. It also records explicitly whether the old success was accepted; TLC verifies that this flag remains FALSE. Therefore OldSuccess (term 1) cannot satisfy the handler guard; it may instead be dropped without changing leader cursors.

## Authoritative TLC result
- TLC version: 2026.11.06.222357 (revision d8fb231)
- Final metadir: validation/old-success-across-terms-guard-recheck-20261113
- Exit code: 0
- Result: Model checking completed; no error found
- States generated: 50
- Distinct states: 26
- States left on queue: 0
- Complete graph depth: 8
- Maximum outdegree: 3
- Invariants checked: 10 (TypeOK, CursorLowerBound, NextWithinLeaderLog, MatchWithinLeaderLog, OldSuccessNeverAccepted, OldTermSuccessRemainsUnaccepted, DeliveryRequiresSend, SentIdsMonotonic, AcceptedResponsesWereDelivered, AcceptedCurrentIdsBounded)

## Interpretation
Within this finite witness, an old-term successful response with a colliding requestId is never accepted by the term-2 leader. New-term responses remain eligible to advance matchIndex/nextIndex. The model checks the term guard, not a complete Raft implementation.

## Limitations
- This is a bounded, abstract witness, not a proof of the production broker or full Raft.
- The term-2 leader is an initial environmental assumption; election/leadership establishment is not modeled.
- Network delivery is represented by a finite set of message transitions.
- No crash/restart, persistence, quorum commit, liveness, or durable vote ledger claim is made.
- No production files or base models were changed.

## Files
- WitnessV2AppendEntriesOldSuccessAcrossTerms.tla
- WitnessV2AppendEntriesOldSuccessAcrossTerms.cfg
- This report
