---- MODULE WitnessV2AppendEntriesOldSuccessThenCurrentFailureSameTerm ----
EXTENDS Naturals, FiniteSets, TLC
VARIABLES currentTerm, leaderNext, leaderMatch, lastRequestId, messages, acceptedOldSuccess, acceptedCurrentFailure
vars == <<currentTerm, leaderNext, leaderMatch, lastRequestId, messages, acceptedOldSuccess, acceptedCurrentFailure>>
OldSuccess1 == [kind |-> "APPEND_RESP", term |-> 2, requestId |-> 1, success |-> TRUE, matchIndex |-> 5]
CurrentFailure2 == [kind |-> "APPEND_RESP", term |-> 2, requestId |-> 2, success |-> FALSE, matchIndex |-> 0]
Init ==
  /\ currentTerm = 2
  /\ leaderNext = 6
  /\ leaderMatch = 0
  /\ lastRequestId = 2
  /\ messages = {OldSuccess1, CurrentFailure2}
  /\ acceptedOldSuccess = FALSE
  /\ acceptedCurrentFailure = FALSE
HandleSuccess(m) ==
  /\ m \in messages
  /\ m.kind = "APPEND_RESP"
  /\ m.success = TRUE
  /\ m.term = currentTerm
  /\ leaderMatch' = IF m.matchIndex > leaderMatch THEN m.matchIndex ELSE leaderMatch
  /\ leaderNext' = IF m.matchIndex + 1 > leaderNext THEN m.matchIndex + 1 ELSE leaderNext
  /\ messages' = messages \ {m}
  /\ acceptedOldSuccess' = (acceptedOldSuccess \/ (m = OldSuccess1))
  /\ UNCHANGED <<currentTerm, lastRequestId, acceptedCurrentFailure>>
HandleFailure(m) ==
  /\ m \in messages
  /\ m.kind = "APPEND_RESP"
  /\ m.success = FALSE
  /\ m.term = currentTerm
  /\ m.requestId = lastRequestId
  /\ leaderNext' = IF leaderNext - 1 > leaderMatch + 1 THEN leaderNext - 1 ELSE leaderMatch + 1
  /\ messages' = messages \ {m}
  /\ acceptedCurrentFailure' = (acceptedCurrentFailure \/ (m = CurrentFailure2))
  /\ UNCHANGED <<currentTerm, leaderMatch, lastRequestId, acceptedOldSuccess>>
Next == (\E m \in messages : HandleSuccess(m)) \/ (\E m \in messages : HandleFailure(m))
TypeOK ==
  /\ currentTerm = 2
  /\ leaderNext \in 1..6
  /\ leaderMatch \in 0..5
  /\ lastRequestId = 2
  /\ messages \subseteq {OldSuccess1, CurrentFailure2}
  /\ acceptedOldSuccess \in BOOLEAN
  /\ acceptedCurrentFailure \in BOOLEAN
CursorLowerBound == leaderNext >= leaderMatch + 1
CursorUpperBound == leaderNext <= 6
MatchBound == leaderMatch <= 5
BothResponsesHandledConsistent == (acceptedOldSuccess /\ acceptedCurrentFailure) => (leaderMatch = 5 /\ leaderNext = 6)
Spec == Init /\ [][Next]_vars
====