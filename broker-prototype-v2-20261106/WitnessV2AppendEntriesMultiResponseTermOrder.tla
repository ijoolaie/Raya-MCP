---- MODULE WitnessV2AppendEntriesMultiResponseTermOrder ----
EXTENDS Naturals, FiniteSets, TLC
CONSTANT UseTermGuard

Nodes == {"n1", "n2", "n3"}
Leader == "LEADER"
Follower == "FOLLOWER"

OldFailure ==
  [kind |-> "APPEND_RESP", src |-> "n2", dst |-> "n1",
   term |-> 1, requestId |-> 2, success |-> FALSE, matchIndex |-> 0]
NewSuccess1 ==
  [kind |-> "APPEND_RESP", src |-> "n2", dst |-> "n1",
   term |-> 3, requestId |-> 1, success |-> TRUE, matchIndex |-> 2]
NewFailure2 ==
  [kind |-> "APPEND_RESP", src |-> "n2", dst |-> "n1",
   term |-> 3, requestId |-> 2, success |-> FALSE, matchIndex |-> 0]

VARIABLES messages, currentTerm, role, lastRequestId, nextIndex,
          ackedIndex, handled, oldMutation
vars == <<messages, currentTerm, role, lastRequestId, nextIndex,
          ackedIndex, handled, oldMutation>>

Init ==
  /\ messages = {OldFailure, NewSuccess1, NewFailure2}
  /\ currentTerm = [n \in Nodes |-> IF n = "n1" THEN 3 ELSE 3]
  /\ role = [n \in Nodes |-> IF n = "n1" THEN Leader ELSE Follower]
  /\ lastRequestId = [n \in Nodes |-> [p \in Nodes |-> IF n = "n1" /\ p = "n2" THEN 2 ELSE 0]]
  /\ nextIndex = [n \in Nodes |-> [p \in Nodes |-> IF n = "n1" /\ p = "n2" THEN 2 ELSE 1]]
  /\ ackedIndex = [n \in Nodes |-> [p \in Nodes |-> 0]]
  /\ handled = {}
  /\ oldMutation = FALSE

HandleOldFailure ==
  /\ OldFailure \in messages
  /\ role["n1"] = Leader
  /\ IF UseTermGuard THEN OldFailure.term = currentTerm["n1"] ELSE TRUE
  /\ OldFailure.requestId = lastRequestId["n1"]["n2"]
  /\ nextIndex' = [nextIndex EXCEPT !["n1"]["n2"] = IF @ > 1 THEN @ - 1 ELSE 1]
  /\ messages' = messages \ {OldFailure}
  /\ handled' = handled \cup {"old"}
  /\ oldMutation' = TRUE
  /\ UNCHANGED <<currentTerm, role, lastRequestId, ackedIndex>>

HandleNewSuccess ==
  /\ NewSuccess1 \in messages
  /\ role["n1"] = Leader
  /\ NewSuccess1.term = currentTerm["n1"]
  /\ ackedIndex' = [ackedIndex EXCEPT !["n1"]["n2"] = IF NewSuccess1.matchIndex > @ THEN NewSuccess1.matchIndex ELSE @]
  /\ nextIndex' = [nextIndex EXCEPT !["n1"]["n2"] = IF NewSuccess1.matchIndex + 1 > @ THEN NewSuccess1.matchIndex + 1 ELSE @]
  /\ messages' = messages \ {NewSuccess1}
  /\ handled' = handled \cup {"success1"}
  /\ UNCHANGED <<currentTerm, role, lastRequestId, oldMutation>>

HandleNewFailure ==
  /\ NewFailure2 \in messages
  /\ role["n1"] = Leader
  /\ NewFailure2.term = currentTerm["n1"]
  /\ NewFailure2.requestId = lastRequestId["n1"]["n2"]
  /\ nextIndex' = [nextIndex EXCEPT !["n1"]["n2"] = IF @ > 1 THEN @ - 1 ELSE 1]
  /\ messages' = messages \ {NewFailure2}
  /\ handled' = handled \cup {"failure2"}
  /\ UNCHANGED <<currentTerm, role, lastRequestId, ackedIndex, oldMutation>>

Next ==
  \/ HandleOldFailure
  \/ HandleNewSuccess
  \/ HandleNewFailure

TypeOK ==
  /\ UseTermGuard \in BOOLEAN
  /\ messages \subseteq {OldFailure, NewSuccess1, NewFailure2}
  /\ currentTerm \in [Nodes -> {3}]
  /\ role \in [Nodes -> {Leader, Follower}]
  /\ lastRequestId \in [Nodes -> [Nodes -> 0..2]]
  /\ nextIndex \in [Nodes -> [Nodes -> 1..3]]
  /\ ackedIndex \in [Nodes -> [Nodes -> 0..2]]
  /\ handled \subseteq {"old", "success1", "failure2"}
  /\ oldMutation \in BOOLEAN

OldTermResponseNeverMutates ==
  oldMutation = FALSE

CursorLowerBound ==
  ackedIndex["n1"]["n2"] >= 0 /\ nextIndex["n1"]["n2"] >= ackedIndex["n1"]["n2"] + 1

AllCurrentResponsesCanBeHandled ==
  handled \subseteq {"success1", "failure2"}

Spec == Init /\ [][Next]_vars
====