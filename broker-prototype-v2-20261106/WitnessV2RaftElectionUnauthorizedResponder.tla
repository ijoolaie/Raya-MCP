---- MODULE WitnessV2RaftElectionUnauthorizedResponder ----
EXTENDS Naturals, FiniteSets, TLC

CONSTANTS Nodes, CandidateNode, AuthorizedResponder, UnauthorizedResponder
ASSUME Cardinality(Nodes) = 3
ASSUME CandidateNode \in Nodes
ASSUME AuthorizedResponder \in Nodes
ASSUME UnauthorizedResponder \in Nodes
ASSUME CandidateNode # AuthorizedResponder
ASSUME CandidateNode # UnauthorizedResponder
ASSUME AuthorizedResponder # UnauthorizedResponder

Follower == "FOLLOWER"
Candidate == "CANDIDATE"
Leader == "LEADER"
NoVote == "NO_VOTE"
Quorum == (Cardinality(Nodes) \div 2) + 1
AuthorizedResponders == {AuthorizedResponder}

VARIABLES currentTerm, role, votedFor, votesReceived, messages, responseHandled
vars == <<currentTerm, role, votedFor, votesReceived, messages, responseHandled>>

ValidVotes(candidate) ==
  {v \in Nodes : currentTerm[v] = currentTerm[candidate] /\ votedFor[v] = candidate}

Init ==
  /\ currentTerm = [n \in Nodes |-> 1]
  /\ role = [n \in Nodes |-> IF n = CandidateNode THEN Candidate ELSE Follower]
  /\ votedFor = [n \in Nodes |-> IF n = CandidateNode THEN CandidateNode ELSE NoVote]
  /\ votesReceived = [n \in Nodes |-> IF n = CandidateNode THEN {CandidateNode} ELSE {}]
  /\ messages = {[kind |-> "VOTE_RESPONSE", src |-> UnauthorizedResponder,
                   dst |-> CandidateNode, term |-> 1, granted |-> TRUE]}
  /\ responseHandled = FALSE

HandleVoteResponse(m) ==
  /\ m \in messages
  /\ m.kind = "VOTE_RESPONSE"
  /\ m.dst = CandidateNode
  /\ m.term >= currentTerm[m.dst]
  /\ IF m.term > currentTerm[m.dst]
       THEN
         /\ currentTerm' = [currentTerm EXCEPT ![m.dst] = m.term]
         /\ role' = [role EXCEPT ![m.dst] = Follower]
         /\ votedFor' = [votedFor EXCEPT ![m.dst] = NoVote]
         /\ votesReceived' = [votesReceived EXCEPT ![m.dst] = {}]
       ELSE
         /\ currentTerm' = currentTerm
         /\ IF role[m.dst] = Candidate /\ m.term = currentTerm[m.dst] /\ m.granted
              THEN
                /\ votesReceived' = [votesReceived EXCEPT ![m.dst] = @ \cup {m.src}]
                /\ role' = IF Cardinality(ValidVotes(m.dst)) >= Quorum
                            THEN [role EXCEPT ![m.dst] = Leader] ELSE role
              ELSE
                /\ votesReceived' = votesReceived
                /\ role' = role
         /\ votedFor' = votedFor
  /\ messages' = messages \ {m}
  /\ responseHandled' = TRUE

Next == \E m \in messages : HandleVoteResponse(m)
Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ currentTerm \in [Nodes -> 1..1]
  /\ role \in [Nodes -> {Follower, Candidate, Leader}]
  /\ votedFor \in [Nodes -> (Nodes \cup {NoVote})]
  /\ votesReceived \in [Nodes -> SUBSET Nodes]
  /\ messages \subseteq [kind : {"VOTE_RESPONSE"}, src : Nodes, dst : Nodes,
                            term : 1..1, granted : BOOLEAN]
  /\ responseHandled \in BOOLEAN

UnauthorizedResponseIsRecorded ==
  responseHandled => UnauthorizedResponder \in votesReceived[CandidateNode]

UnauthorizedResponseCannotCreateQuorum ==
  responseHandled =>
    /\ UnauthorizedResponder \notin ValidVotes(CandidateNode)
    /\ Cardinality(ValidVotes(CandidateNode)) = 1
    /\ role[CandidateNode] = Candidate

ResponseConsumed == responseHandled => messages = {}

====
