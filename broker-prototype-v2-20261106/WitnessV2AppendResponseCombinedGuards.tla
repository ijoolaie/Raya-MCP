---- MODULE WitnessV2AppendResponseCombinedGuards ----
EXTENDS Naturals, FiniteSets, TLC
VARIABLES online, currentTerm, roleA, leaderNextA, leaderMatchA,
          lastRequestIdA, messages, acceptedFailure,
          acceptedWhileLeader, acceptedWhileSourceOnline,
          acceptedWhileDestinationOnline
vars == <<online, currentTerm, roleA, leaderNextA, leaderMatchA,
          lastRequestIdA, messages, acceptedFailure,
          acceptedWhileLeader, acceptedWhileSourceOnline,
          acceptedWhileDestinationOnline>>
OldFailure == [kind |-> "APPEND_RESP", src |-> "F", dst |-> "A",
               term |-> 3, requestId |-> 4, success |-> FALSE, matchIndex |-> 0]
Init ==
  /\ online = {"A", "F"}
  /\ currentTerm = 3
  /\ roleA = "Leader"
  /\ leaderNextA = 6
  /\ leaderMatchA = 0
  /\ lastRequestIdA = 4
  /\ messages = {OldFailure}
  /\ acceptedFailure = FALSE
  /\ acceptedWhileLeader = FALSE
  /\ acceptedWhileSourceOnline = FALSE
  /\ acceptedWhileDestinationOnline = FALSE
DemoteA ==
  /\ roleA = "Leader"
  /\ roleA' = "Follower"
  /\ UNCHANGED <<online, currentTerm, leaderNextA, leaderMatchA,
                  lastRequestIdA, messages, acceptedFailure,
                  acceptedWhileLeader, acceptedWhileSourceOnline,
                  acceptedWhileDestinationOnline>>
TakeSourceOffline ==
  /\ "F" \in online
  /\ online' = online \ {"F"}
  /\ UNCHANGED <<currentTerm, roleA, leaderNextA, leaderMatchA,
                  lastRequestIdA, messages, acceptedFailure,
                  acceptedWhileLeader, acceptedWhileSourceOnline,
                  acceptedWhileDestinationOnline>>
TakeDestinationOffline ==
  /\ "A" \in online
  /\ online' = online \ {"A"}
  /\ UNCHANGED <<currentTerm, roleA, leaderNextA, leaderMatchA,
                  lastRequestIdA, messages, acceptedFailure,
                  acceptedWhileLeader, acceptedWhileSourceOnline,
                  acceptedWhileDestinationOnline>>
HandleOldFailure ==
  /\ OldFailure \in messages
  /\ roleA = "Leader"
  /\ "F" \in online
  /\ "A" \in online
  /\ OldFailure.term = currentTerm
  /\ OldFailure.requestId = lastRequestIdA
  /\ leaderNextA' = (IF leaderNextA > 1 THEN leaderNextA - 1 ELSE 1)
  /\ currentTerm' = currentTerm
  /\ roleA' = roleA
  /\ leaderMatchA' = leaderMatchA
  /\ lastRequestIdA' = lastRequestIdA
  /\ online' = online
  /\ messages' = messages \ {OldFailure}
  /\ acceptedFailure' = TRUE
  /\ acceptedWhileLeader' = (roleA = "Leader")
  /\ acceptedWhileSourceOnline' = ("F" \in online)
  /\ acceptedWhileDestinationOnline' = ("A" \in online)
DropOldFailure ==
  /\ OldFailure \in messages
  /\ currentTerm' = currentTerm
  /\ roleA' = roleA
  /\ leaderNextA' = leaderNextA
  /\ leaderMatchA' = leaderMatchA
  /\ lastRequestIdA' = lastRequestIdA
  /\ online' = online
  /\ messages' = messages \ {OldFailure}
  /\ acceptedFailure' = acceptedFailure
  /\ acceptedWhileLeader' = acceptedWhileLeader
  /\ acceptedWhileSourceOnline' = acceptedWhileSourceOnline
  /\ acceptedWhileDestinationOnline' = acceptedWhileDestinationOnline
Next == DemoteA \/ TakeSourceOffline \/ TakeDestinationOffline \/
        HandleOldFailure \/ DropOldFailure
TypeOK ==
  /\ online \subseteq {"A", "F"}
  /\ currentTerm = 3
  /\ roleA \in {"Leader", "Follower"}
  /\ leaderNextA \in 1..6
  /\ leaderMatchA = 0
  /\ lastRequestIdA = 4
  /\ messages \subseteq {OldFailure}
  /\ acceptedFailure \in BOOLEAN
  /\ acceptedWhileLeader \in BOOLEAN
  /\ acceptedWhileSourceOnline \in BOOLEAN
  /\ acceptedWhileDestinationOnline \in BOOLEAN
CursorLowerBound == leaderNextA >= leaderMatchA + 1
AcceptanceSnapshotValid ==
  acceptedFailure =>
    acceptedWhileLeader /\ acceptedWhileSourceOnline /\
    acceptedWhileDestinationOnline
Spec == Init /\ [][Next]_vars
====
