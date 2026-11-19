# Same-term demotion with delayed AppendEntries success — TLC recheck (2026-11-13)

## Purpose
Check the role guard in the base model's HandleAppendResponse action independently of its term guard. The base action requires both:
- role[m.dst] = Leader
- m.term = currentTerm[m.dst]

## Scenario abstraction
- Node A is initially a leader in term 3.
- A successful APPEND_RESP from follower F is already in flight, addressed to A, with term 3, requestId 4 and matchIndex 8.
- A is demoted to Follower without changing term 3.
- The response is then considered for processing.

This isolates the role guard: the message term still equals A's current term, so the term check alone cannot reject it.

## Positive model
Files:
- WitnessV2AppendEntriesSameTermDemotionStaleSuccessRoleGuard.tla
- WitnessV2AppendEntriesSameTermDemotionStaleSuccessRoleGuard.cfg

The handler retains roleA = "Leader". TLC result:
- Exit code 0
- 5 states generated
- 4 distinct states
- 0 states left on queue
- Complete graph depth 3
- Maximum outdegree 2
- Invariants: TypeOK, CursorLowerBound, FollowerNeverProcessesLeaderResponse, DemotionPhaseConsistent
- No invariant violation

After demotion, the delayed response cannot be handled; the follower's matchIndex remains 0.

## Negative control
Files:
- WitnessV2AppendEntriesSameTermDemotionStaleSuccessRoleGuardWithoutRoleGuard.tla
- WitnessV2AppendEntriesSameTermDemotionStaleSuccessRoleGuardWithoutRoleGuard.cfg

The role guard was removed while the term equality check remained. TLC found the expected violation:
- Exit code 12
- Violated invariant: FollowerNeverProcessesLeaderResponse
- 3 states generated, 3 distinct
- Counterexample depth 2

Trace:
1. A is Leader in term 3; response term 3 / requestId 4 / matchIndex 8 is in flight.
2. A becomes Follower in the same term; response remains in flight.
3. Without the role guard, the follower processes the response and matchIndex advances 0 -> 8.

Trace file:
WitnessV2AppendEntriesSameTermDemotionStaleSuccessRoleGuardWithoutRoleGuard_TTrace_1794568461.tla and .bin.

## Interpretation and limitations
The model demonstrates that the role guard is independently useful: a same-term delayed success must not update replication progress after the addressed node has ceased being leader. The term guard remains present in the negative control.

The demotion is an abstract same-term role transition used to isolate the guard. It is not a complete election or split-brain model, and it does not assert that a new legitimate leader is elected in the same term. The production/base model was not changed. No commits, resets, deletions, or cleanup were performed.
