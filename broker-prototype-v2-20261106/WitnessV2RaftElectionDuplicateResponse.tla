---- MODULE WitnessV2RaftElectionDuplicateResponse ----
EXTENDS Naturals, FiniteSets, TLC
CONSTANTS Nodes, CandidateNode, ResponderNode
ASSUME Cardinality(Nodes) = 3
ASSUME CandidateNode \in Nodes
ASSUME ResponderNode \in Nodes
ASSUME CandidateNode # ResponderNode
Follower == "FOLLOWER"
Candidate == "CANDIDATE"
Leader == "LEADER"
Quorum == (Cardinality(Nodes) \div 2) + 1
VARIABLES currentTerm, role, receivedResponses, acceptedVotes, messages, handledIds
vars == <<currentTerm, role, receivedResponses, acceptedVotes, messages, handledIds>>
Init ==
  /\ currentTerm = [n \in Nodes |-> IF n = CandidateNode \/ n = ResponderNode THEN 1 ELSE 0]
  /\ role = [n \in Nodes |-> IF n = CandidateNode THEN Candidate ELSE Follower]
  /\ receivedResponses = [n \in Nodes |-> {}]
  /\ acceptedVotes = [n \in Nodes |-> {}]
  /\ messages =
       {[kind |-> "VOTE_RESPONSE", src |-> ResponderNode, dst |-> CandidateNode,
         term |-> 1, granted |-> TRUE, msgId |-> i] : i \in {1, 2}}
  /\ handledIds = {}
HandleVoteResponse(m) ==
  /\ m \in messages
  /\ m.kind = "VOTE_RESPONSE"
  /\ m.dst = CandidateNode
  /\ m.term = currentTerm[CandidateNode]
  /\ m.granted
  /\ receivedResponses' =
       [receivedResponses EXCEPT ![m.dst] = @ \cup {m.src}]
  /\ acceptedVotes' =
       [acceptedVotes EXCEPT ![m.dst] = @ \cup {m.src}]
  /\ role' =
       IF Cardinality(acceptedVotes[CandidateNode] \cup {m.src}) + 1 >= Quorum
       THEN [role EXCEPT ![CandidateNode] = Leader]
       ELSE role
  /\ messages' = messages \ {m}
  /\ handledIds' = handledIds \cup {m.msgId}
  /\ UNCHANGED currentTerm
Next == \E m \in messages : HandleVoteResponse(m)
Spec == Init /\ [][Next]_vars
TypeOK ==
  /\ currentTerm \in [Nodes -> 0..1]
  /\ role \in [Nodes -> {Follower, Candidate, Leader}]
  /\ receivedResponses \in [Nodes -> SUBSET Nodes]
  /\ acceptedVotes \in [Nodes -> SUBSET Nodes]
  /\ messages \subseteq
       [kind : {"VOTE_RESPONSE"}, src : Nodes, dst : Nodes,
        term : 1..1, granted : BOOLEAN, msgId : {1, 2}]
  /\ handledIds \subseteq {1, 2}
DuplicateIsIdempotent ==
  /\ receivedResponses[CandidateNode] \subseteq {ResponderNode}
  /\ acceptedVotes[CandidateNode] \subseteq {ResponderNode}
  /\ Cardinality(acceptedVotes[CandidateNode]) <= 1
  /\ Cardinality(receivedResponses[CandidateNode]) <= 1
SameSenderNeverCountsTwice ==
  Cardinality(acceptedVotes[CandidateNode]) = Cardinality(receivedResponses[CandidateNode])
NoFalseQuorumFromDuplicate ==
  Cardinality(acceptedVotes[CandidateNode]) + 1 <= Quorum
====
