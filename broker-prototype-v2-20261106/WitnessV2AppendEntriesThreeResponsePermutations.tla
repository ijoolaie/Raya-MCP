---- MODULE WitnessV2AppendEntriesThreeResponsePermutations ----
EXTENDS Naturals, Sequences, FiniteSets, TLC
VARIABLES matchIndex, nextIndex, lastRequestId, messages, responseOrder
vars == <<matchIndex, nextIndex, lastRequestId, messages, responseOrder>>
Max(a,b) == IF a >= b THEN a ELSE b
Init ==
  /\ matchIndex = 0
  /\ nextIndex = 3
  /\ lastRequestId = 3
  /\ messages = {
       [requestId |-> 1, success |-> TRUE, ack |-> 1],
       [requestId |-> 2, success |-> TRUE, ack |-> 2],
       [requestId |-> 3, success |-> FALSE, ack |-> 0]}
  /\ responseOrder = <<>>
Handle(m) ==
  /\ m \in messages
  /\ IF m.success
       THEN /\ matchIndex' = Max(matchIndex, m.ack)
            /\ nextIndex' = Max(nextIndex, m.ack + 1)
       ELSE /\ m.requestId = lastRequestId
            /\ nextIndex' = Max(nextIndex - 1, matchIndex + 1)
            /\ UNCHANGED matchIndex
  /\ messages' = messages \ {m}
  /\ responseOrder' = Append(responseOrder,
       [requestId |-> m.requestId, success |-> m.success, ack |-> m.ack])
  /\ UNCHANGED lastRequestId
Next == \E m \in messages : Handle(m)
Spec == Init /\ [][Next]_vars
TypeOK ==
  /\ matchIndex \in 0..2
  /\ nextIndex \in 1..3
  /\ lastRequestId = 3
  /\ messages \subseteq [requestId : 1..3, success : BOOLEAN, ack : 0..2]
  /\ responseOrder \in Seq([requestId : 1..3, success : BOOLEAN, ack : 0..2])
MatchWithinLeaderLog == matchIndex <= 2
NextNotBelowMatch == nextIndex >= matchIndex + 1
NextWithinLeaderLog == nextIndex <= 3
AllThreeHandled == messages = {} => Len(responseOrder) = 3
====
