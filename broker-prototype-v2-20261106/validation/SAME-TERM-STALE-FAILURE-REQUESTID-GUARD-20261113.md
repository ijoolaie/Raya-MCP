# Same-term stale AppendEntries failure and requestId guard — TLC recheck (2026-11-13)

## Question
Does the requestId equality guard reject a delayed failure response from an older request when both responses belong to the same term?

## Positive bounded witness
Artifacts:
- WitnessV2AppendEntriesSameTermStaleFailureRequestId.tla
- WitnessV2AppendEntriesSameTermStaleFailureRequestId.cfg

Initial state:
- currentTerm = 2
- lastRequestId = 2
- leaderNext = 6
- leaderMatch = 0
- stale failure response: term 2, requestId 1
- current failure response: term 2, requestId 2

The handler requires both term equality and requestId equality. The stale same-term failure is therefore ineligible, while the current request's failure is eligible.

TLC:
- Version 2026.11.06.222357 (revision d8fb231)
- Exit code: 0
- 5 states generated
- 4 distinct states
- 0 states left on queue
- Complete graph depth: 3
- Maximum outdegree: 2
- No invariant violation

## Negative control: remove requestId guard
Artifacts:
- WitnessV2AppendEntriesSameTermStaleFailureWithoutRequestIdGuard.tla
- WitnessV2AppendEntriesSameTermStaleFailureWithoutRequestIdGuard.cfg

Only the requestId equality guard was replaced with TRUE. The term equality guard remains.

TLC:
- Expected invariant violation, exit code 12
- Violated invariant: StaleFailureNeverAccepted
- 2 states generated, 2 distinct
- The term-2 stale response with requestId 1 is accepted despite lastRequestId=2.
- leaderNext decreases from 6 to 5.

This isolates the requestId guard: term checking alone does not distinguish an older request from the current request in the same term.

## Interpretation alongside the cross-term tests
The checks establish two distinct modeled protections:
1. Term equality rejects responses from an earlier term, including when request IDs collide after a new leader initializes its per-peer request counter.
2. RequestId equality rejects an older failure response from the same term after a newer request has been sent.

The base model's AssumeLeader action resets lastRequestId for the new leader, so requestId is not globally unique across terms. It is a per-leader/per-peer sequence counter in this abstraction; term and requestId checks serve different purposes.

## Implementation correspondence and limits
This witness is deliberately small and bounded. It mirrors the base model's failure-response guard and retry decrement, but does not model node identity, election transitions, concurrent leaders, full message routing, crashes/restarts, persistence, commit, or Production integration. It does not prove the whole base model. No base model or Production source was changed.

An initial TLC run found a primed-flag expression precedence issue in the new witness; the flag assignments were parenthesized and the corrected positive and negative runs are the results reported above. The initial diagnostic state output is retained.
