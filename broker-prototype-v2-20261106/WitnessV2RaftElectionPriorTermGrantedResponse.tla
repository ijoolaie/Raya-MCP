---- MODULE WitnessV2RaftElectionPriorTermGrantedResponse ----
EXTENDS Naturals, FiniteSets, TLC

CONSTANTS Nodes, CandidateNode, ResponderNode
ASSUME Cardinality(Nodes) = 3
ASSUME CandidateNode \in Nodes
ASSUME ResponderNode \in Nodes
ASSUME CandidateNode # ResponderNode

Follower == "FOLLOWER"
Candidate == "CANDIDATE"
Leader == "LEADER"
NoVote == "NO_VOTE"

VARIABLES online, currentTerm, role, votedFor, votesReceived, messages, termAdvanced, staleResponseDropped
vars == <<online, currentTerm, role, votedFor, votesReceived, messages, termAdvanced, staleResponseDropped>>

Init ==
  /\ online = Nodes
  /\ currentTerm = [n \in Nodes |-> IF n = CandidateNode THEN 1 ELSE 0]
  /\ role = [n \in Nodes |-> IF n = CandidateNode THEN Candidate ELSE Follower]
  /\ votedFor = [n \in Nodes |-> IF n = CandidateNode THEN CandidateNode ELSE NoVote]
  /\ votesReceived = [n \in Nodes |-> IF n = CandidateNode THEN {CandidateNode} ELSE {}]
  /\ messages = {[kind |-> "VOTE_RESPONSE", src |-> ResponderNode,
                  dst |-> CandidateNode, term |-> 1, granted |-> TRUE]}
  /\ termAdvanced = FALSE
  /\ staleResponseDropped = FALSE

AdvanceCandidateTerm ==
  /\ ~termAdvanced
  /\ currentTerm[CandidateNode] = 1
  /\ currentTerm' = [currentTerm EXCEPT ![CandidateNode] = 2]
  /\ role' = [role EXCEPT ![CandidateNode] = Candidate]
  /\ votedFor' = [votedFor EXCEPT ![CandidateNode] = CandidateNode]
  /\ votesReceived' = [votesReceived EXCEPT ![CandidateNode] = {CandidateNode}]
  /\ termAdvanced' = TRUE
  /\ UNCHANGED <<online, messages, staleResponseDropped>>

DropPriorTermGrantedResponse(m) ==
  /\ m \in messages
  /\ m.kind = "VOTE_RESPONSE"
  /\ m.granted = TRUE
  /\ m.dst = CandidateNode
  /\ m.term < currentTerm[m.dst]
  /\ messages' = messages \ {m}
  /\ staleResponseDropped' = TRUE
  /\ UNCHANGED <<online, currentTerm, role, votedFor, votesReceived, termAdvanced>>

Next ==
  \/ AdvanceCandidateTerm
  \/ \E m \in messages : DropPriorTermGrantedResponse(m)

Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ online \subseteq Nodes
  /\ currentTerm \in [Nodes -> 0..2]
  /\ role \in [Nodes -> {Follower, Candidate, Leader}]
  /\ votedFor \in [Nodes -> (Nodes \cup {NoVote})]
  /\ votesReceived \in [Nodes -> SUBSET Nodes]
  /\ messages \subseteq [kind : {"VOTE_RESPONSE"}, src : Nodes, dst : Nodes,
                            term : 1..1, granted : BOOLEAN]
  /\ termAdvanced \in BOOLEAN
  /\ staleResponseDropped \in BOOLEAN

PriorTermResponseNeverChangesNewElection ==
  staleResponseDropped =>
    /\ termAdvanced
    /\ currentTerm[CandidateNode] = 2
    /\ role[CandidateNode] = Candidate
    /\ votedFor[CandidateNode] = CandidateNode
    /\ votesReceived[CandidateNode] = {CandidateNode}

StaleResponseConsumed ==
  staleResponseDropped => messages = {}

NewTermStateEstablished ==
  termAdvanced =>
    /\ currentTerm[CandidateNode] = 2
    /\ role[CandidateNode] = Candidate
    /\ votedFor[CandidateNode] = CandidateNode
    /\ votesReceived[CandidateNode] = {CandidateNode}

====
