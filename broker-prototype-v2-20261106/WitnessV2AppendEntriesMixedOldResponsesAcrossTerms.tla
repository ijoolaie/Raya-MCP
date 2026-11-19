---- MODULE WitnessV2AppendEntriesMixedOldResponsesAcrossTerms ----
EXTENDS Naturals, FiniteSets, TLC

VARIABLES currentTerm, leaderNext, leaderMatch, lastRequestId,
          messages, sent, delivered, acceptedCurrent,
          acceptedOldSuccess, acceptedOldFailure
vars == <<currentTerm, leaderNext, leaderMatch, lastRequestId,
          messages, sent, delivered, acceptedCurrent,
          acceptedOldSuccess, acceptedOldFailure>>

OldSuccess == [kind |-> "APPEND_RESP", term |-> 1,
               requestId |-> 2, success |-> TRUE, matchIndex |-> 5]
OldFailure == [kind |-> "APPEND_RESP", term |-> 1,
               requestId |-> 2, success |-> FALSE, matchIndex |-> 0]
NewReq1 == [kind |-> "APPEND", term |-> 2, requestId |-> 1]
NewReq2 == [kind |-> "APPEND", term |-> 2, requestId |-> 2]
NewResp1 == [kind |-> "APPEND_RESP", term |-> 2,
             requestId |-> 1, success |-> TRUE, matchIndex |-> 2]
NewResp2 == [kind |-> "APPEND_RESP", term |-> 2,
             requestId |-> 2, success |-> TRUE, matchIndex |-> 5]

Init ==
  /\ currentTerm = 2
  /\ leaderNext = 3
  /\ leaderMatch = 1
  /\ lastRequestId = 0
  /\ messages = {OldSuccess, OldFailure}
  /\ sent = {}
  /\ delivered = {}
  /\ acceptedCurrent = {}
  /\ acceptedOldSuccess = FALSE
  /\ acceptedOldFailure = FALSE

SendReq1 ==
  /\ 1 \notin sent
  /\ lastRequestId = 0
  /\ messages' = messages \cup {NewReq1}
  /\ sent' = sent \cup {1}
  /\ lastRequestId' = 1
  /\ UNCHANGED <<currentTerm, leaderNext, leaderMatch, delivered,
                  acceptedCurrent, acceptedOldSuccess, acceptedOldFailure>>

SendReq2 ==
  /\ 1 \in sent
  /\ 2 \notin sent
  /\ lastRequestId = 1
  /\ messages' = messages \cup {NewReq2}
  /\ sent' = sent \cup {2}
  /\ lastRequestId' = 2
  /\ UNCHANGED <<currentTerm, leaderNext, leaderMatch, delivered,
                  acceptedCurrent, acceptedOldSuccess, acceptedOldFailure>>

DeliverReq1 ==
  /\ NewReq1 \in messages
  /\ NewReq1.term = currentTerm
  /\ messages' = (messages \ {NewReq1}) \cup {NewResp1}
  /\ delivered' = delivered \cup {1}
  /\ UNCHANGED <<currentTerm, leaderNext, leaderMatch, lastRequestId,
                  sent, acceptedCurrent, acceptedOldSuccess, acceptedOldFailure>>

DeliverReq2 ==
  /\ NewReq2 \in messages
  /\ NewReq2.term = currentTerm
  /\ messages' = (messages \ {NewReq2}) \cup {NewResp2}
  /\ delivered' = delivered \cup {2}
  /\ UNCHANGED <<currentTerm, leaderNext, leaderMatch, lastRequestId,
                  sent, acceptedCurrent, acceptedOldSuccess, acceptedOldFailure>>

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
  /\ acceptedCurrent' = acceptedCurrent \cup {m.requestId}
  /\ acceptedOldSuccess' = acceptedOldSuccess \/ (m = OldSuccess)
  /\ UNCHANGED <<currentTerm, lastRequestId, sent, delivered, acceptedOldFailure>>

HandleFailure(m) ==
  /\ m \in messages
  /\ m.kind = "APPEND_RESP"
  /\ m.success = FALSE
  /\ m.term = currentTerm
  /\ m.requestId = lastRequestId
  /\ leaderNext' = IF leaderNext - 1 > leaderMatch + 1
                    THEN leaderNext - 1 ELSE leaderMatch + 1
  /\ messages' = messages \ {m}
  /\ acceptedOldFailure' = acceptedOldFailure \/ (m = OldFailure)
  /\ UNCHANGED <<currentTerm, leaderMatch, lastRequestId, sent, delivered,
                  acceptedCurrent, acceptedOldSuccess>>

DropOldSuccess ==
  /\ OldSuccess \in messages
  /\ OldSuccess.term < currentTerm
  /\ messages' = messages \ {OldSuccess}
  /\ UNCHANGED <<currentTerm, leaderNext, leaderMatch, lastRequestId,
                  sent, delivered, acceptedCurrent, acceptedOldSuccess,
                  acceptedOldFailure>>

DropOldFailure ==
  /\ OldFailure \in messages
  /\ OldFailure.term < currentTerm
  /\ messages' = messages \ {OldFailure}
  /\ UNCHANGED <<currentTerm, leaderNext, leaderMatch, lastRequestId,
                  sent, delivered, acceptedCurrent, acceptedOldSuccess,
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
  /\ acceptedCurrent \subseteq {1, 2}
  /\ acceptedOldSuccess \in BOOLEAN
  /\ acceptedOldFailure \in BOOLEAN
  /\ messages \subseteq {OldSuccess, OldFailure, NewReq1, NewReq2, NewResp1, NewResp2}

CursorLowerBound == leaderNext >= leaderMatch + 1
NextWithinLeaderLog == leaderNext <= 6
MatchWithinLeaderLog == leaderMatch <= 5
OldSuccessNeverAccepted == acceptedOldSuccess = FALSE
OldFailureNeverAccepted == acceptedOldFailure = FALSE
OldResponsesNeverAccepted ==
  acceptedOldSuccess = FALSE /\ acceptedOldFailure = FALSE
DeliveryRequiresSend == delivered \subseteq sent
SentIdsMonotonic == (2 \in sent) => (1 \in sent)
AcceptedResponsesWereDelivered == acceptedCurrent \subseteq delivered
AcceptedCurrentIdsBounded == acceptedCurrent \subseteq {1, 2}

Spec == Init /\ [][Next]_vars
====
