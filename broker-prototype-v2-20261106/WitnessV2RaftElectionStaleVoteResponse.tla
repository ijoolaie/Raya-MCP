---- MODULE WitnessV2RaftElectionStaleVoteResponse ----
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

VARIABLES online, currentTerm, role, votedFor, votesReceived, messages, staleResponseDropped
vars == <<online, currentTerm, role, votedFor, votesReceived, messages, staleResponseDropped>>

Init ==
  /\ online = Nodes
  /\ currentTerm = [n \in Nodes |-> IF n = CandidateNode THEN 2 ELSE IF n = ResponderNode THEN 1 ELSE 0]
  /\ role = [n \in Nodes |-> IF n = CandidateNode THEN Candidate ELSE Follower]
  /\ votedFor = [n \in Nodes |-> IF n = CandidateNode THEN CandidateNode ELSE NoVote]
  /\ votesReceived = [n \in Nodes |-> IF n = CandidateNode THEN {CandidateNode} ELSE {}]
  /\ messages = {[kind |-> "VOTE_RESPONSE", src |-> ResponderNode, dst |-> CandidateNode, term |-> 1, granted |-> ResponseGranted]}
  /\ staleResponseDropped = FALSE

HandleVoteResponse(m) ==
  /\ m \in messages
  /\ m.kind = "VOTE_RESPONSE"
  /\ m.dst \in online
  /\ m.term >= currentTerm[m.dst]
  /\ IF m.term > currentTerm[m.dst]
       THEN
         /\ currentTerm' = [currentTerm EXCEPT ![m.dst] = m.term]
         /\ role' = [role EXCEPT ![m.dst] = Follower]
         /\ votedFor' = [votedFor EXCEPT ![m.dst] = NoVote]
         /\ votesReceived' = [votesReceived EXCEPT ![m.dst] = {}]
       ELSE
         /\ currentTerm' = currentTerm
         /\ role' = IF role[m.dst] = Candidate /\ m.granted
                      THEN [role EXCEPT ![m.dst] = IF Cardinality(votesReceived[m.dst] \cup {m.src}) >= 2 THEN Leader ELSE Candidate]
                      ELSE role
         /\ votedFor' = votedFor
         /\ votesReceived' = IF role[m.dst] = Candidate /\ m.granted
                              THEN [votesReceived EXCEPT ![m.dst] = @ \cup {m.src}]
                              ELSE votesReceived
  /\ messages' = messages \ {m}
  /\ UNCHANGED <<online, staleResponseDropped>>

DropStaleResponse(m) ==
  /\ m \in messages
  /\ m.kind = "VOTE_RESPONSE"
  /\ m.dst \in online
  /\ m.term < currentTerm[m.dst]
  /\ messages' = messages \ {m}
  /\ staleResponseDropped' = TRUE
  /\ UNCHANGED <<online, currentTerm, role, votedFor, votesReceived>>

Next ==
  \/ \E m \in messages : HandleVoteResponse(m)
  \/ \E m \in messages : DropStaleResponse(m)

Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ online \subseteq Nodes
  /\ currentTerm \in [Nodes -> 0..MaxTerm]
  /\ role \in [Nodes -> {Follower, Candidate, Leader}]
  /\ votedFor \in [Nodes -> (Nodes \cup {NoVote})]
  /\ votesReceived \in [Nodes -> SUBSET Nodes]
  /\ messages \subseteq [kind : {"VOTE_RESPONSE"}, src : Nodes, dst : Nodes, term : 1..MaxTerm, granted : BOOLEAN]
  /\ staleResponseDropped \in BOOLEAN

StaleResponseDropPreservesCandidate ==
  staleResponseDropped =>
    /\ currentTerm[CandidateNode] = 2
    /\ role[CandidateNode] = Candidate
    /\ votedFor[CandidateNode] = CandidateNode
    /\ votesReceived[CandidateNode] = {CandidateNode}

StaleResponseConsumed ==
  staleResponseDropped => messages = {}

====