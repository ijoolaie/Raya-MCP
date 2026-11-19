---- MODULE WitnessV2AppendEntriesRetryTermChangeStaleResponse ----
EXTENDS Naturals, FiniteSets, TLC
CONSTANT UseTermGuard

Nodes == {"n1", "n2", "n3"}
Leader == "LEADER"
Follower == "FOLLOWER"
Candidate == "CANDIDATE"
MinSet(S) == CHOOSE x \in S : \A y \in S : x <= y

VARIABLES phase, currentTerm, role, votedFor, votes, messages,
          lastRequestId, nextIndex, matchIndex, staleProcessed
vars == <<phase, currentTerm, role, votedFor, votes, messages,
          lastRequestId, nextIndex, matchIndex, staleProcessed>>

OldFailure ==
  [kind |-> "APPEND_RESP", src |-> "n2", dst |-> "n1",
   term |-> 1, requestId |-> 1, success |-> FALSE, matchIndex |-> 0]
HigherTermNotice ==
  [kind |-> "APPEND_RESP", src |-> "n3", dst |-> "n1",
   term |-> 2, requestId |-> 1, success |-> FALSE, matchIndex |-> 0]
NewAppend ==
  [kind |-> "APPEND", src |-> "n1", dst |-> "n2",
   term |-> 3, requestId |-> 1, prevIndex |-> 1, prevTerm |-> 1,
   entries |-> <<2>>, leaderCommit |-> 1]
NewSuccess ==
  [kind |-> "APPEND_RESP", src |-> "n2", dst |-> "n1",
   term |-> 3, requestId |-> 1, success |-> TRUE, matchIndex |-> 2]

Init ==
  /\ phase = 0
  /\ currentTerm = [n \in Nodes |-> 1]
  /\ role = [n \in Nodes |-> IF n = "n1" THEN Leader ELSE Follower]
  /\ votedFor = [n \in Nodes |-> IF n = "n1" THEN "n1" ELSE "NONE"]
  /\ votes = {"n1"}
  /\ messages = {OldFailure, HigherTermNotice}
  /\ lastRequestId = [n \in Nodes |-> [p \in Nodes |-> 1]]
  /\ nextIndex = [n \in Nodes |-> [p \in Nodes |-> 2]]
  /\ matchIndex = [n \in Nodes |-> [p \in Nodes |-> 0]]
  /\ staleProcessed = FALSE

ObserveHigherTerm ==
  /\ phase = 0
  /\ HigherTermNotice \in messages
  /\ HigherTermNotice.term > currentTerm["n1"]
  /\ currentTerm' = [currentTerm EXCEPT !["n1"] = 2]
  /\ role' = [role EXCEPT !["n1"] = Follower]
  /\ votedFor' = [votedFor EXCEPT !["n1"] = "NONE"]
  /\ messages' = messages \ {HigherTermNotice}
  /\ phase' = 1
  /\ UNCHANGED <<votes, lastRequestId, nextIndex, matchIndex, staleProcessed>>

StartElection ==
  /\ phase = 1
  /\ role["n1"] = Follower
  /\ currentTerm["n1"] = 2
  /\ currentTerm' = [currentTerm EXCEPT !["n1"] = 3]
  /\ role' = [role EXCEPT !["n1"] = Candidate]
  /\ votedFor' = [votedFor EXCEPT !["n1"] = "n1"]
  /\ votes' = {"n1"}
  /\ phase' = 2
  /\ UNCHANGED <<messages, lastRequestId, nextIndex, matchIndex, staleProcessed>>

GrantQuorum ==
  /\ phase = 2
  /\ currentTerm["n1"] = 3
  /\ currentTerm' = [currentTerm EXCEPT !["n2"] = 3, !["n3"] = 3]
  /\ role' = [role EXCEPT !["n2"] = Follower, !["n3"] = Follower]
  /\ votedFor' = [votedFor EXCEPT !["n2"] = "n1", !["n3"] = "n1"]
  /\ votes' = {"n1", "n2", "n3"}
  /\ phase' = 3
  /\ UNCHANGED <<messages, lastRequestId, nextIndex, matchIndex, staleProcessed>>

BecomeLeader ==
  /\ phase = 3
  /\ Cardinality(votes) >= 2
  /\ role' = [role EXCEPT !["n1"] = Leader]
  /\ lastRequestId' = [lastRequestId EXCEPT !["n1"] = [p \in Nodes |-> 0]]
  /\ nextIndex' = [nextIndex EXCEPT !["n1"] = [p \in Nodes |-> 2]]
  /\ matchIndex' = [matchIndex EXCEPT !["n1"] = [p \in Nodes |-> 0]]
  /\ phase' = 4
  /\ UNCHANGED <<currentTerm, votedFor, votes, messages, staleProcessed>>

SendNewAppend ==
  /\ phase = 4
  /\ role["n1"] = Leader
  /\ currentTerm["n1"] = 3
  /\ lastRequestId["n1"]["n2"] = 0
  /\ messages' = messages \cup {NewAppend}
  /\ lastRequestId' = [lastRequestId EXCEPT !["n1"]["n2"] = 1]
  /\ phase' = 5
  /\ UNCHANGED <<currentTerm, role, votedFor, votes, nextIndex, matchIndex, staleProcessed>>

HandleOldFailure ==
  /\ phase = 5
  /\ OldFailure \in messages
  /\ role["n1"] = Leader
  /\ IF UseTermGuard THEN OldFailure.term = currentTerm["n1"] ELSE TRUE
  /\ OldFailure.requestId = lastRequestId["n1"]["n2"]
  /\ nextIndex' = [nextIndex EXCEPT !["n1"]["n2"] = IF @ > 1 THEN @ - 1 ELSE 1]
  /\ messages' = messages \ {OldFailure}
  /\ staleProcessed' = TRUE
  /\ phase' = 6
  /\ UNCHANGED <<currentTerm, role, votedFor, votes, lastRequestId, matchIndex>>

Next ==
  \/ ObserveHigherTerm
  \/ StartElection
  \/ GrantQuorum
  \/ BecomeLeader
  \/ SendNewAppend
  \/ HandleOldFailure

TypeOK ==
  /\ UseTermGuard \in BOOLEAN
  /\ phase \in 0..6
  /\ currentTerm \in [Nodes -> 1..3]
  /\ role \in [Nodes -> {Leader, Follower, Candidate}]
  /\ votedFor \in [Nodes -> Nodes \cup {"NONE"}]
  /\ votes \subseteq Nodes
  /\ lastRequestId \in [Nodes -> [Nodes -> 0..1]]
  /\ nextIndex \in [Nodes -> [Nodes -> 1..2]]
  /\ matchIndex \in [Nodes -> [Nodes -> 0..1]]
  /\ staleProcessed \in BOOLEAN

TermAndRoleSafe ==
  /\ (phase \in 1..3 => currentTerm["n1"] >= 2 /\ role["n1"] # Leader)
  /\ (phase >= 4 => currentTerm["n1"] = 3 /\ role["n1"] = Leader)

NewLeaderEpochInitialized ==
  phase >= 4 => currentTerm["n1"] = 3 /\ lastRequestId["n1"]["n2"] \in 0..1

StaleResponseRejected ==
  phase = 5 => nextIndex["n1"]["n2"] = 2 /\ staleProcessed = FALSE

NoOldTermMutation ==
  phase = 6 => nextIndex["n1"]["n2"] = 2 /\ staleProcessed = FALSE

Spec == Init /\ [][Next]_vars
====
