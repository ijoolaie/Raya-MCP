---- MODULE WitnessV2AppendEntriesDelayedSuccessAfterFailure ----
EXTENDS Naturals, FiniteSets, Sequences, TLC

VARIABLES matchIndex, nextIndex, lastRequestId, messages, responseOrder
vars == <<matchIndex, nextIndex, lastRequestId, messages, responseOrder>>

Max(a,b) == IF a >= b THEN a ELSE b

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
          /\ matchIndex' = Max(matchIndex, m.matchIndex)
          /\ nextIndex' = Max(nextIndex, m.matchIndex + 1)
        ELSE
          /\ m.requestId = lastRequestId
          /\ nextIndex' = Max(nextIndex - 1, matchIndex + 1)
          /\ UNCHANGED matchIndex
  /\ messages' = messages \ {m}
  /\ responseOrder' = Append(responseOrder,
       [requestId |-> m.requestId, success |-> m.success])
  /\ UNCHANGED lastRequestId


Next ==
  \/ \E m \in messages : HandleResponse(m)
  \/ (messages = {} /\ UNCHANGED vars)

Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ matchIndex \in 0..2
  /\ nextIndex \in 1..3
  /\ lastRequestId = 2
  /\ messages \subseteq
       [kind : {"RESP"}, requestId : {1, 2},
        success : BOOLEAN, matchIndex : 0..2]
  /\ responseOrder \in Seq([requestId : {1, 2}, success : BOOLEAN])
  /\ Len(responseOrder) <= 2

MatchIndexWithinLeaderLog == matchIndex <= 2
NextIndexBound == nextIndex \in 1..3
NextIndexNotBelowMatchIndex == nextIndex >= matchIndex + 1
ResponseOrderWellFormed ==
  Cardinality({responseOrder[i].requestId : i \in 1..Len(responseOrder)})
    = Len(responseOrder)
AllResponsesHandled == messages = {} => Len(responseOrder) = 2
FailureThenDelayedSuccessReached ==
  /\ Len(responseOrder) = 2
  /\ responseOrder[1].requestId = 2
  /\ responseOrder[1].success = FALSE
  /\ responseOrder[2].requestId = 1
  /\ responseOrder[2].success = TRUE

FailureThenDelayedSuccessNotReached == ~FailureThenDelayedSuccessReached

====
