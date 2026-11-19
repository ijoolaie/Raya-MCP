---- MODULE WitnessV2AppendEntriesOldFailureWhilePeerOfflineWithoutPeerOnlineGuard ----
EXTENDS Naturals, FiniteSets, TLC
VARIABLES online, currentTerm, roleA, lastRequestIdA, messages, acceptedOldFailure, acceptedWhilePeerOffline
vars == <<online, currentTerm, roleA, lastRequestIdA, messages, acceptedOldFailure, acceptedWhilePeerOffline>>
OldFailure == [kind |-> "APPEND_RESP", src |-> "F", dst |-> "A", term |-> 3, requestId |-> 4, success |-> FALSE, matchIndex |-> 0]
Init == /\ online = {"A", "F"} /\ currentTerm = 3 /\ roleA = "Leader" /\ lastRequestIdA = 4 /\ messages = {OldFailure} /\ acceptedOldFailure = FALSE /\ acceptedWhilePeerOffline = FALSE
TakePeerOffline == /\ online = {"A", "F"} /\ acceptedOldFailure = FALSE /\ online' = {"A"} /\ UNCHANGED <<currentTerm, roleA, lastRequestIdA, messages, acceptedOldFailure, acceptedWhilePeerOffline>>
RestorePeer == /\ online = {"A"} /\ acceptedOldFailure = FALSE /\ online' = {"A", "F"} /\ UNCHANGED <<currentTerm, roleA, lastRequestIdA, messages, acceptedOldFailure, acceptedWhilePeerOffline>>
HandleOldFailure == /\ OldFailure \in messages /\ "A" \in online /\ roleA = "Leader" /\ OldFailure.term = currentTerm /\ OldFailure.requestId = lastRequestIdA /\ online' = online /\ currentTerm' = currentTerm /\ roleA' = roleA /\ lastRequestIdA' = lastRequestIdA /\ messages' = messages \ {OldFailure} /\ acceptedOldFailure' = TRUE /\ acceptedWhilePeerOffline' = ~("F" \in online)
DropOldFailure == /\ OldFailure \in messages /\ online' = online /\ currentTerm' = currentTerm /\ roleA' = roleA /\ lastRequestIdA' = lastRequestIdA /\ messages' = messages \ {OldFailure} /\ acceptedOldFailure' = acceptedOldFailure /\ acceptedWhilePeerOffline' = acceptedWhilePeerOffline
Next == TakePeerOffline \/ RestorePeer \/ HandleOldFailure \/ DropOldFailure
TypeOK == /\ online \subseteq {"A", "F"} /\ currentTerm = 3 /\ roleA \in {"Leader", "Follower"} /\ lastRequestIdA = 4 /\ messages \subseteq {OldFailure} /\ acceptedOldFailure \in BOOLEAN /\ acceptedWhilePeerOffline \in BOOLEAN
NoResponseAcceptedWhilePeerOffline == acceptedWhilePeerOffline = FALSE
AcceptedResponseRequiresPeerOnline == acceptedOldFailure => "F" \in online
Spec == Init /\ [][Next]_vars
====
