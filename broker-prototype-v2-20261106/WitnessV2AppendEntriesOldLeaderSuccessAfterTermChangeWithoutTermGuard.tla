---- MODULE WitnessV2AppendEntriesOldLeaderSuccessAfterTermChangeWithoutTermGuard ----
EXTENDS Naturals, FiniteSets, TLC
VARIABLES currentTerm, role, leaderNext, leaderMatch, lastRequestId, messages, acceptedStaleSuccess
vars == <<currentTerm, role, leaderNext, leaderMatch, lastRequestId, messages, acceptedStaleSuccess>>
OldLeaderSuccess == [kind |-> "APPEND_RESP", term |-> 2, requestId |-> 7, success |-> TRUE, matchIndex |-> 8]
Init == /\ currentTerm = 2 /\ role = "Leader" /\ leaderNext = 9 /\ leaderMatch = 0 /\ lastRequestId = 7 /\ messages = {OldLeaderSuccess} /\ acceptedStaleSuccess = FALSE
ObserveHigherTerm == /\ currentTerm = 2 /\ currentTerm' = 3 /\ role' = "Follower" /\ leaderNext' = leaderNext /\ leaderMatch' = leaderMatch /\ lastRequestId' = lastRequestId /\ messages' = messages /\ acceptedStaleSuccess' = acceptedStaleSuccess
AssumeNewLeader == /\ currentTerm = 3 /\ role = "Follower" /\ currentTerm' = currentTerm /\ role' = "Leader" /\ leaderNext' = 4 /\ leaderMatch' = 0 /\ lastRequestId' = 0 /\ messages' = messages /\ acceptedStaleSuccess' = acceptedStaleSuccess
HandleOldSuccess == /\ OldLeaderSuccess \in messages /\ OldLeaderSuccess.kind = "APPEND_RESP" /\ OldLeaderSuccess.success = TRUE /\ role = "Leader" /\ currentTerm = 3 /\ TRUE /\ currentTerm' = currentTerm /\ role' = role /\ leaderMatch' = (IF OldLeaderSuccess.matchIndex > leaderMatch THEN OldLeaderSuccess.matchIndex ELSE leaderMatch) /\ leaderNext' = (IF OldLeaderSuccess.matchIndex + 1 > leaderNext THEN OldLeaderSuccess.matchIndex + 1 ELSE leaderNext) /\ messages' = messages \ {OldLeaderSuccess} /\ acceptedStaleSuccess' = TRUE /\ lastRequestId' = lastRequestId
DropStaleSuccess == /\ OldLeaderSuccess \in messages /\ currentTerm' = currentTerm /\ role' = role /\ leaderNext' = leaderNext /\ leaderMatch' = leaderMatch /\ lastRequestId' = lastRequestId /\ messages' = messages \ {OldLeaderSuccess} /\ acceptedStaleSuccess' = acceptedStaleSuccess
Next == ObserveHigherTerm \/ AssumeNewLeader \/ HandleOldSuccess \/ DropStaleSuccess
TypeOK == /\ currentTerm \in {2, 3} /\ role \in {"Leader", "Follower"} /\ leaderNext \in 1..9 /\ leaderMatch \in 0..8 /\ lastRequestId \in 0..7 /\ messages \subseteq {OldLeaderSuccess} /\ acceptedStaleSuccess \in BOOLEAN
CursorLowerBound == leaderNext >= leaderMatch + 1
StaleSuccessNeverAccepted == acceptedStaleSuccess = FALSE
NewLeaderInitialProgress == (currentTerm = 3 /\ role = "Leader" /\ ~acceptedStaleSuccess) => (leaderNext = 4 /\ leaderMatch = 0 /\ lastRequestId = 0)
Spec == Init /\ [][Next]_vars
====