---- MODULE WitnessV2RaftElectionVoteSetConsistency ----
EXTENDS Naturals, FiniteSets, TLC

CONSTANTS Nodes, CandidateNode, ResponderNode, MaxTerm
ASSUME Cardinality(Nodes) = 3
ASSUME CandidateNode \in Nodes
ASSUME ResponderNode \in Nodes
ASSUME CandidateNode # ResponderNode
ASSUME MaxTerm \in Nat

Follower == "FOLLOWER"
Candidate == "CANDIDATE"
Leader == "LEADER"
NoVote == "NO_VOTE"

VARIABLES currentTerm, role, votedFor, votesReceived, messages, handled
vars == <<currentTerm, role, votedFor, votesReceived, messages, handled>>

ValidVotes(candidate) ==
  {v \in Nodes : currentTerm[v] = currentTerm[candidate] /\ votedFor[v] = candidate}

Init ==
  /\ currentTerm = [n \in Nodes |-> IF n = CandidateNode \/ n = ResponderNode THEN 1 ELSE 0]
  /\ role = [n \in Nodes |-> IF n = CandidateNode THEN Candidate ELSE Follower]
  /\ votedFor = [n \in Nodes |-> IF n = CandidateNode THEN CandidateNode ELSE NoVote]
  /\ votesReceived = [n \in Nodes |-> IF n = CandidateNode THEN {CandidateNode} ELSE {}]
  /\ messages = {[kind |-> "VOTE_RESPONSE", src |-> ResponderNode, dst |-> CandidateNode, term |-> 1, granted |-> TRUE]}
  /\ handled = FALSE

HandleResponse(m) ==
  /\ m \in messages
  /\ m.kind = "VOTE_RESPONSE"
  /\ m.term = currentTerm[m.dst]
  /\ m.granted
  /\ votesReceived' = [votesReceived EXCEPT ![m.dst] = @ \cup {m.src}]
  /\ role' = IF role[m.dst] = Candidate /\ Cardinality(ValidVotes(m.dst)) >= 2
              THEN [role EXCEPT ![m.dst] = Leader] ELSE role
  /\ currentTerm' = currentTerm
  /\ votedFor' = votedFor
  /\ messages' = messages \ {m}
  /\ handled' = TRUE

Next == \E m \in messages : HandleResponse(m)
Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ currentTerm \in [Nodes -> 0..MaxTerm]
  /\ role \in [Nodes -> {Follower, Candidate, Leader}]
  /\ votedFor \in [Nodes -> (Nodes \cup {NoVote})]
  /\ votesReceived \in [Nodes -> SUBSET Nodes]
  /\ messages \subseteq [kind : {"VOTE_RESPONSE"}, src : Nodes, dst : Nodes, term : 1..MaxTerm, granted : BOOLEAN]
  /\ handled \in BOOLEAN

VotesReceivedAreValid ==
  \A c \in Nodes : votesReceived[c] \subseteq ValidVotes(c)

NoElectionFromUnvotedResponse ==
  handled => role[CandidateNode] = Candidate

====
