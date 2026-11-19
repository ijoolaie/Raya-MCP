---- MODULE WitnessV2AppendEntriesFourResponseOrdersGuarded ----
EXTENDS Naturals, Sequences, FiniteSets, TLC
CONSTANT UseGuard

Success2 == [mid |-> 12, requestId |-> 2, success |-> TRUE, ackIndex |-> 2]
Success3 == [mid |-> 13, requestId |-> 3, success |-> TRUE, ackIndex |-> 3]
Failure4a == [mid |-> 41, requestId |-> 4, success |-> FALSE, ackIndex |-> 0]
Failure4b == [mid |-> 42, requestId |-> 4, success |-> FALSE, ackIndex |-> 0]
AllResponses == {Success2, Success3, Failure4a, Failure4b}

VARIABLES messages, matchIndex, nextIndex, lastRequestId, handled, order
vars == <<messages, matchIndex, nextIndex, lastRequestId, handled, order>>

Init ==
  /\ messages = AllResponses
  /\ matchIndex = 1
  /\ nextIndex = 5
  /\ lastRequestId = 4
  /\ handled = {}
  /\ order = <<>>

Handle(m) ==
  /\ m \in messages
  /\ IF m.success
       THEN /\ matchIndex' = IF m.ackIndex > matchIndex THEN m.ackIndex ELSE matchIndex
            /\ nextIndex' = IF m.ackIndex + 1 > nextIndex THEN m.ackIndex + 1 ELSE nextIndex
       ELSE /\ m.requestId = lastRequestId
            /\ nextIndex' = IF UseGuard
                              THEN IF nextIndex - 1 > matchIndex + 1
                                     THEN nextIndex - 1 ELSE matchIndex + 1
                              ELSE nextIndex - 1
            /\ UNCHANGED matchIndex
  /\ messages' = messages \ {m}
  /\ handled' = handled \cup {m.mid}
  /\ order' = Append(order, m.mid)
  /\ UNCHANGED lastRequestId

Next == \E m \in messages : Handle(m)

TypeOK ==
  /\ UseGuard \in BOOLEAN
  /\ messages \subseteq AllResponses
  /\ matchIndex \in 1..3
  /\ nextIndex \in 1..5
  /\ lastRequestId = 4
  /\ handled \subseteq {12, 13, 41, 42}
  /\ order \in Seq({12, 13, 41, 42})

CursorLowerBound == nextIndex >= matchIndex + 1
MatchWithinLeaderLog == matchIndex <= 3
NextWithinLeaderLog == nextIndex <= 5
AllFourHandled == messages = {} => Cardinality(handled) = 4
NoDuplicateAccounting == Cardinality(handled) = Len(order)

Spec == Init /\ [][Next]_vars
====
