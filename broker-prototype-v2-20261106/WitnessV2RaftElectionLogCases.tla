---- MODULE WitnessV2RaftElectionLogCases ----
EXTENDS Naturals, FiniteSets, Sequences, TLC

CONSTANTS Nodes, MaxTerm, StaleVoters
ASSUME Cardinality(Nodes) = 3
ASSUME MaxTerm \in Nat
ASSUME StaleVoters \subseteq Nodes

Follower == "FOLLOWER"
Candidate == "CANDIDATE"
Leader == "LEADER"
NoVote == "NO_VOTE"

Quorum == (Cardinality(Nodes) \div 2) + 1
NodeSymmetry == Permutations(Nodes)

VARIABLES online, currentTerm, role, log, votedFor, votesReceived, voteLedger, messages
vars == <<online, currentTerm, role, log, votedFor, votesReceived, voteLedger, messages>>
MessageBound == Cardinality(messages) <= 2

LastLogTerm(n) == IF Len(log[n]) = 0 THEN 0 ELSE log[n][Len(log[n])]
CandidateLogUpToDate(candidateLastTerm, candidateLastIndex, voter) ==
  \/ candidateLastTerm > LastLogTerm(voter)
  \/ (candidateLastTerm = LastLogTerm(voter) /\
      candidateLastIndex >= Len(log[voter]))

ValidVotes(candidate) ==
  {v \in Nodes :
    currentTerm[v] = currentTerm[candidate] /\ votedFor[v] = candidate}

Init ==
  /\ online = Nodes
  /\ currentTerm = [n \in Nodes |-> 0]
  /\ role = [n \in Nodes |-> Follower]
  /\ log = [n \in Nodes |-> IF n \in StaleVoters THEN <<1>> ELSE <<>>]
  /\ votedFor = [n \in Nodes |-> NoVote]
  /\ votesReceived = [n \in Nodes |-> {}]
  /\ voteLedger = {}
  /\ messages = {}

SetOnline(s) ==
  /\ s \subseteq Nodes
  /\ online' = s
  /\ UNCHANGED <<currentTerm, role, log, votedFor, votesReceived, voteLedger, messages>>

StartElection(n) ==
  /\ n \in online
  /\ role[n] # Leader
  /\ currentTerm[n] < MaxTerm
  /\ currentTerm' = [currentTerm EXCEPT ![n] = @ + 1]
  /\ role' = [role EXCEPT ![n] = Candidate]
  /\ votedFor' = [votedFor EXCEPT ![n] = n]
  /\ votesReceived' = [votesReceived EXCEPT ![n] = {n}]
  /\ voteLedger' = voteLedger \cup {[voter |-> n, term |-> currentTerm[n] + 1, candidate |-> n]}
  /\ messages' = messages \cup
       {[kind |-> "REQUEST_VOTE", src |-> n, dst |-> p,
         term |-> currentTerm[n] + 1,
         lastLogIndex |-> Len(log[n]),
         lastLogTerm |-> LastLogTerm(n)] : p \in Nodes \ {n}}
  /\ UNCHANGED <<online, log>>

DeliverRequestVote(m) ==
  /\ m \in messages
  /\ m.kind = "REQUEST_VOTE"
  /\ m.dst \in online
  /\ LET higher == m.term > currentTerm[m.dst]
         grant == m.term >= currentTerm[m.dst]
                  /\ (higher \/ votedFor[m.dst] = NoVote \/ votedFor[m.dst] = m.src)
                  /\ CandidateLogUpToDate(m.lastLogTerm, m.lastLogIndex, m.dst)
     IN
       /\ currentTerm' =
            IF higher
            THEN [currentTerm EXCEPT ![m.dst] = m.term]
            ELSE currentTerm
       /\ role' =
            IF higher \/ grant
            THEN [role EXCEPT ![m.dst] = Follower]
            ELSE role
       /\ votedFor' =
            IF higher
            THEN [votedFor EXCEPT ![m.dst] = IF grant THEN m.src ELSE NoVote]
            ELSE IF grant
                 THEN [votedFor EXCEPT ![m.dst] = m.src]
                 ELSE votedFor
       /\ votesReceived' =
            IF higher
            THEN [votesReceived EXCEPT ![m.dst] = {}]
            ELSE votesReceived
       /\ voteLedger' =
            IF grant
            THEN voteLedger \cup {[voter |-> m.dst, term |-> m.term, candidate |-> m.src]}
            ELSE voteLedger
       /\ messages' = (messages \ {m}) \cup
            {[kind |-> "VOTE_RESPONSE", src |-> m.dst, dst |-> m.src,
              term |-> m.term, granted |-> grant]}
  /\ UNCHANGED <<online, log>>

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
                /\ votesReceived' =
                     [votesReceived EXCEPT ![m.dst] = @ \cup {m.src}]
                /\ role' =
                     IF Cardinality(ValidVotes(m.dst)) >= Quorum
                     THEN [role EXCEPT ![m.dst] = Leader]
                     ELSE role
              ELSE
                /\ votesReceived' = votesReceived
                /\ role' = role
         /\ votedFor' = votedFor
  /\ messages' = messages \ {m}
  /\ UNCHANGED <<online, log, voteLedger>>

ObserveHigherTerm(m) ==
  /\ m \in messages
  /\ m.term > currentTerm[m.dst]
  /\ m.dst \in online
  /\ currentTerm' = [currentTerm EXCEPT ![m.dst] = m.term]
  /\ role' = [role EXCEPT ![m.dst] = Follower]
  /\ votedFor' = [votedFor EXCEPT ![m.dst] = NoVote]
  /\ votesReceived' = [votesReceived EXCEPT ![m.dst] = {}]
  /\ messages' = messages \ {m}
  /\ UNCHANGED <<online, log, voteLedger>>

DropMessage(m) ==
  /\ m \in messages
  /\ messages' = messages \ {m}
  /\ UNCHANGED <<online, currentTerm, role, log, votedFor, votesReceived, voteLedger>>

Next ==
  \/ \E s \in SUBSET Nodes : SetOnline(s)
  \/ \E n \in Nodes : StartElection(n)
  \/ \E m \in messages : DeliverRequestVote(m)
  \/ \E m \in messages : HandleVoteResponse(m)
  \/ \E m \in messages : DropMessage(m)

Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ online \subseteq Nodes
  /\ currentTerm \in [Nodes -> 0..MaxTerm]
  /\ role \in [Nodes -> {Follower, Candidate, Leader}]
  /\ log \in [Nodes -> Seq(0..MaxTerm)]
  /\ votedFor \in [Nodes -> (Nodes \cup {NoVote})]
  /\ votesReceived \in [Nodes -> SUBSET Nodes]
  /\ voteLedger \subseteq [voter : Nodes, term : 1..MaxTerm, candidate : Nodes]
  /\ messages \subseteq
       ([kind : {"REQUEST_VOTE"}, src : Nodes, dst : Nodes,
         term : 1..MaxTerm, lastLogIndex : Nat, lastLogTerm : 0..MaxTerm]
        \cup
        [kind : {"VOTE_RESPONSE"}, src : Nodes, dst : Nodes,
         term : 1..MaxTerm, granted : BOOLEAN])

AtMostOneLeaderPerTerm ==
  \A a, b \in Nodes :
    (role[a] = Leader /\ role[b] = Leader /\ currentTerm[a] = currentTerm[b])
      => a = b

VoteRecordWellFormed ==
  \A n \in Nodes : votedFor[n] = NoVote \/ votedFor[n] \in Nodes

OneVotePerTerm ==
  \A v \in Nodes, t \in 1..MaxTerm :
    Cardinality({x \in voteLedger : x.voter = v /\ x.term = t}) <= 1

NoStaleLogLeader ==
  \A c \in Nodes : role[c] = Leader => Len(log[c]) >= 1

====