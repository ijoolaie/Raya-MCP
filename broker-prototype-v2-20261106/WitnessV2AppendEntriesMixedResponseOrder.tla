---- MODULE WitnessV2AppendEntriesMixedResponseOrder ----
EXTENDS Naturals, FiniteSets, Sequences, TLC

VARIABLES matchIndex, nextIndex, lastRequestId, messages, responseOrder
vars == <<matchIndex, nextIndex, lastRequestId, messages, responseOrder>>

Init ==
  /\ matchIndex = 0
  /\ nextIndex = 2
  /\ lastRequestId = 2
  /\ messages =
       {[kind |-> "RESP", requestId |-> 1,
         success |-> TRUE, matchIndex |-> 1],
        [kind |-> "RESP", requestId |-> 2,
         success |-> FALSE, matchIndex |-> 0]}
  /\ responseOrder = <<>>

HandleResponse(m) ==
  /\ m \in messages
  /\ IF m.success
        THEN
          /\ matchIndex' =
               IF m.matchIndex > matchIndex THEN m.matchIndex ELSE matchIndex
          /\ nextIndex' =
               IF m.matchIndex + 1 > nextIndex THEN m.matchIndex + 1 ELSE nextIndex
        ELSE
          /\ m.requestId = lastRequestId
          /\ nextIndex' = IF nextIndex > 1 THEN nextIndex - 1 ELSE 1
          /\ UNCHANGED matchIndex
  /\ messages' = messages \ {m}
  /\ responseOrder' = Append(responseOrder, m.requestId)
  /\ UNCHANGED lastRequestId

Next ==
  \/ \E m \in messages : HandleResponse(m)
  \/ (messages = {} /\ UNCHANGED vars)

Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ matchIndex \in 0..2
  /\ nextIndex \in 1..3
  /\ lastRequestId \in 0..2
  /\ messages \subseteq
       [kind : {"RESP"}, requestId : {1, 2},
        success : BOOLEAN, matchIndex : 0..2]
  /\ responseOrder \in Seq({1, 2})
  /\ Len(responseOrder) <= 2

MatchIndexWithinLeaderLog == matchIndex <= 2
NextIndexBound == nextIndex \in 1..3
ResponseOrderWellFormed ==
  Cardinality({responseOrder[i] : i \in 1..Len(responseOrder)})
    = Len(responseOrder)
AllResponsesHandled == messages = {} => Len(responseOrder) = 2
NextIndexNotBelowMatchIndex == nextIndex >= matchIndex + 1

====
