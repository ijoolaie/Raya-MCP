---- MODULE WitnessV2AppendEntriesDeliverSameTermAppendThenOldFailure ----
EXTENDS Naturals, FiniteSets, TLC
VARIABLES currentTerm, roleA, leaderNextA, leaderMatchA, lastRequestIdA, messages, acceptedOldFailure
vars == <<currentTerm, roleA, leaderNextA, leaderMatchA, lastRequestIdA, messages, acceptedOldFailure>>
OldFailure == [kind |-> "APPEND_RESP", src |-> "F", dst |-> "A", term |-> 3, requestId |-> 4, success |-> FALSE, matchIndex |-> 0]
SameTermAppend == [kind |-> "APPEND", src |-> "B", dst |-> "A", term |-> 3, requestId |-> 2, prevIndex |-> 0, prevTerm |-> 0, entries |-> <<>>, leaderCommit |-> 0]
Init == /\ currentTerm = 3 /\ roleA = "Leader" /\ leaderNextA = 6 /\ leaderMatchA = 0 /\ lastRequestIdA = 4 /\ messages = {OldFailure, SameTermAppend} /\ acceptedOldFailure = FALSE
DeliverSameTermAppend == /\ SameTermAppend \in messages /\ SameTermAppend.term >= currentTerm /\ SameTermAppend.prevIndex = 0 /\ SameTermAppend.prevTerm = 0 /\ currentTerm' = SameTermAppend.term /\ roleA' = "Follower" /\ leaderNextA' = leaderNextA /\ leaderMatchA' = leaderMatchA /\ lastRequestIdA' = lastRequestIdA /\ messages' = (messages \ {SameTermAppend}) \cup {[kind |-> "APPEND_RESP", src |-> "A", dst |-> "B", term |-> 3, requestId |-> 2, success |-> TRUE, matchIndex |-> 0]} /\ acceptedOldFailure' = acceptedOldFailure
HandleOldFailure == /\ OldFailure \in messages /\ SameTermAppend \notin messages /\ roleA = "Leader" /\ OldFailure.term = currentTerm /\ OldFailure.requestId = lastRequestIdA /\ currentTerm' = currentTerm /\ roleA' = roleA /\ leaderNextA' = (IF leaderNextA > 1 THEN leaderNextA - 1 ELSE 1) /\ leaderMatchA' = leaderMatchA /\ lastRequestIdA' = lastRequestIdA /\ messages' = messages \ {OldFailure} /\ acceptedOldFailure' = TRUE
DropOldFailure == /\ OldFailure \in messages /\ currentTerm' = currentTerm /\ roleA' = roleA /\ leaderNextA' = leaderNextA /\ leaderMatchA' = leaderMatchA /\ lastRequestIdA' = lastRequestIdA /\ messages' = messages \ {OldFailure} /\ acceptedOldFailure' = acceptedOldFailure
Next == DeliverSameTermAppend \/ HandleOldFailure \/ DropOldFailure
TypeOK == /\ currentTerm = 3 /\ roleA \in {"Leader", "Follower"} /\ leaderNextA \in 1..6 /\ leaderMatchA = 0 /\ lastRequestIdA = 4 /\ messages \subseteq {OldFailure, SameTermAppend, [kind |-> "APPEND_RESP", src |-> "A", dst |-> "B", term |-> 3, requestId |-> 2, success |-> TRUE, matchIndex |-> 0]} /\ acceptedOldFailure \in BOOLEAN
CursorLowerBound == leaderNextA >= leaderMatchA + 1
OldFailureNotAppliedAfterDemotion == acceptedOldFailure = FALSE
DemotionPhaseConsistent == roleA = "Leader" => SameTermAppend \in messages
Spec == Init /\ [][Next]_vars
====
