---- MODULE WitnessV2RaftElectionSameTermGrantedResponse ----
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

VARIABLES online, currentTerm, role, votedFor, votesReceived, messages, responseHandled
vars == <<online, currentTerm, role, votedFor, votesReceived, messages, responseHandled>>

Init ==
  /\ online = Nodes
  /\ currentTerm = [n \in Nodes |-> IF n = CandidateNode THEN 1 ELSE IF n = ResponderNode THEN 1 ELSE 0]
  /\ role = [n \in Nodes |-> IF n = CandidateNode THEN Candidate ELSE Follower]
  /\ votedFor = [n \in Nodes |-> IF n = CandidateNode \/ n = ResponderNode THEN CandidateNode ELSE NoVote]
  /\ votesReceived = [n \in Nodes |-> IF n = CandidateNode THEN {CandidateNode} ELSE {}]
  /\ messages = {[kind |-> "VOTE_RESPONSE", src |-> ResponderNode, dst |-> CandidateNode, term |-> 1, granted |-> TRUE]}
  /\ responseHandled = FALSE

HandleGrantedSameTermResponse(m) ==
  /\ m \in messages
  /\ m.kind = "VOTE_RESPONSE"
  /\ m.dst \in online
  /\ m.term = currentTerm[m.dst]
  /\ m.granted = TRUE
  /\ currentTerm' = currentTerm
  /\ votesReceived' = [votesReceived EXCEPT ![m.dst] = @ \cup {m.src}]
  /\ role' = IF role[m.dst] = Candidate /\ Cardinality({v \in Nodes : currentTerm[v] = currentTerm[m.dst] /\ votedFor[v] = m.dst}) >= 2
              THEN [role EXCEPT ![m.dst] = Leader]
              ELSE role
  /\ votedFor' = votedFor
  /\ messages' = messages \ {m}
  /\ responseHandled' = TRUE
  /\ UNCHANGED <<online>>

Next == \E m \in messages : HandleGrantedSameTermResponse(m)
Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ online \subseteq Nodes
  /\ currentTerm \in [Nodes -> 0..MaxTerm]
  /\ role \in [Nodes -> {Follower, Candidate, Leader}]
  /\ votedFor \in [Nodes -> (Nodes \cup {NoVote})]
  /\ votesReceived \in [Nodes -> SUBSET Nodes]
  /\ messages \subseteq [kind : {"VOTE_RESPONSE"}, src : Nodes, dst : Nodes, term : 1..MaxTerm, granted : BOOLEAN]
  /\ responseHandled \in BOOLEAN

GrantedResponseElectsCandidate ==
  responseHandled =>
    /\ currentTerm[CandidateNode] = 1
    /\ role[CandidateNode] = Leader
    /\ votedFor[CandidateNode] = CandidateNode
    /\ votesReceived[CandidateNode] = {CandidateNode, ResponderNode}

ResponseConsumed == responseHandled => messages = {}

====
