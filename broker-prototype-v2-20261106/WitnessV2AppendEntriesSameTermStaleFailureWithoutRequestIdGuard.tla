---- MODULE WitnessV2AppendEntriesSameTermStaleFailureWithoutRequestIdGuard ----
EXTENDS Naturals, FiniteSets, TLC

VARIABLES currentTerm, leaderNext, leaderMatch, lastRequestId,
          messages, sent, acceptedCurrentFailure, acceptedStaleFailure
vars == <<currentTerm, leaderNext, leaderMatch, lastRequestId,
          messages, sent, acceptedCurrentFailure, acceptedStaleFailure>>

StaleFailure1 == [kind |-> "APPEND_RESP", term |-> 2,
                   requestId |-> 1, success |-> FALSE, matchIndex |-> 0]
CurrentFailure2 == [kind |-> "APPEND_RESP", term |-> 2,
                     requestId |-> 2, success |-> FALSE, matchIndex |-> 0]

Init ==
  /\ currentTerm = 2
  /\ leaderNext = 6
  /\ leaderMatch = 0
  /\ lastRequestId = 2
  /\ messages = {StaleFailure1, CurrentFailure2}
  /\ sent = {1, 2}
  /\ acceptedCurrentFailure = FALSE
  /\ acceptedStaleFailure = FALSE

HandleFailure(m) ==
  /\ m \in messages
  /\ m.kind = "APPEND_RESP"
  /\ m.success = FALSE
  /\ m.term = currentTerm
  /\ TRUE
  /\ leaderNext' = IF leaderNext - 1 > leaderMatch + 1
                    THEN leaderNext - 1 ELSE leaderMatch + 1
  /\ messages' = messages \ {m}
  /\ acceptedCurrentFailure' = (acceptedCurrentFailure \/ (m = CurrentFailure2))
  /\ acceptedStaleFailure' = (acceptedStaleFailure \/ (m = StaleFailure1))
  /\ UNCHANGED <<currentTerm, leaderMatch, lastRequestId, sent>>

DropStaleFailure ==
  /\ StaleFailure1 \in messages
  /\ messages' = messages \ {StaleFailure1}
  /\ UNCHANGED <<currentTerm, leaderNext, leaderMatch, lastRequestId,
                  sent, acceptedCurrentFailure, acceptedStaleFailure>>

Next == (\E m \in messages : HandleFailure(m)) \/ DropStaleFailure

TypeOK ==
  /\ currentTerm = 2
  /\ leaderNext \in 1..6
  /\ leaderMatch \in 0..5
  /\ lastRequestId = 2
  /\ sent = {1, 2}
  /\ messages \subseteq {StaleFailure1, CurrentFailure2}
  /\ acceptedCurrentFailure \in BOOLEAN
  /\ acceptedStaleFailure \in BOOLEAN

CursorLowerBound == leaderNext >= leaderMatch + 1
StaleFailureNeverAccepted == acceptedStaleFailure = FALSE
CurrentFailureRequiresCurrentRequest ==
  acceptedCurrentFailure => (2 = lastRequestId)

Spec == Init /\ [][Next]_vars
====