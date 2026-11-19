---- MODULE WitnessV2AppendEntriesOldResponseWithNewRequests ----
EXTENDS Naturals, FiniteSets, TLC

VARIABLES currentTerm, leaderRole, leaderNext, leaderMatch,
          lastRequestId, messages, newRequestSent, newRequestDelivered
vars == <<currentTerm, leaderRole, leaderNext, leaderMatch,
          lastRequestId, messages, newRequestSent, newRequestDelivered>>

OldFailure == [kind |-> "APPEND_RESP", term |-> 1,
               requestId |-> 2, success |-> FALSE, matchIndex |-> 0]
NewRequest == [kind |-> "APPEND", term |-> 2,
               requestId |-> 2, prevIndex |-> 5,
               prevTerm |-> 4, entries |-> <<>>]
NewSuccess == [kind |-> "APPEND_RESP", term |-> 2,
               requestId |-> 2, success |-> TRUE, matchIndex |-> 5]

Init ==
  /\ currentTerm = 2
  /\ leaderRole = TRUE
  /\ leaderNext = 6
  /\ leaderMatch = 0
  /\ lastRequestId = 0
  /\ messages = {OldFailure}
  /\ newRequestSent = FALSE
  /\ newRequestDelivered = FALSE

SendNewRequest ==
  /\ currentTerm = 2 /\ leaderRole
  /\ ~newRequestSent
  /\ lastRequestId = 0
  /\ messages' = messages \cup {NewRequest}
  /\ lastRequestId' = 2
  /\ newRequestSent' = TRUE
  /\ UNCHANGED <<currentTerm, leaderRole, leaderNext, leaderMatch,
                 newRequestDelivered>>

DeliverNewRequest ==
  /\ NewRequest \in messages
  /\ currentTerm = 2 /\ leaderRole
  /\ NewRequest.prevIndex = 5
  /\ messages' = (messages \ {NewRequest}) \cup {NewSuccess}
  /\ newRequestDelivered' = TRUE
  /\ UNCHANGED <<currentTerm, leaderRole, leaderNext, leaderMatch,
                 lastRequestId, newRequestSent>>

HandleNewSuccess ==
  /\ NewSuccess \in messages
  /\ NewSuccess.term = currentTerm
  /\ leaderRole
  /\ NewSuccess.requestId <= lastRequestId
  /\ leaderMatch' = IF NewSuccess.matchIndex > leaderMatch
                    THEN NewSuccess.matchIndex ELSE leaderMatch
  /\ leaderNext' = IF NewSuccess.matchIndex + 1 > leaderNext
                   THEN NewSuccess.matchIndex + 1 ELSE leaderNext
  /\ messages' = messages \ {NewSuccess}
  /\ UNCHANGED <<currentTerm, leaderRole, lastRequestId,
                 newRequestSent, newRequestDelivered>>

HandleOldFailure ==
  /\ OldFailure \in messages
  /\ OldFailure.term = currentTerm
  /\ leaderRole
  /\ OldFailure.requestId = lastRequestId
  /\ leaderNext' = IF leaderNext - 1 > leaderMatch + 1
                   THEN leaderNext - 1 ELSE leaderMatch + 1
  /\ messages' = messages \ {OldFailure}
  /\ UNCHANGED <<currentTerm, leaderRole, leaderMatch, lastRequestId,
                 newRequestSent, newRequestDelivered>>

DropOldFailure ==
  /\ OldFailure \in messages
  /\ OldFailure.term < currentTerm
  /\ messages' = messages \ {OldFailure}
  /\ UNCHANGED <<currentTerm, leaderRole, leaderNext, leaderMatch,
                 lastRequestId, newRequestSent, newRequestDelivered>>

Next ==
  \/ SendNewRequest
  \/ DeliverNewRequest
  \/ HandleNewSuccess
  \/ HandleOldFailure
  \/ DropOldFailure

TypeOK ==
  /\ currentTerm = 2 /\ leaderRole = TRUE
  /\ leaderNext \in 1..6 /\ leaderMatch \in 0..5
  /\ lastRequestId \in {0, 2}
  /\ messages \subseteq {OldFailure, NewRequest, NewSuccess}
  /\ newRequestSent \in BOOLEAN
  /\ newRequestDelivered \in BOOLEAN
CursorLowerBound == leaderNext >= leaderMatch + 1
NextWithinLeaderLog == leaderNext <= 6
MatchWithinLeaderLog == leaderMatch <= 5
OldTermFailureCannotAffectCursor == leaderNext = 6 /\ leaderMatch \in {0, 5}
NewRequestLifecycle == newRequestDelivered => newRequestSent
NewSuccessRequiresDelivery == leaderMatch = 5 => newRequestDelivered
RequestIdCollisionIsPresent == OldFailure.requestId = NewRequest.requestId

Spec == Init /\ [][Next]_vars
====