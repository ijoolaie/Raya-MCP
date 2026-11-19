---- MODULE WitnessV2AppendEntriesOldResponseWithTwoNewRequests ----
EXTENDS Naturals, FiniteSets, TLC

VARIABLES currentTerm, leaderNext, leaderMatch, lastRequestId,
          messages, sent, delivered
vars == <<currentTerm, leaderNext, leaderMatch, lastRequestId,
          messages, sent, delivered>>

OldFailure == [kind |-> "APPEND_RESP", term |-> 1,
               requestId |-> 2, success |-> FALSE, matchIndex |-> 0]
NewReq1 == [kind |-> "APPEND", term |-> 2, requestId |-> 1,
            prevIndex |-> 3, prevTerm |-> 2, entries |-> <<4>>]
NewReq2 == [kind |-> "APPEND", term |-> 2, requestId |-> 2,
            prevIndex |-> 4, prevTerm |-> 3, entries |-> <<5>>]
NewResp1 == [kind |-> "APPEND_RESP", term |-> 2, requestId |-> 1,
             success |-> TRUE, matchIndex |-> 4]
NewResp2 == [kind |-> "APPEND_RESP", term |-> 2, requestId |-> 2,
             success |-> TRUE, matchIndex |-> 5]

Init ==
  /\ currentTerm = 2
  /\ leaderNext = 6
  /\ leaderMatch = 0
  /\ lastRequestId = 0
  /\ messages = {OldFailure}
  /\ sent = {}
  /\ delivered = {}

SendReq1 ==
  /\ 1 \notin sent
  /\ lastRequestId = 0
  /\ messages' = messages \cup {NewReq1}
  /\ sent' = sent \cup {1}
  /\ lastRequestId' = 1
  /\ UNCHANGED <<currentTerm, leaderNext, leaderMatch, delivered>>

SendReq2 ==
  /\ 1 \in sent
  /\ 2 \notin sent
  /\ lastRequestId = 1
  /\ messages' = messages \cup {NewReq2}
  /\ sent' = sent \cup {2}
  /\ lastRequestId' = 2
  /\ UNCHANGED <<currentTerm, leaderNext, leaderMatch, delivered>>

DeliverReq1 ==
  /\ NewReq1 \in messages
  /\ NewReq1.term = currentTerm
  /\ messages' = (messages \ {NewReq1}) \cup {NewResp1}
  /\ delivered' = delivered \cup {1}
  /\ UNCHANGED <<currentTerm, leaderNext, leaderMatch, lastRequestId, sent>>

DeliverReq2 ==
  /\ NewReq2 \in messages
  /\ NewReq2.term = currentTerm
  /\ messages' = (messages \ {NewReq2}) \cup {NewResp2}
  /\ delivered' = delivered \cup {2}
  /\ UNCHANGED <<currentTerm, leaderNext, leaderMatch, lastRequestId, sent>>

HandleResp1 ==
  /\ NewResp1 \in messages
  /\ NewResp1.term = currentTerm
  /\ leaderMatch' = IF NewResp1.matchIndex > leaderMatch
                    THEN NewResp1.matchIndex ELSE leaderMatch
  /\ leaderNext' = IF NewResp1.matchIndex + 1 > leaderNext
                   THEN NewResp1.matchIndex + 1 ELSE leaderNext
  /\ messages' = messages \ {NewResp1}
  /\ UNCHANGED <<currentTerm, lastRequestId, sent, delivered>>

HandleResp2 ==
  /\ NewResp2 \in messages
  /\ NewResp2.term = currentTerm
  /\ leaderMatch' = IF NewResp2.matchIndex > leaderMatch
                    THEN NewResp2.matchIndex ELSE leaderMatch
  /\ leaderNext' = IF NewResp2.matchIndex + 1 > leaderNext
                   THEN NewResp2.matchIndex + 1 ELSE leaderNext
  /\ messages' = messages \ {NewResp2}
  /\ UNCHANGED <<currentTerm, lastRequestId, sent, delivered>>

HandleOldFailure ==
  /\ OldFailure \in messages
  /\ OldFailure.term = currentTerm
  /\ OldFailure.requestId = lastRequestId
  /\ leaderNext' = IF leaderNext - 1 > leaderMatch + 1
                   THEN leaderNext - 1 ELSE leaderMatch + 1
  /\ messages' = messages \ {OldFailure}
  /\ UNCHANGED <<currentTerm, leaderMatch, lastRequestId, sent, delivered>>

DropOldFailure ==
  /\ OldFailure \in messages
  /\ OldFailure.term < currentTerm
  /\ messages' = messages \ {OldFailure}
  /\ UNCHANGED <<currentTerm, leaderNext, leaderMatch, lastRequestId, sent, delivered>>

Next ==
  \/ SendReq1 \/ SendReq2
  \/ DeliverReq1 \/ DeliverReq2
  \/ HandleResp1 \/ HandleResp2
  \/ HandleOldFailure \/ DropOldFailure

TypeOK ==
  /\ currentTerm = 2
  /\ leaderNext \in 1..6
  /\ leaderMatch \in 0..5
  /\ lastRequestId \in 0..2
  /\ sent \subseteq {1, 2}
  /\ delivered \subseteq {1, 2}
  /\ messages \subseteq {OldFailure, NewReq1, NewReq2, NewResp1, NewResp2}
CursorLowerBound == leaderNext >= leaderMatch + 1
NextWithinLeaderLog == leaderNext <= 6
MatchWithinLeaderLog == leaderMatch <= 5
OldTermFailureCannotAffectCursor == leaderNext = 6
DeliveryRequiresSend == delivered \subseteq sent
SentIdsMonotonic == (2 \in sent) => (1 \in sent)
ResponseProgressBounded == leaderMatch \in {0, 4, 5}

Spec == Init /\ [][Next]_vars
====