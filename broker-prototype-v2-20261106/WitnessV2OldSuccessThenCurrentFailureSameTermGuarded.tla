---- MODULE WitnessV2OldSuccessThenCurrentFailureSameTermGuarded ----
EXTENDS Naturals, FiniteSets, TLC
CONSTANTS Term, CurrentTerm
ASSUME Term = CurrentTerm
VARIABLES nextIndex, matchIndex, lastRequestId, pending
vars == <<nextIndex, matchIndex, lastRequestId, pending>>
OldSuccess == [kind |-> "APPEND_RESP", term |-> Term, requestId |-> 1, success |-> TRUE, matchIndex |-> 3]
CurrentFailure == [kind |-> "APPEND_RESP", term |-> Term, requestId |-> 2, success |-> FALSE, matchIndex |-> 0]
Init ==
  /\ nextIndex = 4
  /\ matchIndex = 1
  /\ lastRequestId = 2
  /\ pending = {OldSuccess, CurrentFailure}
HandleOldSuccess ==
  /\ OldSuccess \in pending
  /\ OldSuccess.term = CurrentTerm
  /\ matchIndex' = IF OldSuccess.matchIndex > matchIndex THEN OldSuccess.matchIndex ELSE matchIndex
  /\ nextIndex' = IF OldSuccess.matchIndex + 1 > nextIndex THEN OldSuccess.matchIndex + 1 ELSE nextIndex
  /\ pending' = pending \ {OldSuccess}
  /\ UNCHANGED <<lastRequestId>>
HandleCurrentFailure ==
  /\ CurrentFailure \in pending
  /\ CurrentFailure.term = CurrentTerm
  /\ CurrentFailure.requestId = lastRequestId
  /\ nextIndex' = IF nextIndex - 1 > matchIndex + 1
                   THEN nextIndex - 1 ELSE matchIndex + 1
  /\ pending' = pending \ {CurrentFailure}
  /\ UNCHANGED <<matchIndex, lastRequestId>>
Next == HandleOldSuccess \/ HandleCurrentFailure
Spec == Init /\ [][Next]_vars
TypeOK ==
  /\ nextIndex \in 1..6
  /\ matchIndex \in 0..5
  /\ lastRequestId = 2
  /\ pending \subseteq {OldSuccess, CurrentFailure}
CursorLowerBound == nextIndex >= matchIndex + 1
====

