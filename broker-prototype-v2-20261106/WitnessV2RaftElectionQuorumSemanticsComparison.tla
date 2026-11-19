---- MODULE WitnessV2RaftElectionQuorumSemanticsComparison ----
EXTENDS Naturals, FiniteSets, TLC

CONSTANTS Nodes, CandidateNode, ResponderNode
ASSUME Cardinality(Nodes) = 3
ASSUME CandidateNode \in Nodes
ASSUME ResponderNode \in Nodes
ASSUME CandidateNode # ResponderNode

Follower == "FOLLOWER"
Candidate == "CANDIDATE"
NoVote == "NO_VOTE"
Quorum == (Cardinality(Nodes) \div 2) + 1

VARIABLES currentTerm, role, votedFor, votesReceived, voteLedger,
          messages, responderAdvanced, responseHandled
vars == <<currentTerm, role, votedFor, votesReceived, voteLedger,
          messages, responderAdvanced, responseHandled>>

CurrentStateVotes(c) ==
  {v \in Nodes :
    currentTerm[v] = currentTerm[c] /\ votedFor[v] = c}

HistoricalGrantedVotes(c) ==
  {v \in Nodes :
    [voter |-> v, term |-> currentTerm[c], candidate |-> c] \in voteLedger}

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
       {[kind |-> "VOTE_RESPONSE", src |-> ResponderNode,
         dst |-> CandidateNode, term |-> 1, granted |-> TRUE]}
  /\ responderAdvanced = FALSE
  /\ responseHandled = FALSE

AdvanceResponderTerm ==
  /\ ~responderAdvanced
  /\ currentTerm[ResponderNode] = 1
  /\ currentTerm' = [currentTerm EXCEPT ![ResponderNode] = 2]
  /\ role' = [role EXCEPT ![ResponderNode] = Follower]
  /\ votedFor' = [votedFor EXCEPT ![ResponderNode] = NoVote]
  /\ responderAdvanced' = TRUE
  /\ UNCHANGED <<votesReceived, voteLedger, messages, responseHandled>>

HandleDelayedGrantedResponse(m) ==
  /\ m \in messages
  /\ m.kind = "VOTE_RESPONSE"
  /\ m.dst = CandidateNode
  /\ m.term = currentTerm[CandidateNode]
  /\ m.granted = TRUE
  /\ responderAdvanced
  /\ votesReceived' = [votesReceived EXCEPT ![m.dst] = @ \cup {m.src}]
  /\ messages' = messages \ {m}
  /\ responseHandled' = TRUE
  /\ UNCHANGED <<currentTerm, role, votedFor, voteLedger, responderAdvanced>>

Next ==
  \/ AdvanceResponderTerm
  \/ \E m \in messages : HandleDelayedGrantedResponse(m)

Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ currentTerm \in [Nodes -> 0..2]
  /\ role \in [Nodes -> {Follower, Candidate}]
  /\ votedFor \in [Nodes -> (Nodes \cup {NoVote})]
  /\ votesReceived \in [Nodes -> SUBSET Nodes]
  /\ voteLedger \subseteq [voter : Nodes, term : 1..2, candidate : Nodes]
  /\ messages \subseteq [kind : {"VOTE_RESPONSE"}, src : Nodes, dst : Nodes,
                            term : 1..1, granted : BOOLEAN]
  /\ responderAdvanced \in BOOLEAN
  /\ responseHandled \in BOOLEAN

DelayedResponseIsActuallyGranted ==
  responseHandled =>
    [voter |-> ResponderNode, term |-> 1, candidate |-> CandidateNode] \in voteLedger

BothPredicatesDisagreeAfterDelivery ==
  responseHandled =>
    /\ ResponderNode \in votesReceived[CandidateNode]
    /\ Cardinality(CurrentStateVotes(CandidateNode)) = 1
    /\ Cardinality(HistoricalGrantedVotes(CandidateNode)) = 2
    /\ Cardinality(CurrentStateVotes(CandidateNode)) < Quorum
    /\ Cardinality(HistoricalGrantedVotes(CandidateNode)) >= Quorum

ResponderTermAdvanced ==
  responseHandled => currentTerm[ResponderNode] = 2

ResponseConsumed ==
  responseHandled => messages = {}

====
