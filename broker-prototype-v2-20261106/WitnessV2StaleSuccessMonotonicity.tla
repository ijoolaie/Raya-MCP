---- MODULE WitnessV2StaleSuccessMonotonicity ----
EXTENDS Naturals, FiniteSets, TLC
CONSTANTS PreserveSuccessMonotonicity
VARIABLES currentTerm, lastRequestId, nextIndex, matchIndex, maxSuccessEvidence, pending
vars == <<currentTerm, lastRequestId, nextIndex, matchIndex, maxSuccessEvidence, pending>>
OldSuccess == [kind |-> "SUCCESS", term |-> 1, requestId |-> 1, matchedThrough |-> 2]
NewerSuccess == [kind |-> "SUCCESS", term |-> 1, requestId |-> 2, matchedThrough |-> 4]
CurrentFailure == [kind |-> "FAILURE", term |-> 1, requestId |-> 3, matchedThrough |-> 0]
Init ==
  /\ currentTerm = 1
  /\ lastRequestId = 3
  /\ nextIndex = 5
  /\ matchIndex = 1
  /\ maxSuccessEvidence = 1
  /\ pending = {OldSuccess, NewerSuccess, CurrentFailure}
HandleSuccess(m) ==
  /\ m \in pending
  /\ m.kind = "SUCCESS"
  /\ m.term = currentTerm
  /\ IF PreserveSuccessMonotonicity
        THEN
          /\ matchIndex' = IF m.matchedThrough > matchIndex THEN m.matchedThrough ELSE matchIndex
          /\ nextIndex' = IF m.matchedThrough + 1 > nextIndex THEN m.matchedThrough + 1 ELSE nextIndex
        ELSE
          /\ matchIndex' = m.matchedThrough
          /\ nextIndex' = m.matchedThrough + 1
  /\ maxSuccessEvidence' = IF m.matchedThrough > maxSuccessEvidence THEN m.matchedThrough ELSE maxSuccessEvidence
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, lastRequestId>>
HandleCurrentFailure ==
  /\ CurrentFailure \in pending
  /\ CurrentFailure.term = currentTerm
  /\ CurrentFailure.requestId = lastRequestId
  /\ nextIndex' = IF nextIndex - 1 > matchIndex + 1 THEN nextIndex - 1 ELSE matchIndex + 1
  /\ pending' = pending \ {CurrentFailure}
  /\ UNCHANGED <<currentTerm, lastRequestId, matchIndex, maxSuccessEvidence>>
Next ==
  \/ \E m \in pending : HandleSuccess(m)
  \/ HandleCurrentFailure
Spec == Init /\ [][Next]_vars
TypeOK ==
  /\ currentTerm = 1
  /\ lastRequestId = 3
  /\ nextIndex \in 1..6
  /\ matchIndex \in 0..5
  /\ maxSuccessEvidence \in 1..4
  /\ pending \subseteq {OldSuccess, NewerSuccess, CurrentFailure}
CursorLowerBound == nextIndex >= matchIndex + 1
MatchIndexDominatesObservedSuccess == matchIndex >= maxSuccessEvidence
====
