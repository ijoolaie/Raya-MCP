---- MODULE WitnessV2AppendEntriesElectionTermReuseThreeNode ----
EXTENDS Naturals, FiniteSets, TLC
CONSTANT UseTermGuard

Nodes == {"n1", "n2", "n3"}
Leader == "LEADER"
Follower == "FOLLOWER"
Candidate == "CANDIDATE"
NoVote == "NO_VOTE"

VARIABLES phase, currentTerm, role, votedFor, votes,
          messages, lastRequestId, nextIndex, matchIndex, staleHandled
vars == <<phase, currentTerm, role, votedFor, votes,
          messages, lastRequestId, nextIndex, matchIndex, staleHandled>>

Init ==
  /\ phase = 0
  /\ currentTerm = [n \in Nodes |-> 1]
  /\ role = [n \in Nodes |-> IF n = "n1" THEN Leader ELSE Follower]
  /\ votedFor = [n \in Nodes |-> IF n = "n1" THEN "n1" ELSE NoVote]
  /\ votes = {"n1"}
  /\ messages =
       {[kind |-> "APPEND_RESP", src |-> "n2", dst |-> "n1",
         term |-> 1, requestId |-> 1, success |-> FALSE, ackIndex |-> 0],
        [kind |-> "APPEND_RESP", src |-> "n3", dst |-> "n1",
         term |-> 2, requestId |-> 1, success |-> FALSE, ackIndex |-> 0]}
  /\ lastRequestId = [n \in Nodes |-> [p \in Nodes |-> 1]]
  /\ nextIndex = [n \in Nodes |-> [p \in Nodes |-> 2]]
  /\ matchIndex = [n \in Nodes |-> [p \in Nodes |-> 0]]
  /\ staleHandled = FALSE

ObserveHigherTerm ==
  /\ phase = 0
  /\ \E m \in messages :
       /\ m.kind = "APPEND_RESP"
       /\ m.src = "n3"
       /\ m.term > currentTerm["n1"]
       /\ currentTerm' = [currentTerm EXCEPT !["n1"] = m.term]
       /\ role' = [role EXCEPT !["n1"] = Follower]
       /\ votedFor' = [votedFor EXCEPT !["n1"] = NoVote]
       /\ messages' = messages \ {m}
  /\ phase' = 1
  /\ UNCHANGED <<votes, lastRequestId, nextIndex,
                 matchIndex, staleHandled>>

StartElection ==
  /\ phase = 1
  /\ role["n1"] = Follower
  /\ currentTerm["n1"] = 2
  /\ currentTerm' = [currentTerm EXCEPT !["n1"] = 3]
  /\ role' = [role EXCEPT !["n1"] = Candidate]
  /\ votedFor' = [votedFor EXCEPT !["n1"] = "n1"]
  /\ votes' = {"n1"}
  /\ phase' = 2
  /\ UNCHANGED <<messages, lastRequestId, nextIndex, matchIndex, staleHandled>>

SendVoteRequests ==
  /\ phase = 2
  /\ messages' = messages \cup
       {[kind |-> "REQUEST_VOTE", src |-> "n1", dst |-> "n2", term |-> 3],
        [kind |-> "REQUEST_VOTE", src |-> "n1", dst |-> "n3", term |-> 3]}
  /\ phase' = 3
  /\ UNCHANGED <<currentTerm, role, votedFor, votes, lastRequestId,
                 nextIndex, matchIndex, staleHandled>>

GrantVotes ==
  /\ phase = 3
  /\ \A p \in {"n2", "n3"} :
       \E m \in messages :
         m.kind = "REQUEST_VOTE" /\ m.src = "n1" /\ m.dst = p
         /\ m.term = 3 /\ currentTerm[p] <= 3 /\ votedFor[p] = NoVote
  /\ messages' =
       (messages \ {m \in messages : m.kind = "REQUEST_VOTE"}) \cup
       {[kind |-> "VOTE_RESPONSE", src |-> "n2", dst |-> "n1",
         term |-> 3, granted |-> TRUE],
        [kind |-> "VOTE_RESPONSE", src |-> "n3", dst |-> "n1",
         term |-> 3, granted |-> TRUE]}
  /\ currentTerm' = [currentTerm EXCEPT !["n2"] = 3, !["n3"] = 3]
  /\ votedFor' = [votedFor EXCEPT !["n2"] = "n1", !["n3"] = "n1"]
  /\ role' = [role EXCEPT !["n2"] = Follower, !["n3"] = Follower]
  /\ phase' = 4
  /\ UNCHANGED <<votes, lastRequestId, nextIndex, matchIndex, staleHandled>>

HandleVoteResponses ==
  /\ phase = 4
  /\ \A p \in {"n2", "n3"} :
       \E m \in messages :
         m.kind = "VOTE_RESPONSE" /\ m.src = p /\ m.dst = "n1"
         /\ m.term = currentTerm["n1"] /\ m.granted
  /\ messages' = messages \ {m \in messages : m.kind = "VOTE_RESPONSE"}
  /\ votes' = {"n1", "n2", "n3"}
  /\ phase' = 5
  /\ UNCHANGED <<currentTerm, role, votedFor, lastRequestId,
                 nextIndex, matchIndex, staleHandled>>

BecomeLeader ==
  /\ phase = 5
  /\ role["n1"] = Candidate
  /\ Cardinality(votes) >= 2
  /\ role' = [role EXCEPT !["n1"] = Leader]
  /\ lastRequestId' = [lastRequestId EXCEPT !["n1"] =
        [p \in Nodes |-> 0]]
  /\ nextIndex' = [nextIndex EXCEPT !["n1"] = [p \in Nodes |-> 2]]
  /\ matchIndex' = [matchIndex EXCEPT !["n1"] = [p \in Nodes |-> 0]]
  /\ phase' = 6
  /\ UNCHANGED <<currentTerm, votedFor, votes, messages, staleHandled>>

SendAppendRequest ==
  /\ phase = 6
  /\ role["n1"] = Leader
  /\ lastRequestId["n1"]["n2"] = 0
  /\ messages' = messages \cup
       {[kind |-> "APPEND", src |-> "n1", dst |-> "n2",
         term |-> 3, requestId |-> 1]}
  /\ lastRequestId' = [lastRequestId EXCEPT !["n1"]["n2"] = 1]
  /\ phase' = 7
  /\ UNCHANGED <<currentTerm, role, votedFor, votes, nextIndex,
                 matchIndex, staleHandled>>

HandleStaleResponse ==
  /\ phase = 7
  /\ \E m \in messages :
       /\ m.kind = "APPEND_RESP"
       /\ m.src = "n2" /\ m.dst = "n1"
       /\ IF UseTermGuard THEN m.term = currentTerm["n1"] ELSE TRUE
       /\ role["n1"] = Leader
       /\ IF m.success
            THEN
              /\ matchIndex' = [matchIndex EXCEPT !["n1"]["n2"] =
                    IF m.matchIndex > @ THEN m.matchIndex ELSE @]
              /\ nextIndex' = [nextIndex EXCEPT !["n1"]["n2"] =
                    IF m.matchIndex + 1 > @ THEN m.matchIndex + 1 ELSE @]
            ELSE
              /\ m.requestId = lastRequestId["n1"]["n2"]
              /\ nextIndex' = [nextIndex EXCEPT !["n1"]["n2"] =
                    IF @ > 1 THEN @ - 1 ELSE 1]
              /\ UNCHANGED matchIndex
       /\ messages' = messages \ {m}
  /\ staleHandled' = TRUE
  /\ phase' = 8
  /\ UNCHANGED <<currentTerm, role, votedFor, votes, lastRequestId>>

Next ==
  \/ ObserveHigherTerm
  \/ StartElection
  \/ SendVoteRequests
  \/ GrantVotes
  \/ HandleVoteResponses
  \/ BecomeLeader
  \/ SendAppendRequest
  \/ HandleStaleResponse

TypeOK ==
  /\ UseTermGuard \in BOOLEAN
  /\ phase \in 0..8
  /\ currentTerm \in [Nodes -> 1..3]
  /\ role \in [Nodes -> {Leader, Follower, Candidate}]
  /\ votedFor \in [Nodes -> Nodes \cup {NoVote}]
  /\ votes \subseteq Nodes
  /\ lastRequestId \in [Nodes -> [Nodes -> 0..1]]
  /\ nextIndex \in [Nodes -> [Nodes -> 1..2]]
  /\ matchIndex \in [Nodes -> [Nodes -> 0..1]]
  /\ staleHandled \in BOOLEAN

StaleResponseHasNoEffect ==
  currentTerm["n1"] = 3 /\ role["n1"] = Leader =>
    /\ nextIndex["n1"]["n2"] = 2
    /\ matchIndex["n1"]["n2"] = 0
    /\ staleHandled = FALSE

Spec == Init /\ [][Next]_vars
====
