---- MODULE WitnessV2ConcurrentElectionsDelayedVoteThreeNode ----
EXTENDS Naturals, FiniteSets, TLC
CONSTANTS UseTermGuard, UseRoleGuard

Nodes == {"n1", "n2", "n3"}
Follower == "FOLLOWER"
Candidate == "CANDIDATE"
Leader == "LEADER"
NoVote == "NO_VOTE"

VARIABLES phase, currentTerm, role, votedFor, votesReceived, voteLedger, messages,
          staleVoteHandled
vars == <<phase, currentTerm, role, votedFor, votesReceived, voteLedger, messages,
          staleVoteHandled>>

Init ==
  /\ phase = 0
  /\ currentTerm = [n \in Nodes |-> 1]
  /\ role = [n \in Nodes |-> Follower]
  /\ votedFor = [n \in Nodes |-> NoVote]
  /\ votesReceived = [n \in Nodes |-> {}]
  /\ voteLedger = {}
  /\ messages = {}
  /\ staleVoteHandled = FALSE

StartConcurrentElections ==
  /\ phase = 0
  /\ currentTerm' = [currentTerm EXCEPT !["n1"] = 2, !["n2"] = 2]
  /\ role' = [role EXCEPT !["n1"] = Candidate, !["n2"] = Candidate]
  /\ votedFor' = [votedFor EXCEPT !["n1"] = "n1", !["n2"] = "n2"]
  /\ votesReceived' = [votesReceived EXCEPT !["n1"] = {"n1"}, !["n2"] = {"n2"}]
  /\ messages' = {
       [kind |-> "REQUEST_VOTE", src |-> "n1", dst |-> "n3", term |-> 2],
       [kind |-> "REQUEST_VOTE", src |-> "n2", dst |-> "n3", term |-> 2]}
  /\ phase' = 1
  /\ UNCHANGED <<voteLedger, staleVoteHandled>>

GrantN1AtN3 ==
  /\ phase = 1
  /\ [kind |-> "REQUEST_VOTE", src |-> "n1", dst |-> "n3", term |-> 2] \in messages
  /\ votedFor["n3"] = NoVote
  /\ currentTerm["n3"] <= 2
  /\ currentTerm' = [currentTerm EXCEPT !["n3"] = 2]
  /\ role' = [role EXCEPT !["n3"] = Follower]
  /\ votedFor' = [votedFor EXCEPT !["n3"] = "n1"]
  /\ voteLedger' = voteLedger \cup {[voter |-> "n3", term |-> 2, candidate |-> "n1"]}
  /\ messages' = (messages \ {
       [kind |-> "REQUEST_VOTE", src |-> "n1", dst |-> "n3", term |-> 2]})
       \cup {[kind |-> "VOTE_RESPONSE", src |-> "n3", dst |-> "n1",
              term |-> 2, granted |-> TRUE]}
  /\ phase' = 2
  /\ UNCHANGED <<votesReceived, staleVoteHandled>>

StartN2HigherTermElection ==
  /\ phase = 2
  /\ currentTerm["n2"] = 2
  /\ role["n2"] = Candidate
  /\ currentTerm' = [currentTerm EXCEPT !["n2"] = 3]
  /\ role' = [role EXCEPT !["n2"] = Candidate]
  /\ votedFor' = [votedFor EXCEPT !["n2"] = "n2"]
  /\ votesReceived' = [votesReceived EXCEPT !["n2"] = {"n2"}]
  /\ messages' = (messages \ {
       [kind |-> "REQUEST_VOTE", src |-> "n2", dst |-> "n3", term |-> 2]})
       \cup {[kind |-> "REQUEST_VOTE", src |-> "n2", dst |-> "n3", term |-> 3]}
  /\ phase' = 3
  /\ UNCHANGED <<voteLedger, staleVoteHandled>>

GrantN2AtN3 ==
  /\ phase = 3
  /\ [kind |-> "REQUEST_VOTE", src |-> "n2", dst |-> "n3", term |-> 3] \in messages
  /\ currentTerm["n3"] <= 3
  /\ (currentTerm["n3"] < 3 \/ votedFor["n3"] = "n2")
  /\ currentTerm' = [currentTerm EXCEPT !["n3"] = 3]
  /\ role' = [role EXCEPT !["n3"] = Follower]
  /\ votedFor' = [votedFor EXCEPT !["n3"] = "n2"]
  /\ voteLedger' = voteLedger \cup {[voter |-> "n3", term |-> 3, candidate |-> "n2"]}
  /\ messages' = (messages \ {
       [kind |-> "REQUEST_VOTE", src |-> "n2", dst |-> "n3", term |-> 3]})
       \cup {[kind |-> "VOTE_RESPONSE", src |-> "n3", dst |-> "n2",
              term |-> 3, granted |-> TRUE]}
  /\ phase' = 4
  /\ UNCHANGED <<votesReceived, staleVoteHandled>>

HandleN2VoteResponse ==
  /\ phase = 4
  /\ [kind |-> "VOTE_RESPONSE", src |-> "n3", dst |-> "n2",
      term |-> 3, granted |-> TRUE] \in messages
  /\ currentTerm["n2"] = 3
  /\ role["n2"] = Candidate
  /\ messages' = messages \ {
       [kind |-> "VOTE_RESPONSE", src |-> "n3", dst |-> "n2",
        term |-> 3, granted |-> TRUE]}
  /\ votesReceived' = [votesReceived EXCEPT !["n2"] = @ \cup {"n3"}]
  /\ role' = [role EXCEPT !["n2"] = Leader]
  /\ phase' = 5
  /\ UNCHANGED <<currentTerm, votedFor, voteLedger, staleVoteHandled>>

SendHigherTermAppend ==
  /\ phase = 5
  /\ role["n2"] = Leader
  /\ messages' = messages \cup {
       [kind |-> "APPEND", src |-> "n2", dst |-> "n1", term |-> 3]}
  /\ phase' = 6
  /\ UNCHANGED <<currentTerm, role, votedFor, votesReceived, voteLedger, staleVoteHandled>>

N1ObservesHigherTerm ==
  /\ phase = 6
  /\ [kind |-> "APPEND", src |-> "n2", dst |-> "n1", term |-> 3] \in messages
  /\ currentTerm["n1"] < 3
  /\ currentTerm' = [currentTerm EXCEPT !["n1"] = 3]
  /\ role' = [role EXCEPT !["n1"] = Follower]
  /\ votedFor' = [votedFor EXCEPT !["n1"] = NoVote]
  /\ messages' = messages \ {
       [kind |-> "APPEND", src |-> "n2", dst |-> "n1", term |-> 3]}
  /\ phase' = 7
  /\ UNCHANGED <<votesReceived, voteLedger, staleVoteHandled>>

HandleDelayedOldVoteResponse ==
  /\ phase = 7
  /\ [kind |-> "VOTE_RESPONSE", src |-> "n3", dst |-> "n1",
      term |-> 2, granted |-> TRUE] \in messages
  /\ IF UseTermGuard THEN 2 = currentTerm["n1"] ELSE TRUE
  /\ IF UseRoleGuard THEN role["n1"] = Candidate ELSE TRUE
  /\ messages' = messages \ {
       [kind |-> "VOTE_RESPONSE", src |-> "n3", dst |-> "n1",
        term |-> 2, granted |-> TRUE]}
  /\ votesReceived' = [votesReceived EXCEPT !["n1"] = @ \cup {"n3"}]
  /\ staleVoteHandled' = TRUE
  /\ phase' = 8
  /\ UNCHANGED <<currentTerm, role, votedFor, voteLedger>>

DropDelayedOldVoteResponse ==
  /\ phase = 7
  /\ [kind |-> "VOTE_RESPONSE", src |-> "n3", dst |-> "n1",
      term |-> 2, granted |-> TRUE] \in messages
  /\ messages' = messages \ {
       [kind |-> "VOTE_RESPONSE", src |-> "n3", dst |-> "n1",
        term |-> 2, granted |-> TRUE]}
  /\ phase' = 8
  /\ UNCHANGED <<currentTerm, role, votedFor, votesReceived, voteLedger, staleVoteHandled>>

Next ==
  \/ StartConcurrentElections
  \/ GrantN1AtN3
  \/ StartN2HigherTermElection
  \/ GrantN2AtN3
  \/ HandleN2VoteResponse
  \/ SendHigherTermAppend
  \/ N1ObservesHigherTerm
  \/ HandleDelayedOldVoteResponse
  \/ DropDelayedOldVoteResponse

TypeOK ==
  /\ UseTermGuard \in BOOLEAN
  /\ UseRoleGuard \in BOOLEAN
  /\ phase \in 0..8
  /\ currentTerm \in [Nodes -> 1..3]
  /\ role \in [Nodes -> {Follower, Candidate, Leader}]
  /\ votedFor \in [Nodes -> Nodes \cup {NoVote}]
  /\ votesReceived \in [Nodes -> SUBSET Nodes]
  /\ voteLedger \subseteq [voter : Nodes, term : 1..3, candidate : Nodes]
  /\ messages \subseteq
       ([kind : {"REQUEST_VOTE"}, src : Nodes, dst : Nodes, term : 1..3]
        \cup [kind : {"VOTE_RESPONSE"}, src : Nodes, dst : Nodes,
              term : 1..3, granted : BOOLEAN]
        \cup [kind : {"APPEND"}, src : Nodes, dst : Nodes, term : 1..3])
  /\ staleVoteHandled \in BOOLEAN

OneVotePerTerm ==
  \A n \in Nodes : \A t \in 1..3 :
    Cardinality({v \in voteLedger : v.voter = n /\ v.term = t}) <= 1

N2LeaderHasQuorum ==
  role["n2"] = Leader => {"n2", "n3"} \subseteq votesReceived["n2"]

N1StepsDownOnHigherTerm ==
  phase >= 7 => currentTerm["n1"] = 3 /\ role["n1"] = Follower

StaleVoteNotCounted ==
  phase = 8 => "n3" \notin votesReceived["n1"]

Spec == Init /\ [][Next]_vars
====