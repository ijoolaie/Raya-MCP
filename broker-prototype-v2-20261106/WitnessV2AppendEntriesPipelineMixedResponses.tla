---- MODULE WitnessV2AppendEntriesPipelineMixedResponses ----
EXTENDS Naturals, FiniteSets, Sequences, TLC

LeaderLog == <<1, 2, 3>>
InitialFollowerLog == <<1>>

VARIABLES sendCursor, lastRequestId, requests, followerLog,
          responses, matchIndex, nextIndex,
          requestDeliveryOrder, responseOrder
vars == <<sendCursor, lastRequestId, requests, followerLog,
          responses, matchIndex, nextIndex,
          requestDeliveryOrder, responseOrder>>

PrevMatches(localLog, prevIndex, prevTerm) ==
  IF prevIndex = 0
  THEN prevTerm = 0
  ELSE IF prevIndex <= Len(localLog)
       THEN localLog[prevIndex] = prevTerm
       ELSE FALSE

Init ==
  /\ sendCursor = 2
  /\ lastRequestId = 0
  /\ requests = {}
  /\ followerLog = InitialFollowerLog
  /\ responses = {}
  /\ matchIndex = 0
  /\ nextIndex = 2
  /\ requestDeliveryOrder = <<>>
  /\ responseOrder = <<>>

SendRequest ==
  /\ lastRequestId < 2
  /\ LET rid == lastRequestId + 1
         prev == sendCursor - 1
         msg == [kind |-> "APPEND", requestId |-> rid,
                 prevIndex |-> prev, prevTerm |-> LeaderLog[prev],
                 entries |-> <<LeaderLog[sendCursor]>>]
     IN requests' = requests \cup {msg}
  /\ lastRequestId' = lastRequestId + 1
  /\ sendCursor' = sendCursor + 1
  /\ UNCHANGED <<followerLog, responses, matchIndex, nextIndex,
                 requestDeliveryOrder, responseOrder>>

DeliverRequest(m) ==
  /\ m \in requests
  /\ LET ok == PrevMatches(followerLog, m.prevIndex, m.prevTerm)
         accepted == m.prevIndex + Len(m.entries)
         resp == [kind |-> "RESP", requestId |-> m.requestId,
                  success |-> ok, matchIndex |-> IF ok THEN accepted ELSE 0]
     IN /\ responses' = responses \cup {resp}
        /\ followerLog' = IF ok
             THEN Append(followerLog, m.entries[1])
             ELSE followerLog
  /\ requests' = requests \ {m}
  /\ requestDeliveryOrder' = Append(requestDeliveryOrder, m.requestId)
  /\ UNCHANGED <<sendCursor, lastRequestId, matchIndex, nextIndex, responseOrder>>

HandleResponse(m) ==
  /\ m \in responses
  /\ IF m.success
       THEN /\ matchIndex' = IF matchIndex > m.matchIndex THEN matchIndex ELSE m.matchIndex
            /\ nextIndex' = IF nextIndex > m.matchIndex + 1 THEN nextIndex ELSE m.matchIndex + 1
       ELSE /\ m.requestId = lastRequestId
            /\ nextIndex' = IF nextIndex > 1 THEN nextIndex - 1 ELSE 1
            /\ UNCHANGED matchIndex
  /\ responses' = responses \ {m}
  /\ responseOrder' = Append(responseOrder, m.requestId)
  /\ UNCHANGED <<sendCursor, lastRequestId, requests, followerLog,
                 requestDeliveryOrder>>

Next ==
  \/ SendRequest
  \/ \E m \in requests : DeliverRequest(m)
  \/ \E m \in responses : HandleResponse(m)
  \/ (requests = {} /\ responses = {} /\ lastRequestId = 2 /\ UNCHANGED vars)

Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ sendCursor \in 2..4
  /\ lastRequestId \in 0..2
  /\ requests \subseteq
       [kind : {"APPEND"}, requestId : 1..2,
        prevIndex : 1..2, prevTerm : 1..3, entries : Seq(1..3)]
  /\ followerLog \in Seq(1..3)
  /\ Len(followerLog) <= 3
  /\ responses \subseteq
       [kind : {"RESP"}, requestId : 1..2,
        success : BOOLEAN, matchIndex : 0..3]
  /\ matchIndex \in 0..3
  /\ nextIndex \in 1..4
  /\ requestDeliveryOrder \in Seq({1, 2})
  /\ Len(requestDeliveryOrder) <= 2
  /\ responseOrder \in Seq({1, 2})
  /\ Len(responseOrder) <= 2

MatchIndexWithinLeaderLog == matchIndex <= Len(LeaderLog)
NextIndexBound == nextIndex \in 1..4
RequestDeliveryOrderWellFormed ==
  Cardinality({requestDeliveryOrder[i] : i \in 1..Len(requestDeliveryOrder)})
    = Len(requestDeliveryOrder)
ResponseOrderWellFormed ==
  Cardinality({responseOrder[i] : i \in 1..Len(responseOrder)})
    = Len(responseOrder)
AllRequestsAndResponsesResolved ==
  (lastRequestId = 2 /\ requests = {} /\ responses = {})
    => /\ Len(requestDeliveryOrder) = 2
       /\ Len(responseOrder) = 2
FollowerLogBounded == Len(followerLog) <= Len(LeaderLog)

NextIndexNotBelowMatchIndex == nextIndex >= matchIndex + 1

====