---- MODULE WitnessV2RaftElectionMultiResponderAccounting ----
EXTENDS Naturals, FiniteSets, TLC
CONSTANTS Nodes, CandidateNode, ResponderA, ResponderB, MaxTerm
ASSUME Cardinality(Nodes) = 5
ASSUME CandidateNode \in Nodes
ASSUME ResponderA \in Nodes
ASSUME ResponderB \in Nodes
ASSUME CandidateNode # ResponderA
ASSUME CandidateNode # ResponderB
ASSUME ResponderA # ResponderB
ASSUME MaxTerm = 1

Follower == "FOLLOWER"
Candidate == "CANDIDATE"
Leader == "LEADER"
NoVote == "NO_VOTE"
Quorum == (Cardinality(Nodes) \div 2) + 1

VARIABLES currentTerm, role, votedFor, receivedResponses, acceptedVotes,
          voteLedger, messages, handledRequests, handledResponses
vars == <<currentTerm, role, votedFor, receivedResponses, acceptedVotes,
          voteLedger, messages, handledRequests, handledResponses>>

ValidLedgerVotes(candidate) ==
  {v \in Nodes :
    [voter |-> v, term |-> currentTerm[candidate], candidate |-> candidate]
      \in voteLedger}

Init ==
  /\ currentTerm =
       [n \in Nodes |->
         IF n = CandidateNode \/ n = ResponderA \/ n = ResponderB
         THEN 1 ELSE 0]
  /\ role = [n \in Nodes |-> IF n = CandidateNode THEN Candidate ELSE Follower]
  /\ votedFor = [n \in Nodes |-> IF n = CandidateNode THEN CandidateNode ELSE NoVote]
  /\ receivedResponses = [n \in Nodes |-> {}]
  /\ acceptedVotes = [n \in Nodes |-> {}]
  /\ voteLedger =
       {[voter |-> CandidateNode, term |-> 1, candidate |-> CandidateNode]}
  /\ messages =
       {[kind |-> "REQUEST_VOTE", src |-> CandidateNode, dst |-> v, term |-> 1]
          : v \in {ResponderA, ResponderB}}
  /\ handledRequests = {}
  /\ handledResponses = {}

DeliverRequestVote(m) ==
  /\ m \in messages
  /\ m.kind = "REQUEST_VOTE"
  /\ m.dst \in {ResponderA, ResponderB}
  /\ m.term = currentTerm[m.dst]
  /\ votedFor[m.dst] = NoVote
  /\ votedFor' = [votedFor EXCEPT ![m.dst] = m.src]
  /\ voteLedger' =
       voteLedger \cup {[voter |-> m.dst, term |-> m.term, candidate |-> m.src]}
  /\ messages' = (messages \ {m}) \cup
       {[kind |-> "VOTE_RESPONSE", src |-> m.dst, dst |-> m.src,
         term |-> m.term, granted |-> TRUE]}
  /\ handledRequests' = handledRequests \cup {m.dst}
  /\ UNCHANGED <<currentTerm, role, receivedResponses, acceptedVotes, handledResponses>>

HandleVoteResponse(m) ==
  /\ m \in messages
  /\ m.kind = "VOTE_RESPONSE"
  /\ m.dst = CandidateNode
  /\ m.term = currentTerm[CandidateNode]
  /\ m.granted = TRUE
  /\ role[CandidateNode] = Candidate
  /\ receivedResponses' =
       [receivedResponses EXCEPT ![m.dst] = @ \cup {m.src}]
  /\ acceptedVotes' =
       [acceptedVotes EXCEPT ![m.dst] = @ \cup {m.src}]
  /\ role' =
       IF Cardinality(acceptedVotes[CandidateNode] \cup {m.src}) + 1 >= Quorum
       THEN [role EXCEPT ![CandidateNode] = Leader] ELSE role
  /\ messages' = messages \ {m}
  /\ handledResponses' = handledResponses \cup {m.src}
  /\ UNCHANGED <<currentTerm, votedFor, voteLedger, handledRequests>>

Next ==
  \/ \E m \in messages : DeliverRequestVote(m)
  \/ \E m \in messages : HandleVoteResponse(m)

Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ currentTerm \in [Nodes -> 0..MaxTerm]
  /\ role \in [Nodes -> {Follower, Candidate, Leader}]
  /\ votedFor \in [Nodes -> (Nodes \cup {NoVote})]
  /\ receivedResponses \in [Nodes -> SUBSET Nodes]
  /\ acceptedVotes \in [Nodes -> SUBSET Nodes]
  /\ voteLedger \subseteq [voter : Nodes, term : 1..MaxTerm, candidate : Nodes]
  /\ messages \subseteq
       ([kind : {"REQUEST_VOTE"}, src : Nodes, dst : Nodes, term : 1..MaxTerm]
        \cup
        [kind : {"VOTE_RESPONSE"}, src : Nodes, dst : Nodes,
         term : 1..MaxTerm, granted : BOOLEAN])
  /\ handledRequests \subseteq {ResponderA, ResponderB}
  /\ handledResponses \subseteq {ResponderA, ResponderB}

AcceptedVotesMatchLedger ==
  \A c \in Nodes : acceptedVotes[c] \subseteq ValidLedgerVotes(c)

ReceivedAndAcceptedAgree == receivedResponses = acceptedVotes

ResponseWasGeneratedByGrant ==
  \A v \in handledResponses : v \in ValidLedgerVotes(CandidateNode)

DistinctRespondersOnly ==
  /\ acceptedVotes[CandidateNode] \subseteq {ResponderA, ResponderB}
  /\ Cardinality(acceptedVotes[CandidateNode]) <= 2
  /\ Cardinality(receivedResponses[CandidateNode]) <= 2

QuorumRequiresTwoDistinctResponders ==
  role[CandidateNode] = Leader =>
    /\ Cardinality(acceptedVotes[CandidateNode]) = 2
    /\ {ResponderA, ResponderB} \subseteq acceptedVotes[CandidateNode]
    /\ Cardinality(acceptedVotes[CandidateNode] \cup {CandidateNode}) >= Quorum

BothResponsesAccounted ==
  role[CandidateNode] = Leader =>
    /\ receivedResponses[CandidateNode] = {ResponderA, ResponderB}
    /\ handledRequests = {ResponderA, ResponderB}
    /\ handledResponses = {ResponderA, ResponderB}

====
