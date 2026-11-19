---- MODULE WitnessV2RaftElectionStaleResponseDuringNewElection ----
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
Quorum == (Cardinality(Nodes) \div 2) + 1

VARIABLES currentTerm, role, votedFor, votesReceived, voteLedger,
          messages, responderAdvanced, firstResponseHandled,
          higherTermObserved, newElectionStarted, staleResponseDropped
vars == <<currentTerm, role, votedFor, votesReceived, voteLedger,
          messages, responderAdvanced, firstResponseHandled,
          higherTermObserved, newElectionStarted, staleResponseDropped>>

Init ==
  /\ currentTerm = [n \in Nodes |-> IF n = CandidateNode \/ n = ResponderNode THEN 1 ELSE 0]
  /\ role = [n \in Nodes |-> IF n = CandidateNode THEN Candidate ELSE Follower]
  /\ votedFor = [n \in Nodes |-> IF n = CandidateNode \/ n = ResponderNode
                                  THEN CandidateNode ELSE NoVote]
  /\ votesReceived = [n \in Nodes |-> IF n = CandidateNode THEN {CandidateNode} ELSE {}]
  /\ voteLedger =
       {[voter |-> CandidateNode, term |-> 1, candidate |-> CandidateNode],
        [voter |-> ResponderNode, term |-> 1, candidate |-> CandidateNode]}
  /\ messages =
       {[kind |-> "VOTE_RESPONSE", msgId |-> 1, src |-> ResponderNode, dst |-> CandidateNode,
         term |-> 1, granted |-> TRUE],
        [kind |-> "VOTE_RESPONSE", msgId |-> 2, src |-> ResponderNode, dst |-> CandidateNode,
         term |-> 1, granted |-> TRUE]}
  /\ responderAdvanced = FALSE
  /\ firstResponseHandled = FALSE
  /\ higherTermObserved = FALSE
  /\ newElectionStarted = FALSE
  /\ staleResponseDropped = FALSE

AdvanceResponderTerm ==
  /\ ~responderAdvanced
  /\ currentTerm[ResponderNode] = 1
  /\ currentTerm' = [currentTerm EXCEPT ![ResponderNode] = 2]
  /\ role' = [role EXCEPT ![ResponderNode] = Follower]
  /\ votedFor' = [votedFor EXCEPT ![ResponderNode] = NoVote]
  /\ responderAdvanced' = TRUE
  /\ UNCHANGED <<votesReceived, voteLedger, messages, firstResponseHandled,
                  higherTermObserved, newElectionStarted, staleResponseDropped>>

HandleFirstDelayedGrantedResponse(m) ==
  /\ m \in messages
  /\ m.msgId = 1
  /\ m.dst = CandidateNode
  /\ m.term = currentTerm[CandidateNode]
  /\ m.granted = TRUE
  /\ responderAdvanced
  /\ ~firstResponseHandled
  /\ role[CandidateNode] = Candidate
  /\ votesReceived' = [votesReceived EXCEPT ![m.dst] = @ \cup {m.src}]
  /\ role' = [role EXCEPT ![m.dst] =
       IF Cardinality(votesReceived[m.dst] \cup {m.src}) >= Quorum
       THEN Leader ELSE @]
  /\ messages' = messages \ {m}
  /\ firstResponseHandled' = TRUE
  /\ UNCHANGED <<currentTerm, votedFor, voteLedger, responderAdvanced,
                  higherTermObserved, newElectionStarted, staleResponseDropped>>

ObserveHigherTermFromResponder ==
  /\ firstResponseHandled
  /\ role[CandidateNode] = Leader
  /\ currentTerm[ResponderNode] > currentTerm[CandidateNode]
  /\ currentTerm' = [currentTerm EXCEPT ![CandidateNode] = currentTerm[ResponderNode]]
  /\ role' = [role EXCEPT ![CandidateNode] = Follower]
  /\ votedFor' = [votedFor EXCEPT ![CandidateNode] = NoVote]
  /\ votesReceived' = [votesReceived EXCEPT ![CandidateNode] = {}]
  /\ higherTermObserved' = TRUE
  /\ UNCHANGED <<voteLedger, messages, responderAdvanced, firstResponseHandled,
                  newElectionStarted, staleResponseDropped>>

StartNewElectionTermTwo ==
  /\ higherTermObserved
  /\ ~newElectionStarted
  /\ currentTerm[CandidateNode] = 2
  /\ role[CandidateNode] = Follower
  /\ currentTerm' = [currentTerm EXCEPT ![CandidateNode] = 2]
  /\ role' = [role EXCEPT ![CandidateNode] = Candidate]
  /\ votedFor' = [votedFor EXCEPT ![CandidateNode] = CandidateNode]
  /\ votesReceived' = [votesReceived EXCEPT ![CandidateNode] = {CandidateNode}]
  /\ newElectionStarted' = TRUE
  /\ UNCHANGED <<voteLedger, messages, responderAdvanced, firstResponseHandled,
                  higherTermObserved, staleResponseDropped>>

DropStalePriorTermResponse(m) ==
  /\ newElectionStarted
  /\ m \in messages
  /\ m.msgId = 2
  /\ m.dst = CandidateNode
  /\ m.term < currentTerm[CandidateNode]
  /\ m.granted = TRUE
  /\ messages' = messages \ {m}
  /\ staleResponseDropped' = TRUE
  /\ UNCHANGED <<currentTerm, role, votedFor, votesReceived, voteLedger,
                  responderAdvanced, firstResponseHandled, higherTermObserved,
                  newElectionStarted>>

Next ==
  \/ AdvanceResponderTerm
  \/ \E m \in messages : HandleFirstDelayedGrantedResponse(m)
  \/ ObserveHigherTermFromResponder
  \/ StartNewElectionTermTwo
  \/ \E m \in messages : DropStalePriorTermResponse(m)

Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ currentTerm \in [Nodes -> 0..2]
  /\ role \in [Nodes -> {Follower, Candidate, Leader}]
  /\ votedFor \in [Nodes -> (Nodes \cup {NoVote})]
  /\ votesReceived \in [Nodes -> SUBSET Nodes]
  /\ voteLedger \subseteq [voter : Nodes, term : 1..2, candidate : Nodes]
  /\ messages \subseteq [kind : {"VOTE_RESPONSE"}, msgId : 1..2, src : Nodes, dst : Nodes,
                            term : 1..1, granted : BOOLEAN]
  /\ responderAdvanced \in BOOLEAN
  /\ firstResponseHandled \in BOOLEAN
  /\ higherTermObserved \in BOOLEAN
  /\ newElectionStarted \in BOOLEAN
  /\ staleResponseDropped \in BOOLEAN

FirstTermGrantElects ==
  (firstResponseHandled /\ ~higherTermObserved) =>
    /\ currentTerm[CandidateNode] = 1
    /\ role[CandidateNode] = Leader
    /\ votesReceived[CandidateNode] = {CandidateNode, ResponderNode}
    /\ [voter |-> ResponderNode, term |-> 1, candidate |-> CandidateNode] \in voteLedger

HigherTermObservationStepsLeaderDown ==
  (higherTermObserved /\ ~newElectionStarted) =>
    /\ currentTerm[CandidateNode] = 2
    /\ role[CandidateNode] = Follower
    /\ votedFor[CandidateNode] = NoVote
    /\ votesReceived[CandidateNode] = {}

NewElectionHasOnlyItsOwnVote ==
  newElectionStarted =>
    /\ currentTerm[CandidateNode] = 2
    /\ role[CandidateNode] = Candidate
    /\ votedFor[CandidateNode] = CandidateNode
    /\ votesReceived[CandidateNode] = {CandidateNode}

PriorTermResponseCannotPolluteNewElection ==
  staleResponseDropped =>
    /\ newElectionStarted
    /\ currentTerm[CandidateNode] = 2
    /\ role[CandidateNode] = Candidate
    /\ votedFor[CandidateNode] = CandidateNode
    /\ votesReceived[CandidateNode] = {CandidateNode}
    /\ messages = {}

====
