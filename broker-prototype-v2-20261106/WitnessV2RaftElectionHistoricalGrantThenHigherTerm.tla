---- MODULE WitnessV2RaftElectionHistoricalGrantThenHigherTerm ----
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
          messages, responderAdvanced, responseHandled, higherTermObserved
vars == <<currentTerm, role, votedFor, votesReceived, voteLedger,
          messages, responderAdvanced, responseHandled, higherTermObserved>>

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
       {[kind |-> "VOTE_RESPONSE", src |-> ResponderNode, dst |-> CandidateNode,
         term |-> 1, granted |-> TRUE]}
  /\ responderAdvanced = FALSE
  /\ responseHandled = FALSE
  /\ higherTermObserved = FALSE

AdvanceResponderTerm ==
  /\ ~responderAdvanced
  /\ currentTerm[ResponderNode] = 1
  /\ currentTerm' = [currentTerm EXCEPT ![ResponderNode] = 2]
  /\ role' = [role EXCEPT ![ResponderNode] = Follower]
  /\ votedFor' = [votedFor EXCEPT ![ResponderNode] = NoVote]
  /\ responderAdvanced' = TRUE
  /\ UNCHANGED <<votesReceived, voteLedger, messages, responseHandled, higherTermObserved>>

HandleDelayedGrantedResponse(m) ==
  /\ m \in messages
  /\ m.kind = "VOTE_RESPONSE"
  /\ m.dst = CandidateNode
  /\ m.term = currentTerm[CandidateNode]
  /\ m.granted = TRUE
  /\ responderAdvanced
  /\ role[CandidateNode] = Candidate
  /\ votesReceived' = [votesReceived EXCEPT ![m.dst] = @ \cup {m.src}]
  /\ role' = [role EXCEPT ![m.dst] =
       IF Cardinality(votesReceived[m.dst] \cup {m.src}) >= Quorum
       THEN Leader ELSE @]
  /\ messages' = messages \ {m}
  /\ responseHandled' = TRUE
  /\ UNCHANGED <<currentTerm, votedFor, voteLedger, responderAdvanced, higherTermObserved>>

ObserveHigherTermFromResponder ==
  /\ responseHandled
  /\ role[CandidateNode] = Leader
  /\ currentTerm[ResponderNode] > currentTerm[CandidateNode]
  /\ currentTerm' = [currentTerm EXCEPT ![CandidateNode] = currentTerm[ResponderNode]]
  /\ role' = [role EXCEPT ![CandidateNode] = Follower]
  /\ votedFor' = [votedFor EXCEPT ![CandidateNode] = NoVote]
  /\ votesReceived' = [votesReceived EXCEPT ![CandidateNode] = {}]
  /\ higherTermObserved' = TRUE
  /\ UNCHANGED <<voteLedger, messages, responderAdvanced, responseHandled>>

Next ==
  \/ AdvanceResponderTerm
  \/ \E m \in messages : HandleDelayedGrantedResponse(m)
  \/ ObserveHigherTermFromResponder

Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ currentTerm \in [Nodes -> 0..2]
  /\ role \in [Nodes -> {Follower, Candidate, Leader}]
  /\ votedFor \in [Nodes -> (Nodes \cup {NoVote})]
  /\ votesReceived \in [Nodes -> SUBSET Nodes]
  /\ voteLedger \subseteq [voter : Nodes, term : 1..2, candidate : Nodes]
  /\ messages \subseteq [kind : {"VOTE_RESPONSE"}, src : Nodes, dst : Nodes,
                            term : 1..1, granted : BOOLEAN]
  /\ responderAdvanced \in BOOLEAN
  /\ responseHandled \in BOOLEAN
  /\ higherTermObserved \in BOOLEAN

DelayedGrantPromotesByTwoDistinctVotes ==
  responseHandled =>
    /\ [voter |-> ResponderNode, term |-> 1, candidate |-> CandidateNode] \in voteLedger
    /\ IF higherTermObserved
       THEN /\ votesReceived[CandidateNode] = {}
            /\ currentTerm[CandidateNode] = 2
            /\ role[CandidateNode] = Follower
       ELSE /\ votesReceived[CandidateNode] = {CandidateNode, ResponderNode}
            /\ currentTerm[CandidateNode] = 1
            /\ role[CandidateNode] = Leader

HigherTermObservationStepsLeaderDown ==
  higherTermObserved =>
    /\ currentTerm[CandidateNode] = 2
    /\ role[CandidateNode] = Follower
    /\ votedFor[CandidateNode] = NoVote
    /\ votesReceived[CandidateNode] = {}

NoHigherTermObservationBeforeElection ==
  ~higherTermObserved => currentTerm[CandidateNode] = 1

====
