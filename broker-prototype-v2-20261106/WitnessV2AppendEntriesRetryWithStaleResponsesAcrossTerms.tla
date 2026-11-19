---- MODULE WitnessV2AppendEntriesRetryWithStaleResponsesAcrossTerms ----
EXTENDS Naturals, FiniteSets, TLC

VARIABLES currentTerm, leaderNext, leaderMatch, lastRequestId,
          messages, sent, delivered, acceptedCurrentSuccess,
          acceptedCurrentFailure, acceptedOldSuccess, acceptedOldFailure
vars == <<currentTerm, leaderNext, leaderMatch, lastRequestId,
          messages, sent, delivered, acceptedCurrentSuccess,
          acceptedCurrentFailure, acceptedOldSuccess, acceptedOldFailure>>

OldSuccess == [kind |-> "APPEND_RESP", term |-> 1,
               requestId |-> 2, success |-> TRUE, matchIndex |-> 5]
OldFailure == [kind |-> "APPEND_RESP", term |-> 1,
               requestId |-> 2, success |-> FALSE, matchIndex |-> 0]
NewReq1 == [kind |-> "APPEND", term |-> 2, requestId |-> 1]
NewReq2 == [kind |-> "APPEND", term |-> 2, requestId |-> 2]
NewSuccess1 == [kind |-> "APPEND_RESP", term |-> 2,
                requestId |-> 1, success |-> TRUE, matchIndex |-> 2]
NewFailure2 == [kind |-> "APPEND_RESP", term |-> 2,
                requestId |-> 2, success |-> FALSE, matchIndex |-> 0]

Init ==
  /\ currentTerm = 2
  /\ leaderNext = 6
  /\ leaderMatch = 0
  /\ lastRequestId = 0
  /\ messages = {OldSuccess, OldFailure}
  /\ sent = {}
  /\ delivered = {}
  /\ acceptedCurrentSuccess = FALSE
  /\ acceptedCurrentFailure = FALSE
  /\ acceptedOldSuccess = FALSE
  /\ acceptedOldFailure = FALSE

SendReq1 ==
  /\ 1 \notin sent
  /\ lastRequestId = 0
  /\ messages' = messages \cup {NewReq1}
  /\ sent' = sent \cup {1}
  /\ lastRequestId' = 1
  /\ UNCHANGED <<currentTerm, leaderNext, leaderMatch, delivered,
                  acceptedCurrentSuccess, acceptedCurrentFailure,
                  acceptedOldSuccess, acceptedOldFailure>>

SendReq2 ==
  /\ 1 \in sent
  /\ 2 \notin sent
  /\ lastRequestId = 1
  /\ messages' = messages \cup {NewReq2}
  /\ sent' = sent \cup {2}
  /\ lastRequestId' = 2
  /\ UNCHANGED <<currentTerm, leaderNext, leaderMatch, delivered,
                  acceptedCurrentSuccess, acceptedCurrentFailure,
                  acceptedOldSuccess, acceptedOldFailure>>

DeliverReq1 ==
  /\ NewReq1 \in messages
  /\ messages' = (messages \ {NewReq1}) \cup {NewSuccess1}
  /\ delivered' = delivered \cup {1}
  /\ UNCHANGED <<currentTerm, leaderNext, leaderMatch, lastRequestId,
                  sent, acceptedCurrentSuccess, acceptedCurrentFailure,
                  acceptedOldSuccess, acceptedOldFailure>>

DeliverReq2 ==
  /\ NewReq2 \in messages
  /\ messages' = (messages \ {NewReq2}) \cup {NewFailure2}
  /\ delivered' = delivered \cup {2}
  /\ UNCHANGED <<currentTerm, leaderNext, leaderMatch, lastRequestId,
                  sent, acceptedCurrentSuccess, acceptedCurrentFailure,
                  acceptedOldSuccess, acceptedOldFailure>>

HandleSuccess(m) ==
  /\ m \in messages
  /\ m.kind = "APPEND_RESP"
  /\ m.success = TRUE
  /\ m.term = currentTerm
  /\ leaderMatch' = IF m.matchIndex > leaderMatch
                     THEN m.matchIndex ELSE leaderMatch
  /\ leaderNext' = IF m.matchIndex + 1 > leaderNext
                    THEN m.matchIndex + 1 ELSE leaderNext
  /\ messages' = messages \ {m}
  /\ acceptedCurrentSuccess' = (acceptedCurrentSuccess \/ (m = NewSuccess1))
  /\ acceptedOldSuccess' = (acceptedOldSuccess \/ (m = OldSuccess))
  /\ UNCHANGED <<currentTerm, lastRequestId, sent, delivered,
                  acceptedCurrentFailure, acceptedOldFailure>>

HandleFailure(m) ==
  /\ m \in messages
  /\ m.kind = "APPEND_RESP"
  /\ m.success = FALSE
  /\ m.term = currentTerm
  /\ m.requestId = lastRequestId
  /\ leaderNext' = IF leaderNext - 1 > leaderMatch + 1
                    THEN leaderNext - 1 ELSE leaderMatch + 1
  /\ messages' = messages \ {m}
  /\ acceptedCurrentFailure' = (acceptedCurrentFailure \/ (m = NewFailure2))
  /\ acceptedOldFailure' = (acceptedOldFailure \/ (m = OldFailure))
  /\ UNCHANGED <<currentTerm, leaderMatch, lastRequestId, sent, delivered,
                  acceptedCurrentSuccess, acceptedOldSuccess>>

DropOldSuccess ==
  /\ OldSuccess \in messages
  /\ OldSuccess.term < currentTerm
  /\ messages' = messages \ {OldSuccess}
  /\ UNCHANGED <<currentTerm, leaderNext, leaderMatch, lastRequestId,
                  sent, delivered, acceptedCurrentSuccess,
                  acceptedCurrentFailure, acceptedOldSuccess,
                  acceptedOldFailure>>

DropOldFailure ==
  /\ OldFailure \in messages
  /\ OldFailure.term < currentTerm
  /\ messages' = messages \ {OldFailure}
  /\ UNCHANGED <<currentTerm, leaderNext, leaderMatch, lastRequestId,
                  sent, delivered, acceptedCurrentSuccess,
                  acceptedCurrentFailure, acceptedOldSuccess,
                  acceptedOldFailure>>

Next ==
  \/ SendReq1 \/ SendReq2
  \/ DeliverReq1 \/ DeliverReq2
  \/ \E m \in messages : HandleSuccess(m)
  \/ \E m \in messages : HandleFailure(m)
  \/ DropOldSuccess \/ DropOldFailure

TypeOK ==
  /\ currentTerm = 2
  /\ leaderNext \in 1..6
  /\ leaderMatch \in 0..5
  /\ lastRequestId \in 0..2
  /\ sent \subseteq {1, 2}
  /\ delivered \subseteq {1, 2}
  /\ acceptedCurrentSuccess \in BOOLEAN
  /\ acceptedCurrentFailure \in BOOLEAN
  /\ acceptedOldSuccess \in BOOLEAN
  /\ acceptedOldFailure \in BOOLEAN
  /\ messages \subseteq {OldSuccess, OldFailure, NewReq1, NewReq2,
                          NewSuccess1, NewFailure2}

CursorLowerBound == leaderNext >= leaderMatch + 1
NextWithinLeaderLog == leaderNext <= 6
MatchWithinLeaderLog == leaderMatch <= 5
OldSuccessNeverAccepted == acceptedOldSuccess = FALSE
OldFailureNeverAccepted == acceptedOldFailure = FALSE
OldResponsesNeverAccepted ==
  acceptedOldSuccess = FALSE /\ acceptedOldFailure = FALSE
DeliveryRequiresSend == delivered \subseteq sent
SentIdsMonotonic == (2 \in sent) => (1 \in sent)
CurrentSuccessRequiresDelivery == acceptedCurrentSuccess => (1 \in delivered)
CurrentFailureRequiresDelivery == acceptedCurrentFailure => (2 \in delivered)

Spec == Init /\ [][Next]_vars
====