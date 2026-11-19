---- MODULE WitnessV2RaftElectionBaseHandlerStaleResponse ----
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

VARIABLES online, currentTerm, role, votedFor, votesReceived, messages, termAdvanced, staleMessageDropped
vars == <<online, currentTerm, role, votedFor, votesReceived, messages, termAdvanced, staleMessageDropped>>

Init ==
  /\ online = Nodes
  /\ currentTerm = [n \in Nodes |-> IF n = CandidateNode THEN 1 ELSE 0]
  /\ role = [n \in Nodes |-> IF n = CandidateNode THEN Candidate ELSE Follower]
  /\ votedFor = [n \in Nodes |-> IF n = CandidateNode THEN CandidateNode ELSE NoVote]
  /\ votesReceived = [n \in Nodes |-> IF n = CandidateNode THEN {CandidateNode} ELSE {}]
  /\ messages = {[kind |-> "VOTE_RESPONSE", src |-> ResponderNode,
                  dst |-> CandidateNode, term |-> 1, granted |-> TRUE]}
  /\ termAdvanced = FALSE
  /\ staleMessageDropped = FALSE

AdvanceCandidateTerm ==
  /\ ~termAdvanced
  /\ currentTerm[CandidateNode] = 1
  /\ currentTerm' = [currentTerm EXCEPT ![CandidateNode] = 2]
  /\ role' = [role EXCEPT ![CandidateNode] = Candidate]
  /\ votedFor' = [votedFor EXCEPT ![CandidateNode] = CandidateNode]
  /\ votesReceived' = [votesReceived EXCEPT ![CandidateNode] = {CandidateNode}]
  /\ termAdvanced' = TRUE
  /\ UNCHANGED <<online, messages, staleMessageDropped>>

\* Same term guard and state-update structure as HandleVoteResponse in WitnessV2RaftElection.
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
         /\ IF role[m.dst] = Candidate /\ m.term = currentTerm[m.dst]
              /\ m.granted
              THEN
                /\ votesReceived' = [votesReceived EXCEPT ![m.dst] = @ \cup {m.src}]
                /\ role' = IF Cardinality(votesReceived[m.dst] \cup {m.src}) >= 2
                            THEN [role EXCEPT ![m.dst] = Leader]
                            ELSE role
              ELSE
                /\ votesReceived' = votesReceived
                /\ role' = role
         /\ votedFor' = votedFor
  /\ messages' = messages \ {m}
  /\ UNCHANGED <<online, termAdvanced, staleMessageDropped>>

DropMessage(m) ==
  /\ m \in messages
  /\ m.dst = CandidateNode
  /\ m.term < currentTerm[m.dst]
  /\ messages' = messages \ {m}
  /\ staleMessageDropped' = TRUE
  /\ UNCHANGED <<online, currentTerm, role, votedFor, votesReceived, termAdvanced>>

Next ==
  \/ AdvanceCandidateTerm
  \/ \E m \in messages : HandleVoteResponse(m)
  \/ \E m \in messages : DropMessage(m)

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
  /\ staleMessageDropped \in BOOLEAN

NewTermElectionStatePreserved ==
  termAdvanced =>
    /\ currentTerm[CandidateNode] = 2
    /\ role[CandidateNode] = Candidate
    /\ votedFor[CandidateNode] = CandidateNode
    /\ votesReceived[CandidateNode] = {CandidateNode}

StaleMessageCannotBeApplied ==
  /\ \A m \in messages :
       (m.dst = CandidateNode /\ m.term < currentTerm[CandidateNode])
         => ~ (m.term >= currentTerm[m.dst])

DroppedStaleMessagePreservesNewTerm ==
  staleMessageDropped =>
    /\ termAdvanced
    /\ currentTerm[CandidateNode] = 2
    /\ role[CandidateNode] = Candidate
    /\ votedFor[CandidateNode] = CandidateNode
    /\ votesReceived[CandidateNode] = {CandidateNode}
    /\ messages = {}

====
