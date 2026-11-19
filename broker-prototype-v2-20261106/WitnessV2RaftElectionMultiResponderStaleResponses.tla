---- MODULE WitnessV2RaftElectionMultiResponderStaleResponses ----
EXTENDS Naturals, FiniteSets, TLC

CONSTANTS Nodes, CandidateNode, ResponderA, ResponderB
ASSUME Cardinality(Nodes) = 3
ASSUME CandidateNode \in Nodes
ASSUME ResponderA \in Nodes
ASSUME ResponderB \in Nodes
ASSUME CandidateNode # ResponderA
ASSUME CandidateNode # ResponderB
ASSUME ResponderA # ResponderB

Follower == "FOLLOWER"
Candidate == "CANDIDATE"
Leader == "LEADER"
NoVote == "NO_VOTE"
Quorum == (Cardinality(Nodes) \div 2) + 1

VARIABLES currentTerm, role, votedFor, votesReceived, voteLedger,
          messages, responderAAdvanced, firstResponseHandled,
          higherTermObserved, newElectionStarted, staleResponsesDropped
vars == <<currentTerm, role, votedFor, votesReceived, voteLedger,
          messages, responderAAdvanced, firstResponseHandled,
          higherTermObserved, newElectionStarted, staleResponsesDropped>>

Init ==
  /\ currentTerm = [n \in Nodes |-> IF n = CandidateNode \/ n = ResponderA \/ n = ResponderB THEN 1 ELSE 0]
  /\ role = [n \in Nodes |-> IF n = CandidateNode THEN Candidate ELSE Follower]
  /\ votedFor = [n \in Nodes |-> IF n = CandidateNode \/ n = ResponderA \/ n = ResponderB
                                  THEN CandidateNode ELSE NoVote]
  /\ votesReceived = [n \in Nodes |-> IF n = CandidateNode THEN {CandidateNode} ELSE {}]
  /\ voteLedger =
       {[voter |-> CandidateNode, term |-> 1, candidate |-> CandidateNode],
        [voter |-> ResponderA, term |-> 1, candidate |-> CandidateNode],
        [voter |-> ResponderB, term |-> 1, candidate |-> CandidateNode]}
  /\ messages =
       {[kind |-> "VOTE_RESPONSE", msgId |-> 1, src |-> ResponderA, dst |-> CandidateNode,
         term |-> 1, granted |-> TRUE],
        [kind |-> "VOTE_RESPONSE", msgId |-> 2, src |-> ResponderA, dst |-> CandidateNode,
         term |-> 1, granted |-> TRUE],
        [kind |-> "VOTE_RESPONSE", msgId |-> 3, src |-> ResponderB, dst |-> CandidateNode,
         term |-> 1, granted |-> TRUE]}
  /\ responderAAdvanced = FALSE
  /\ firstResponseHandled = FALSE
  /\ higherTermObserved = FALSE
  /\ newElectionStarted = FALSE
  /\ staleResponsesDropped = FALSE

AdvanceResponderATerm ==
  /\ ~responderAAdvanced
  /\ currentTerm[ResponderA] = 1
  /\ currentTerm' = [currentTerm EXCEPT ![ResponderA] = 2]
  /\ role' = [role EXCEPT ![ResponderA] = Follower]
  /\ votedFor' = [votedFor EXCEPT ![ResponderA] = NoVote]
  /\ responderAAdvanced' = TRUE
  /\ UNCHANGED <<votesReceived, voteLedger, messages, firstResponseHandled,
                  higherTermObserved, newElectionStarted, staleResponsesDropped>>

HandleFirstGrantedResponse ==
  /\ ~firstResponseHandled
  /\ responderAAdvanced
  /\ role[CandidateNode] = Candidate
  /\ \E m \in messages :
       /\ m.msgId = 1
       /\ m.src = ResponderA
       /\ m.dst = CandidateNode
       /\ m.term = currentTerm[CandidateNode]
       /\ m.granted = TRUE
       /\ votesReceived' = [votesReceived EXCEPT ![CandidateNode] = @ \cup {m.src}]
       /\ role' = [role EXCEPT ![CandidateNode] =
            IF Cardinality(votesReceived[CandidateNode] \cup {m.src}) >= Quorum
            THEN Leader ELSE @]
       /\ messages' = messages \ {m}
  /\ firstResponseHandled' = TRUE
  /\ UNCHANGED <<currentTerm, votedFor, voteLedger, responderAAdvanced,
                  higherTermObserved, newElectionStarted, staleResponsesDropped>>

ObserveHigherTermFromResponderA ==
  /\ firstResponseHandled
  /\ role[CandidateNode] = Leader
  /\ currentTerm[ResponderA] > currentTerm[CandidateNode]
  /\ currentTerm' = [currentTerm EXCEPT ![CandidateNode] = currentTerm[ResponderA]]
  /\ role' = [role EXCEPT ![CandidateNode] = Follower]
  /\ votedFor' = [votedFor EXCEPT ![CandidateNode] = NoVote]
  /\ votesReceived' = [votesReceived EXCEPT ![CandidateNode] = {}]
  /\ higherTermObserved' = TRUE
  /\ UNCHANGED <<voteLedger, messages, responderAAdvanced, firstResponseHandled,
                  newElectionStarted, staleResponsesDropped>>

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
  /\ UNCHANGED <<voteLedger, messages, responderAAdvanced, firstResponseHandled,
                  higherTermObserved, staleResponsesDropped>>

DropAnyStalePriorTermResponse(m) ==
  /\ newElectionStarted
  /\ m \in messages
  /\ m.msgId \in {2, 3}
  /\ m.dst = CandidateNode
  /\ m.term < currentTerm[CandidateNode]
  /\ m.granted = TRUE
  /\ messages' = messages \ {m}
  /\ staleResponsesDropped' = IF Cardinality(messages \ {m}) = 0 THEN TRUE ELSE staleResponsesDropped
  /\ UNCHANGED <<currentTerm, role, votedFor, votesReceived, voteLedger,
                  responderAAdvanced, firstResponseHandled, higherTermObserved,
                  newElectionStarted>>

Next ==
  \/ AdvanceResponderATerm
  \/ HandleFirstGrantedResponse
  \/ ObserveHigherTermFromResponderA
  \/ StartNewElectionTermTwo
  \/ \E m \in messages : DropAnyStalePriorTermResponse(m)

Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ currentTerm \in [Nodes -> 0..2]
  /\ role \in [Nodes -> {Follower, Candidate, Leader}]
  /\ votedFor \in [Nodes -> (Nodes \cup {NoVote})]
  /\ votesReceived \in [Nodes -> SUBSET Nodes]
  /\ voteLedger \subseteq [voter : Nodes, term : 1..2, candidate : Nodes]
  /\ messages \subseteq [kind : {"VOTE_RESPONSE"}, msgId : 1..3, src : Nodes, dst : Nodes,
                            term : 1..1, granted : BOOLEAN]
  /\ responderAAdvanced \in BOOLEAN
  /\ firstResponseHandled \in BOOLEAN
  /\ higherTermObserved \in BOOLEAN
  /\ newElectionStarted \in BOOLEAN
  /\ staleResponsesDropped \in BOOLEAN

FirstTermQuorumUsesDistinctHistoricalGrant ==
  (firstResponseHandled /\ ~higherTermObserved) =>
    /\ currentTerm[CandidateNode] = 1
    /\ role[CandidateNode] = Leader
    /\ votesReceived[CandidateNode] = {CandidateNode, ResponderA}
    /\ [voter |-> ResponderA, term |-> 1, candidate |-> CandidateNode] \in voteLedger
    /\ [voter |-> ResponderB, term |-> 1, candidate |-> CandidateNode] \in voteLedger

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

BothOldResponsesCannotPolluteNewElection ==
  staleResponsesDropped =>
    /\ newElectionStarted
    /\ currentTerm[CandidateNode] = 2
    /\ role[CandidateNode] = Candidate
    /\ votedFor[CandidateNode] = CandidateNode
    /\ votesReceived[CandidateNode] = {CandidateNode}
    /\ messages = {}

====
