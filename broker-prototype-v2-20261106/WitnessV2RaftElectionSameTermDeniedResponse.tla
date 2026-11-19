---- MODULE WitnessV2RaftElectionSameTermDeniedResponse ----
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

VARIABLES online, currentTerm, role, votedFor, votesReceived, messages, denialHandled
vars == <<online, currentTerm, role, votedFor, votesReceived, messages, denialHandled>>

Init ==
  /\ online = Nodes
  /\ currentTerm = [n \in Nodes |-> IF n = CandidateNode THEN 1 ELSE 0]
  /\ role = [n \in Nodes |-> IF n = CandidateNode THEN Candidate ELSE Follower]
  /\ votedFor = [n \in Nodes |-> IF n = CandidateNode THEN CandidateNode ELSE NoVote]
  /\ votesReceived = [n \in Nodes |-> IF n = CandidateNode THEN {CandidateNode} ELSE {}]
  /\ messages = {[kind |-> "VOTE_RESPONSE", src |-> ResponderNode, dst |-> CandidateNode, term |-> 1, granted |-> FALSE]}
  /\ denialHandled = FALSE

HandleDeniedSameTermResponse(m) ==
  /\ m \in messages
  /\ m.kind = "VOTE_RESPONSE"
  /\ m.dst \in online
  /\ m.term = currentTerm[m.dst]
  /\ m.granted = FALSE
  /\ currentTerm' = currentTerm
  /\ role' = role
  /\ votedFor' = votedFor
  /\ votesReceived' = votesReceived
  /\ messages' = messages \ {m}
  /\ denialHandled' = TRUE
  /\ UNCHANGED <<online>>

Next == \E m \in messages : HandleDeniedSameTermResponse(m)
Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ online \subseteq Nodes
  /\ currentTerm \in [Nodes -> 0..MaxTerm]
  /\ role \in [Nodes -> {Follower, Candidate, Leader}]
  /\ votedFor \in [Nodes -> (Nodes \cup {NoVote})]
  /\ votesReceived \in [Nodes -> SUBSET Nodes]
  /\ messages \subseteq [kind : {"VOTE_RESPONSE"}, src : Nodes, dst : Nodes, term : 1..MaxTerm, granted : BOOLEAN]
  /\ denialHandled \in BOOLEAN

SameTermDenialPreservesCandidate ==
  denialHandled =>
    /\ currentTerm[CandidateNode] = 1
    /\ role[CandidateNode] = Candidate
    /\ votedFor[CandidateNode] = CandidateNode
    /\ votesReceived[CandidateNode] = {CandidateNode}

DenialConsumed == denialHandled => messages = {}

====