---- MODULE WitnessV2AppendResponsePeerOfflineGuard ----
EXTENDS Naturals, FiniteSets, TLC
VARIABLES online, currentTerm, roleA, leaderNextA, leaderMatchA, lastRequestIdA, messages, acceptedFailure
vars == <<online, currentTerm, roleA, leaderNextA, leaderMatchA, lastRequestIdA, messages, acceptedFailure>>
OldFailure == [kind |-> "APPEND_RESP", src |-> "F", dst |-> "A", term |-> 3, requestId |-> 4, success |-> FALSE, matchIndex |-> 0]
Init == /\ online = {"A", "F"} /\ currentTerm = 3 /\ roleA = "Leader" /\ leaderNextA = 6 /\ leaderMatchA = 0 /\ lastRequestIdA = 4 /\ messages = {OldFailure} /\ acceptedFailure = FALSE
TakePeerOffline == /\ "F" \in online /\ online' = online \ {"F"} /\ UNCHANGED <<currentTerm, roleA, leaderNextA, leaderMatchA, lastRequestIdA, messages, acceptedFailure>>
HandleOldFailure == /\ OldFailure \in messages /\ "F" \notin online /\ "A" \in online /\ "F" \in online /\ roleA = "Leader" /\ OldFailure.term = currentTerm /\ OldFailure.requestId = lastRequestIdA /\ leaderNextA' = (IF leaderNextA > 1 THEN leaderNextA - 1 ELSE 1) /\ currentTerm' = currentTerm /\ roleA' = roleA /\ leaderMatchA' = leaderMatchA /\ lastRequestIdA' = lastRequestIdA /\ online' = online /\ messages' = messages \ {OldFailure} /\ acceptedFailure' = TRUE
DropOldFailure == /\ OldFailure \in messages /\ currentTerm' = currentTerm /\ roleA' = roleA /\ leaderNextA' = leaderNextA /\ leaderMatchA' = leaderMatchA /\ lastRequestIdA' = lastRequestIdA /\ online' = online /\ messages' = messages \ {OldFailure} /\ acceptedFailure' = acceptedFailure
Next == TakePeerOffline \/ HandleOldFailure \/ DropOldFailure
TypeOK == /\ online \subseteq {"A", "F"} /\ currentTerm = 3 /\ roleA \in {"Leader", "Follower"} /\ leaderNextA \in 1..6 /\ leaderMatchA = 0 /\ lastRequestIdA = 4 /\ messages \subseteq {OldFailure} /\ acceptedFailure \in BOOLEAN
CursorLowerBound == leaderNextA >= leaderMatchA + 1
OfflinePeerResponseNotAccepted == acceptedFailure = FALSE
Spec == Init /\ [][Next]_vars
====
