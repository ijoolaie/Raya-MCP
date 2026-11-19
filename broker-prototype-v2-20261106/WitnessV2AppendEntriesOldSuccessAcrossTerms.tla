---- MODULE WitnessV2AppendEntriesOldSuccessAcrossTerms ----
EXTENDS Naturals, FiniteSets, TLC

VARIABLES currentTerm, leaderNext, leaderMatch, lastRequestId,
          messages, sent, delivered, acceptedCurrent, acceptedOldSuccess
vars == <<currentTerm, leaderNext, leaderMatch, lastRequestId,
          messages, sent, delivered, acceptedCurrent, acceptedOldSuccess>>

OldSuccess == [kind |-> "APPEND_RESP", term |-> 1,
               requestId |-> 2, success |-> TRUE, matchIndex |-> 5]
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
  /\ messages = {OldSuccess}
  /\ sent = {}
  /\ delivered = {}
  /\ acceptedCurrent = {}
  /\ acceptedOldSuccess = FALSE

SendReq1 ==
  /\ 1 \notin sent
  /\ lastRequestId = 0
  /\ messages' = messages \cup {NewReq1}
  /\ sent' = sent \cup {1}
  /\ lastRequestId' = 1
  /\ UNCHANGED <<currentTerm, leaderNext, leaderMatch, delivered,
                  acceptedCurrent, acceptedOldSuccess>>

SendReq2 ==
  /\ 1 \in sent
  /\ 2 \notin sent
  /\ lastRequestId = 1
  /\ messages' = messages \cup {NewReq2}
  /\ sent' = sent \cup {2}
  /\ lastRequestId' = 2
  /\ UNCHANGED <<currentTerm, leaderNext, leaderMatch, delivered,
                  acceptedCurrent, acceptedOldSuccess>>

DeliverReq1 ==
  /\ NewReq1 \in messages
  /\ NewReq1.term = currentTerm
  /\ messages' = (messages \ {NewReq1}) \cup {NewResp1}
  /\ delivered' = delivered \cup {1}
  /\ UNCHANGED <<currentTerm, leaderNext, leaderMatch, lastRequestId,
                  sent, acceptedCurrent, acceptedOldSuccess>>

DeliverReq2 ==
  /\ NewReq2 \in messages
  /\ NewReq2.term = currentTerm
  /\ messages' = (messages \ {NewReq2}) \cup {NewResp2}
  /\ delivered' = delivered \cup {2}
  /\ UNCHANGED <<currentTerm, leaderNext, leaderMatch, lastRequestId,
                  sent, acceptedCurrent, acceptedOldSuccess>>

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
  /\ UNCHANGED <<currentTerm, lastRequestId, sent, delivered>>

DropOldSuccess ==
  /\ OldSuccess \in messages
  /\ OldSuccess.term < currentTerm
  /\ messages' = messages \ {OldSuccess}
  /\ UNCHANGED <<currentTerm, leaderNext, leaderMatch, lastRequestId,
                  sent, delivered, acceptedCurrent, acceptedOldSuccess>>

Next ==
  \/ SendReq1 \/ SendReq2
  \/ DeliverReq1 \/ DeliverReq2
  \/ \E m \in messages : HandleSuccess(m)
  \/ DropOldSuccess

TypeOK ==
  /\ currentTerm = 2
  /\ leaderNext \in 1..6
  /\ leaderMatch \in 0..5
  /\ lastRequestId \in 0..2
  /\ sent \subseteq {1, 2}
  /\ delivered \subseteq {1, 2}
  /\ acceptedCurrent \subseteq {1, 2}
  /\ acceptedOldSuccess \in BOOLEAN
  /\ messages \subseteq {OldSuccess, NewReq1, NewReq2, NewResp1, NewResp2}

CursorLowerBound == leaderNext >= leaderMatch + 1
NextWithinLeaderLog == leaderNext <= 6
MatchWithinLeaderLog == leaderMatch <= 5
OldSuccessNeverAccepted == acceptedOldSuccess = FALSE
OldTermSuccessRemainsUnaccepted ==
  (OldSuccess \in messages) => (acceptedOldSuccess = FALSE)
DeliveryRequiresSend == delivered \subseteq sent
SentIdsMonotonic == (2 \in sent) => (1 \in sent)
AcceptedResponsesWereDelivered == acceptedCurrent \subseteq delivered
AcceptedCurrentIdsBounded == acceptedCurrent \subseteq {1, 2}

Spec == Init /\ [][Next]_vars
====
