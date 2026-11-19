---- MODULE WitnessV2AppendEntriesSameTermDemotionStaleSuccessRoleGuard ----
EXTENDS Naturals, FiniteSets, TLC
VARIABLES currentTerm, roleA, demoted, leaderNextA, leaderMatchA, lastRequestIdA, messages, acceptedOldSuccess
vars == <<currentTerm, roleA, demoted, leaderNextA, leaderMatchA, lastRequestIdA, messages, acceptedOldSuccess>>
OldSuccess == [kind |-> "APPEND_RESP", src |-> "F", dst |-> "A", term |-> 3, requestId |-> 4, success |-> TRUE, matchIndex |-> 8]
Init == /\ currentTerm = 3 /\ roleA = "Leader" /\ demoted = FALSE /\ leaderNextA = 9 /\ leaderMatchA = 0 /\ lastRequestIdA = 4 /\ messages = {OldSuccess} /\ acceptedOldSuccess = FALSE
DemoteSameTerm == /\ currentTerm = 3 /\ roleA = "Leader" /\ currentTerm' = currentTerm /\ roleA' = "Follower" /\ demoted' = TRUE /\ leaderNextA' = leaderNextA /\ leaderMatchA' = leaderMatchA /\ lastRequestIdA' = lastRequestIdA /\ messages' = messages /\ acceptedOldSuccess' = acceptedOldSuccess
HandleSuccess == /\ demoted = TRUE /\ OldSuccess \in messages /\ OldSuccess.kind = "APPEND_RESP" /\ OldSuccess.dst = "A" /\ roleA = "Leader" /\ OldSuccess.term = currentTerm /\ currentTerm' = currentTerm /\ roleA' = roleA /\ demoted' = demoted /\ leaderMatchA' = (IF OldSuccess.matchIndex > leaderMatchA THEN OldSuccess.matchIndex ELSE leaderMatchA) /\ leaderNextA' = (IF OldSuccess.matchIndex + 1 > leaderNextA THEN OldSuccess.matchIndex + 1 ELSE leaderNextA) /\ lastRequestIdA' = lastRequestIdA /\ messages' = messages \ {OldSuccess} /\ acceptedOldSuccess' = TRUE
DropOldSuccess == /\ OldSuccess \in messages /\ currentTerm' = currentTerm /\ roleA' = roleA /\ demoted' = demoted /\ leaderNextA' = leaderNextA /\ leaderMatchA' = leaderMatchA /\ lastRequestIdA' = lastRequestIdA /\ messages' = messages \ {OldSuccess} /\ acceptedOldSuccess' = acceptedOldSuccess
Next == DemoteSameTerm \/ HandleSuccess \/ DropOldSuccess
TypeOK == /\ currentTerm = 3 /\ roleA \in {"Leader", "Follower"} /\ demoted \in BOOLEAN /\ leaderNextA \in 1..9 /\ leaderMatchA \in 0..8 /\ lastRequestIdA = 4 /\ messages \subseteq {OldSuccess} /\ acceptedOldSuccess \in BOOLEAN
CursorLowerBound == leaderNextA >= leaderMatchA + 1
FollowerNeverProcessesLeaderResponse == acceptedOldSuccess = FALSE
DemotionPhaseConsistent == demoted => roleA = "Follower"
Spec == Init /\ [][Next]_vars
====