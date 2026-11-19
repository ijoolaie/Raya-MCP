---- MODULE WitnessV2AppendEntriesDeliverSameTermAppendThenOldSuccess ----
EXTENDS Naturals, FiniteSets, TLC
VARIABLES currentTerm, roleA, leaderNextA, leaderMatchA, lastRequestIdA, messages, acceptedOldSuccess
vars == <<currentTerm, roleA, leaderNextA, leaderMatchA, lastRequestIdA, messages, acceptedOldSuccess>>
OldSuccess == [kind |-> "APPEND_RESP", src |-> "F", dst |-> "A", term |-> 3, requestId |-> 4, success |-> TRUE, matchIndex |-> 8]
SameTermAppend == [kind |-> "APPEND", src |-> "B", dst |-> "A", term |-> 3, requestId |-> 2, prevIndex |-> 0, prevTerm |-> 0, entries |-> <<>>, leaderCommit |-> 0]
Init == /\ currentTerm = 3 /\ roleA = "Leader" /\ leaderNextA = 9 /\ leaderMatchA = 0 /\ lastRequestIdA = 4 /\ messages = {OldSuccess, SameTermAppend} /\ acceptedOldSuccess = FALSE
DeliverSameTermAppend == /\ SameTermAppend \in messages /\ SameTermAppend.term >= currentTerm /\ SameTermAppend.prevIndex = 0 /\ SameTermAppend.prevTerm = 0 /\ currentTerm' = SameTermAppend.term /\ roleA' = "Follower" /\ leaderNextA' = leaderNextA /\ leaderMatchA' = leaderMatchA /\ lastRequestIdA' = lastRequestIdA /\ messages' = (messages \ {SameTermAppend}) \cup {[kind |-> "APPEND_RESP", src |-> "A", dst |-> "B", term |-> 3, requestId |-> 2, success |-> TRUE, matchIndex |-> 0]} /\ acceptedOldSuccess' = acceptedOldSuccess
HandleOldSuccess == /\ OldSuccess \in messages /\ SameTermAppend \notin messages /\ roleA = "Leader" /\ OldSuccess.term = currentTerm /\ currentTerm' = currentTerm /\ roleA' = roleA /\ leaderNextA' = (IF OldSuccess.matchIndex + 1 > leaderNextA THEN OldSuccess.matchIndex + 1 ELSE leaderNextA) /\ leaderMatchA' = (IF OldSuccess.matchIndex > leaderMatchA THEN OldSuccess.matchIndex ELSE leaderMatchA) /\ lastRequestIdA' = lastRequestIdA /\ messages' = messages \ {OldSuccess} /\ acceptedOldSuccess' = TRUE
DropOldSuccess == /\ OldSuccess \in messages /\ currentTerm' = currentTerm /\ roleA' = roleA /\ leaderNextA' = leaderNextA /\ leaderMatchA' = leaderMatchA /\ lastRequestIdA' = lastRequestIdA /\ messages' = messages \ {OldSuccess} /\ acceptedOldSuccess' = acceptedOldSuccess
Next == DeliverSameTermAppend \/ HandleOldSuccess \/ DropOldSuccess
TypeOK == /\ currentTerm = 3 /\ roleA \in {"Leader", "Follower"} /\ leaderNextA \in 1..9 /\ leaderMatchA \in 0..8 /\ lastRequestIdA = 4 /\ messages \subseteq {OldSuccess, SameTermAppend, [kind |-> "APPEND_RESP", src |-> "A", dst |-> "B", term |-> 3, requestId |-> 2, success |-> TRUE, matchIndex |-> 0]} /\ acceptedOldSuccess \in BOOLEAN
CursorLowerBound == leaderNextA >= leaderMatchA + 1
OldSuccessNotAppliedAfterDemotion == acceptedOldSuccess = FALSE
Spec == Init /\ [][Next]_vars
====