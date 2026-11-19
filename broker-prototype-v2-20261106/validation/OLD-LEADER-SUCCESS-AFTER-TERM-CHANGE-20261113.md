# Old-leader success response after term change — TLC recheck (2026-11-13)

## Question
Can a delayed successful AppendEntries response from the previous leader/term advance the new leader's replication progress?

## Positive bounded witness
Artifacts:
- WitnessV2AppendEntriesOldLeaderSuccessAfterTermChange.tla
- WitnessV2AppendEntriesOldLeaderSuccessAfterTermChange.cfg

Scenario:
1. Old leader is in term 2 with an in-flight successful response (term 2, requestId 7, matchIndex 8).
2. A higher term 3 is observed; the node becomes a follower.
3. A new leader is assumed in term 3; its per-peer progress is initialized to nextIndex 4, matchIndex 0, lastRequestId 0.
4. The delayed term-2 success remains in flight and may be handled or dropped.

The success handler requires the response term to equal currentTerm. The handler is only enabled in the new-leader phase, so this specifically tests delivery after the term transition.

TLC:
- Version 2026.11.06.222357 (revision d8fb231)
- Exit code: 0
- 8 states generated
- 6 distinct states
- 0 states left on queue
- Complete graph depth: 4
- Maximum outdegree: 2
- Invariants: TypeOK, CursorLowerBound, StaleSuccessNeverAccepted, NewLeaderInitialProgress
- No invariant violation

## Negative control: remove the term guard
Artifacts:
- WitnessV2AppendEntriesOldLeaderSuccessAfterTermChangeWithoutTermGuard.tla
- WitnessV2AppendEntriesOldLeaderSuccessAfterTermChangeWithoutTermGuard.cfg

Only the response-term equality check was removed; the action still requires the new leader to be active in term 3.

TLC:
- Expected invariant violation, exit code 12
- Violated invariant: StaleSuccessNeverAccepted
- 7 states generated, 6 distinct
- Counterexample depth: 4

Counterexample:
- State 1: old leader term 2, response term 2 / requestId 7 / matchIndex 8 in flight.
- State 2: observe higher term; currentTerm becomes 3, role becomes Follower.
- State 3: assume new leader; term remains 3, role Leader, nextIndex=4, matchIndex=0, lastRequestId=0.
- State 4: without the term guard, old response is accepted; matchIndex advances 0 -> 8 and nextIndex 4 -> 9.

Trace file:
WitnessV2AppendEntriesOldLeaderSuccessAfterTermChangeWithoutTermGuard_TTrace_1794567307.tla/.bin

## Interpretation
The bounded witness confirms the term guard prevents this old-leader success response from mutating the new leader's progress. The negative control demonstrates that without the guard, the same response can incorrectly raise the new leader's matchIndex and nextIndex.

The model abstracts leader election with AssumeNewLeader and does not model real quorum election, node identity, full log contents, crashes, persistence, or Production implementation. It is not a full Raft proof. No base model or Production source was changed.

## Diagnostic history
Earlier TLC attempts exposed an action-expression issue involving unparenthesized IF expressions and an initial mutant setup that allowed handling before the term transition. Those runs are retained as diagnostics; they are not the final result. The final positive run and the final mutant run use the corrected phase guard and parenthesized IF expressions.
