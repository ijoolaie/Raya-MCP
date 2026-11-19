---- MODULE WitnessV2DelayedOldSuccessAfterFailure ----
EXTENDS Naturals, FiniteSets, TLC
CONSTANTS PreserveSuccessMonotonicity
VARIABLES nextIndex, matchIndex, maxSuccessEvidence, pending
vars == <<nextIndex, matchIndex, maxSuccessEvidence, pending>>
OldSuccess == [requestId |-> 1, matchedThrough |-> 2]
NewerSuccess == [requestId |-> 2, matchedThrough |-> 4]
Init ==
  /\ nextIndex = 4
  /\ matchIndex = 1
  /\ maxSuccessEvidence = 1
  /\ pending = {OldSuccess, NewerSuccess}
HandleSuccess(m) ==
  /\ m \in pending
  /\ IF PreserveSuccessMonotonicity
        THEN
          /\ matchIndex' = IF m.matchedThrough > matchIndex THEN m.matchedThrough ELSE matchIndex
          /\ nextIndex' = IF m.matchedThrough + 1 > nextIndex THEN m.matchedThrough + 1 ELSE nextIndex
        ELSE
          /\ matchIndex' = m.matchedThrough
          /\ nextIndex' = m.matchedThrough + 1
  /\ maxSuccessEvidence' = IF m.matchedThrough > maxSuccessEvidence THEN m.matchedThrough ELSE maxSuccessEvidence
  /\ pending' = pending \ {m}
Next == \E m \in pending : HandleSuccess(m)
Spec == Init /\ [][Next]_vars
TypeOK ==
  /\ nextIndex \in 1..6
  /\ matchIndex \in 0..5
  /\ maxSuccessEvidence \in 1..4
  /\ pending \subseteq {OldSuccess, NewerSuccess}
CursorLowerBound == nextIndex >= matchIndex + 1
MatchIndexDominatesObservedSuccess == matchIndex >= maxSuccessEvidence
====
