---- MODULE WitnessV2RaftElectionDelayedGrantedResponseAfterVoterAdvances ----
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
          messages, responderAdvanced, responseHandled
vars == <<currentTerm, role, votedFor, votesReceived, voteLedger,
          messages, responderAdvanced, responseHandled>>

ValidVotes(candidate) ==
  {v \in Nodes :
    currentTerm[v] = currentTerm[candidate] /\ votedFor[v] = candidate}

Init ==
  /\ currentTerm = [n \in Nodes |-> IF n = CandidateNode \/ n = ResponderNode THEN 1 ELSE 0]
  /\ role = [n \in Nodes |-> IF n = CandidateNode THEN Candidate ELSE Follower]
  /\ votedFor = [n \in Nodes |-> IF n = CandidateNode THEN CandidateNode
                                  ELSE IF n = ResponderNode THEN CandidateNode ELSE NoVote]
  /\ votesReceived = [n \in Nodes |-> IF n = CandidateNode THEN {CandidateNode} ELSE {}]
  /\ voteLedger = {[voter |-> ResponderNode, term |-> 1, candidate |-> CandidateNode],
                   [voter |-> CandidateNode, term |-> 1, candidate |-> CandidateNode]}
  /\ messages = {[kind |-> "VOTE_RESPONSE", src |-> ResponderNode,
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
  /\ role' = IF role[m.dst] = Candidate /\ Cardinality(ValidVotes(m.dst)) >= Quorum
              THEN [role EXCEPT ![m.dst] = Leader] ELSE role
  /\ messages' = messages \ {m}
  /\ responseHandled' = TRUE
  /\ UNCHANGED <<currentTerm, votedFor, voteLedger, responderAdvanced>>

Next ==
  \/ AdvanceResponderTerm
  \/ \E m \in messages : HandleDelayedGrantedResponse(m)

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

HistoricalGrantExists ==
  responseHandled =>
    [voter |-> ResponderNode, term |-> 1, candidate |-> CandidateNode] \in voteLedger

DelayedResponseRecorded ==
  responseHandled => ResponderNode \in votesReceived[CandidateNode]

CurrentTermValidityExcludesAdvancedResponder ==
  responseHandled =>
    /\ currentTerm[ResponderNode] = 2
    /\ ResponderNode \notin ValidVotes(CandidateNode)
    /\ Cardinality(ValidVotes(CandidateNode)) = 1
    /\ role[CandidateNode] = Candidate

ResponseConsumed == responseHandled => messages = {}

====
