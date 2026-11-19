# Retry cursor under stale cross-term responses — TLC recheck (2026-11-13)

## Scope
Standalone bounded witness in the D: project copy. It combines:
- a term-1 success response (request ID 2, matchIndex 5) arriving late;
- a term-1 failure response (request ID 2) arriving late;
- two term-2 requests, IDs 1 and 2;
- a current-term success for request 1 (matchIndex 2);
- a current-term failure for request 2, eligible under the modeled lastRequestId guard.

The initial retry cursor is nextIndex=6 and matchIndex=0. A valid current failure may decrement nextIndex, subject to the floor matchIndex+1. The valid current success may advance matchIndex and nextIndex. Either handling order is explored.

## Modeled guards
- Success responses are accepted only when response.term = currentTerm.
- Failure responses additionally require response.requestId = lastRequestId.
- Old-response ghost flags record any accidental acceptance of either stale response.
- Current response flags record whether each valid current-term handler was reached; their delivery implications are checked.

## TLC result
<badge>PASS</badge>
- TLC version: 2026.11.06.222357 (revision d8fb231)
- Exit code: 0
- 125 states generated
- 52 distinct states
- 0 states left on queue
- Complete graph depth: 9
- Maximum outdegree: 4
- 11 invariants checked; no error found
- Successful run metadir: states\26-11-13-13-33-39.471

An initial execution exposed an incompletely constrained primed flag due to operator precedence in a disjunction. The four ghost-flag assignments were parenthesized and the model was rerun; the successful result above is from the corrected model. The earlier diagnostic run and its state data were retained.

## Artifacts and hashes
- Model: WitnessV2AppendEntriesRetryWithStaleResponsesAcrossTerms.tla
  - SHA256: F2D971DDAF8B0C92370CFE60E2D032B42CB17C4BCC3CAD67CD6980215247F0B5
- Config: WitnessV2AppendEntriesRetryWithStaleResponsesAcrossTerms.cfg
  - SHA256: 8461E690913E3CEE466AEC9AC2568470CE286B21F365C72A32B2E90D2BF423CC

## Limits
This is a finite witness, not a full Raft proof. It assumes the term-2 leader and current term as initial environment state; it does not model elections, crashes/restarts, persistence, quorum commit, liveness, or Production integration. The two response outcomes are assigned to distinct request IDs to avoid modeling contradictory outcomes for one request. No base model or Production code is changed.
