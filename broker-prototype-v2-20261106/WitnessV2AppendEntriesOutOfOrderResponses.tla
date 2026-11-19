---- MODULE WitnessV2AppendEntriesOutOfOrderResponses ----
EXTENDS Naturals, FiniteSets, Sequences, TLC

VARIABLES phase, leaderLog, followerLog, matchIndex, messages,
          requestDeliveryOrder, responseOrder, matchIndexHistory
vars == <<phase, leaderLog, followerLog, matchIndex, messages,
          requestDeliveryOrder, responseOrder, matchIndexHistory>>

AppendResult(localLog, entries) ==
  IF Len(entries) > Len(localLog)
  THEN Append(localLog, entries[Len(localLog) + 1])
       \o SubSeq(entries, Len(localLog) + 2, Len(entries))
  ELSE localLog

Init ==
  /\ phase = 0
  /\ leaderLog = <<1>>
  /\ followerLog = <<>>
  /\ matchIndex = 0
  /\ messages = {}
  /\ requestDeliveryOrder = <<>>
  /\ responseOrder = <<>>
  /\ matchIndexHistory = <<>>

SendFirst ==
  /\ phase = 0
  /\ messages' = messages \cup
       {[kind |-> "APPEND", requestId |-> 1,
         prevIndex |-> 0, entries |-> <<1>>]}
  /\ phase' = 1
  /\ UNCHANGED <<leaderLog, followerLog, matchIndex, requestDeliveryOrder,
                 responseOrder, matchIndexHistory>>

AppendLeaderEntry ==
  /\ phase = 1
  /\ leaderLog = <<1>>
  /\ leaderLog' = Append(leaderLog, 2)
  /\ phase' = 2
  /\ UNCHANGED <<followerLog, matchIndex, messages, requestDeliveryOrder,
                 responseOrder, matchIndexHistory>>

SendSecond ==
  /\ phase = 2
  /\ messages' = messages \cup
       {[kind |-> "APPEND", requestId |-> 2,
         prevIndex |-> 0, entries |-> <<1, 2>>]}
  /\ phase' = 3
  /\ UNCHANGED <<leaderLog, followerLog, matchIndex, requestDeliveryOrder,
                 responseOrder, matchIndexHistory>>

DeliverAppend(m) ==
  /\ phase = 3
  /\ m \in messages
  /\ m.kind = "APPEND"
  /\ m.prevIndex = 0
  /\ followerLog' = AppendResult(followerLog, m.entries)
  /\ messages' = (messages \ {m}) \cup
       {[kind |-> "RESP", requestId |-> m.requestId,
         matchIndex |-> Len(m.entries)]}
  /\ requestDeliveryOrder' = Append(requestDeliveryOrder, m.requestId)
  /\ phase' = IF Cardinality({x \in messages : x.kind = "APPEND"}) = 1
              THEN 4 ELSE 3
  /\ UNCHANGED <<leaderLog, matchIndex, responseOrder, matchIndexHistory>>

HandleResponse(m) ==
  /\ phase = 4
  /\ m \in messages
  /\ m.kind = "RESP"
  /\ matchIndex' = IF m.matchIndex > matchIndex
                   THEN m.matchIndex ELSE matchIndex
  /\ responseOrder' = Append(responseOrder, m.requestId)
  /\ matchIndexHistory' = Append(matchIndexHistory, matchIndex')
  /\ messages' = messages \ {m}
  /\ phase' = IF Cardinality({x \in messages : x.kind = "RESP"}) = 1
              THEN 5 ELSE 4
  /\ UNCHANGED <<leaderLog, followerLog, requestDeliveryOrder>>

Next ==
  \/ SendFirst
  \/ AppendLeaderEntry
  \/ SendSecond
  \/ \E m \in messages : DeliverAppend(m)
  \/ \E m \in messages : HandleResponse(m)
  \/ (phase = 5 /\ UNCHANGED vars)

Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ phase \in 0..5
  /\ leaderLog \in Seq({1, 2})
  /\ followerLog \in Seq({1, 2})
  /\ Len(leaderLog) <= 2
  /\ Len(followerLog) <= 2
  /\ matchIndex \in 0..2
  /\ messages \subseteq
       ([kind : {"APPEND"}, requestId : {1, 2},
         prevIndex : {0}, entries : Seq({1, 2})]
        \cup
        [kind : {"RESP"}, requestId : {1, 2},
         matchIndex : 0..2])
  /\ requestDeliveryOrder \in Seq({1, 2})
  /\ responseOrder \in Seq({1, 2})
  /\ matchIndexHistory \in Seq(0..2)
  /\ Len(requestDeliveryOrder) <= 2
  /\ Len(responseOrder) <= 2
  /\ Len(matchIndexHistory) <= 2

MatchIndexWithinLeaderLog == matchIndex <= Len(leaderLog)

RequestDeliveryOrderWellFormed ==
  Cardinality({requestDeliveryOrder[i] :
    i \in 1..Len(requestDeliveryOrder)}) = Len(requestDeliveryOrder)

ResponseOrderWellFormed ==
  Cardinality({responseOrder[i] :
    i \in 1..Len(responseOrder)}) = Len(responseOrder)

MatchIndexHistoryMonotonic ==
  \A i \in 1..(Len(matchIndexHistory) - 1) :
    matchIndexHistory[i] <= matchIndexHistory[i + 1]

PhaseProtocolConsistent ==
  /\ (phase = 4 =>
       /\ {m.kind : m \in messages} = {"RESP"}
       /\ Cardinality(messages) \in 1..2)
  /\ (phase = 5 => messages = {} /\ Len(responseOrder) = 2)

====
