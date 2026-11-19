---- MODULE WitnessV2RaftElectionResponseAccounting ----
EXTENDS Naturals, FiniteSets, TLC
CONSTANTS Nodes, CandidateNode, ResponderNode, MaxTerm
ASSUME Cardinality(Nodes) = 3
ASSUME CandidateNode \in Nodes
ASSUME ResponderNode \in Nodes
ASSUME CandidateNode # ResponderNode
ASSUME MaxTerm \in Nat
Follower == "FOLLOWER"
Candidate == "CANDIDATE"
Leader == "LEADER"
NoVote == "NO_VOTE"
Quorum == (Cardinality(Nodes) \div 2) + 1
VARIABLES currentTerm, role, votedFor, receivedResponses, acceptedVotes, voteLedger, messages, requestHandled, responseHandled
vars == <<currentTerm, role, votedFor, receivedResponses, acceptedVotes, voteLedger, messages, requestHandled, responseHandled>>
ValidLedgerVotes(candidate) ==
  {v \in Nodes : [voter |-> v, term |-> currentTerm[candidate], candidate |-> candidate] \in voteLedger}
Init ==
  /\ currentTerm = [n \in Nodes |-> IF n = CandidateNode \/ n = ResponderNode THEN 1 ELSE 0]
  /\ role = [n \in Nodes |-> IF n = CandidateNode THEN Candidate ELSE Follower]
  /\ votedFor = [n \in Nodes |-> IF n = CandidateNode THEN CandidateNode ELSE NoVote]
  /\ receivedResponses = [n \in Nodes |-> {}]
  /\ acceptedVotes = [n \in Nodes |-> {}]
  /\ voteLedger = {[voter |-> CandidateNode, term |-> 1, candidate |-> CandidateNode]}
  /\ messages = {[kind |-> "REQUEST_VOTE", src |-> CandidateNode, dst |-> ResponderNode, term |-> 1]}
  /\ requestHandled = FALSE
  /\ responseHandled = FALSE
DeliverRequestVote(m) ==
  /\ m \in messages
  /\ m.kind = "REQUEST_VOTE"
  /\ m.dst = ResponderNode
  /\ m.term = currentTerm[m.dst]
  /\ votedFor[m.dst] = NoVote
  /\ votedFor' = [votedFor EXCEPT ![m.dst] = m.src]
  /\ voteLedger' = voteLedger \cup {[voter |-> m.dst, term |-> m.term, candidate |-> m.src]}
  /\ messages' = (messages \ {m}) \cup
       {[kind |-> "VOTE_RESPONSE", src |-> m.dst, dst |-> m.src, term |-> m.term, granted |-> TRUE]}
  /\ requestHandled' = TRUE
  /\ UNCHANGED <<currentTerm, role, receivedResponses, acceptedVotes, responseHandled>>
HandleVoteResponse(m) ==
  /\ m \in messages
  /\ m.kind = "VOTE_RESPONSE"
  /\ m.dst = CandidateNode
  /\ m.term = currentTerm[m.dst]
  /\ m.granted = TRUE
  /\ receivedResponses' = [receivedResponses EXCEPT ![m.dst] = @ \cup {m.src}]
  /\ acceptedVotes' = [acceptedVotes EXCEPT ![m.dst] = @ \cup {m.src}]
  /\ role' = IF Cardinality(acceptedVotes[m.dst] \cup {m.src}) + 1 >= Quorum
              THEN [role EXCEPT ![m.dst] = Leader] ELSE role
  /\ messages' = messages \ {m}
  /\ responseHandled' = TRUE
  /\ UNCHANGED <<currentTerm, votedFor, voteLedger, requestHandled>>
Next == \/ \E m \in messages : DeliverRequestVote(m)
        \/ \E m \in messages : HandleVoteResponse(m)
Spec == Init /\ [][Next]_vars
TypeOK ==
  /\ currentTerm \in [Nodes -> 0..MaxTerm]
  /\ role \in [Nodes -> {Follower, Candidate, Leader}]
  /\ votedFor \in [Nodes -> (Nodes \cup {NoVote})]
  /\ receivedResponses \in [Nodes -> SUBSET Nodes]
  /\ acceptedVotes \in [Nodes -> SUBSET Nodes]
  /\ voteLedger \subseteq [voter : Nodes, term : 1..MaxTerm, candidate : Nodes]
  /\ messages \subseteq ([kind : {"REQUEST_VOTE"}, src : Nodes, dst : Nodes, term : 1..MaxTerm] \cup
       [kind : {"VOTE_RESPONSE"}, src : Nodes, dst : Nodes, term : 1..MaxTerm, granted : BOOLEAN])
  /\ requestHandled \in BOOLEAN
  /\ responseHandled \in BOOLEAN
AcceptedVotesMatchLedger ==
  \A c \in Nodes : acceptedVotes[c] \subseteq ValidLedgerVotes(c)
ReceivedAndAcceptedAgree == receivedResponses = acceptedVotes
ResponseWasGeneratedByGrant == responseHandled => ResponderNode \in ValidLedgerVotes(CandidateNode)
CandidateElectedByQuorum ==
  responseHandled =>
    /\ role[CandidateNode] = Leader
    /\ Cardinality(acceptedVotes[CandidateNode]) + 1 >= Quorum
====