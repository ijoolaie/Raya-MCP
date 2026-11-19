---- MODULE WitnessV2RaftElectionHigherTermResponse ----
EXTENDS Naturals, FiniteSets, TLC

CONSTANTS Nodes, CandidateNode, ResponderNode, MaxTerm, ResponseGranted
ASSUME Cardinality(Nodes) = 3
ASSUME CandidateNode \in Nodes
ASSUME ResponderNode \in Nodes
ASSUME CandidateNode # ResponderNode
ASSUME MaxTerm \in Nat

Follower == "FOLLOWER"
Candidate == "CANDIDATE"
Leader == "LEADER"
NoVote == "NO_VOTE"

VARIABLES online, currentTerm, role, votedFor, votesReceived, messages, responseHandled
vars == <<online, currentTerm, role, votedFor, votesReceived, messages, responseHandled>>

Init ==
  /\ online = Nodes
  /\ currentTerm = [n \in Nodes |-> IF n = ResponderNode THEN 2 ELSE IF n = CandidateNode THEN 1 ELSE 0]
  /\ role = [n \in Nodes |-> IF n = CandidateNode THEN Candidate ELSE Follower]
  /\ votedFor = [n \in Nodes |-> IF n = CandidateNode THEN CandidateNode ELSE NoVote]
  /\ votesReceived = [n \in Nodes |-> IF n = CandidateNode THEN {CandidateNode} ELSE {}]
  /\ messages = {[kind |-> "VOTE_RESPONSE", src |-> ResponderNode, dst |-> CandidateNode, term |-> 2, granted |-> ResponseGranted]}
  /\ responseHandled = FALSE

HandleVoteResponse(m) ==
  /\ m \in messages
  /\ m.kind = "VOTE_RESPONSE"
  /\ m.dst \in online
  /\ m.term > currentTerm[m.dst]
  /\ currentTerm' = [currentTerm EXCEPT ![m.dst] = m.term]
  /\ role' = [role EXCEPT ![m.dst] = Follower]
  /\ votedFor' = [votedFor EXCEPT ![m.dst] = NoVote]
  /\ votesReceived' = [votesReceived EXCEPT ![m.dst] = {}]
  /\ messages' = messages \ {m}
  /\ responseHandled' = TRUE
  /\ UNCHANGED <<online>>

Next == \E m \in messages : HandleVoteResponse(m)
Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ online \subseteq Nodes
  /\ currentTerm \in [Nodes -> 0..MaxTerm]
  /\ role \in [Nodes -> {Follower, Candidate, Leader}]
  /\ votedFor \in [Nodes -> (Nodes \cup {NoVote})]
  /\ votesReceived \in [Nodes -> SUBSET Nodes]
  /\ messages \subseteq [kind : {"VOTE_RESPONSE"}, src : Nodes, dst : Nodes, term : 1..MaxTerm, granted : BOOLEAN]
  /\ responseHandled \in BOOLEAN

HigherTermResponseConsequences ==
  responseHandled =>
    /\ currentTerm[CandidateNode] = 2
    /\ role[CandidateNode] = Follower
    /\ votedFor[CandidateNode] = NoVote
    /\ votesReceived[CandidateNode] = {}

ResponseConsumed == responseHandled => messages = {}

====