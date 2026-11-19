---- MODULE WitnessV2AppendEntriesBaseSendSemantics ----
EXTENDS Naturals, FiniteSets, Sequences, TLC

LeaderLog == <<1, 2, 3>>
InitialFollowerLog == <<1>>

VARIABLES lastRequestId, requests, followerLog, responses,
          matchIndex, nextIndex, requestDeliveryOrder, responseOrder
vars == <<lastRequestId, requests, followerLog, responses,
          matchIndex, nextIndex, requestDeliveryOrder, responseOrder>>

PrevMatches(localLog, prevIndex, prevTerm) ==
  IF prevIndex = 0 THEN prevTerm = 0
  ELSE IF prevIndex <= Len(localLog)
       THEN localLog[prevIndex] = prevTerm
       ELSE FALSE

AppendResult(localLog, prevIndex, entries) ==
  LET conflicts ==
        {k \in 1..Len(entries) :
          prevIndex + k <= Len(localLog) /\
          localLog[prevIndex + k] # entries[k]}
  IN IF conflicts # {}
     THEN LET firstConflict == CHOOSE x \in conflicts :
                    \A y \in conflicts : x <= y
          IN SubSeq(localLog, 1, prevIndex + firstConflict - 1) \o
             SubSeq(entries, firstConflict, Len(entries))
     ELSE IF prevIndex + Len(entries) > Len(localLog)
          THEN localLog \o
               SubSeq(entries, Len(localLog) - prevIndex + 1, Len(entries))
          ELSE localLog
Init ==
  /\ lastRequestId = 0
  /\ requests = {}
  /\ followerLog = InitialFollowerLog
  /\ responses = {}
  /\ matchIndex = 0
  /\ nextIndex = 2
  /\ requestDeliveryOrder = <<>>
  /\ responseOrder = <<>>

SendAppend ==
  /\ lastRequestId < 2
  /\ LET rid == lastRequestId + 1
         prev == nextIndex - 1
         msg == [kind |-> "APPEND", requestId |-> rid,
                 prevIndex |-> prev, prevTerm |-> LeaderLog[prev],
                 entries |-> SubSeq(LeaderLog, prev + 1, Len(LeaderLog))]
     IN requests' = requests \cup {msg}
  /\ lastRequestId' = lastRequestId + 1
  /\ UNCHANGED <<followerLog, responses, matchIndex, nextIndex,
                 requestDeliveryOrder, responseOrder>>

DeliverAppend(m) ==
  /\ m \in requests
  /\ LET ok == PrevMatches(followerLog, m.prevIndex, m.prevTerm)
         newLog == IF ok THEN AppendResult(followerLog, m.prevIndex, m.entries) ELSE followerLog
         accepted == m.prevIndex + Len(m.entries)
         resp == [kind |-> "RESP", requestId |-> m.requestId,
                  success |-> ok, matchIndex |-> IF ok THEN accepted ELSE 0]
     IN /\ responses' = responses \cup {resp}
        /\ followerLog' = newLog
  /\ requests' = requests \ {m}
  /\ requestDeliveryOrder' = Append(requestDeliveryOrder, m.requestId)
  /\ UNCHANGED <<lastRequestId, matchIndex, nextIndex, responseOrder>>

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
  /\ UNCHANGED <<lastRequestId, requests, followerLog, requestDeliveryOrder>>

Next ==
  \/ SendAppend
  \/ \E m \in requests : DeliverAppend(m)
  \/ \E m \in responses : HandleResponse(m)
  \/ (requests = {} /\ responses = {} /\ lastRequestId = 2 /\ UNCHANGED vars)

Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ lastRequestId \in 0..2
  /\ requests \subseteq
       [kind : {"APPEND"}, requestId : 1..2,
        prevIndex : 0..3, prevTerm : 0..3, entries : Seq(1..3)]
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
NoMixedResponsesGenerated ==
  \A r \in responses :
    r.success = TRUE

====