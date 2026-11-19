---- MODULE WitnessV2ElectionMessagesRefined ----
EXTENDS Naturals, FiniteSets, Sequences

CONSTANTS Nodes, MaxTerm, MaxLog
ASSUME Cardinality(Nodes) = 3
ASSUME MaxTerm \in Nat
ASSUME MaxLog \in Nat

Quorum == (Cardinality(Nodes) \div 2) + 1
Follower == "FOLLOWER"
Candidate == "CANDIDATE"
Leader == "LEADER"
NoVote == "NONE"

VARIABLES online, currentTerm, votedFor, role, logLength, logTerm,
          votesReceived, messages

vars == <<online, currentTerm, votedFor, role, logLength, logTerm,
          votesReceived, messages>>

UpToDate(candidateTerm, candidateIndex, voter) ==
  \/ candidateTerm > logTerm[voter]
  \/ (candidateTerm = logTerm[voter] /\ candidateIndex >= logLength[voter])

Init ==
  /\ online = Nodes
  /\ currentTerm = [n \in Nodes |-> 0]
  /\ votedFor = [n \in Nodes |-> NoVote]
  /\ role = [n \in Nodes |-> Follower]
  /\ logLength = [n \in Nodes |-> 0]
  /\ logTerm = [n \in Nodes |-> 0]
  /\ votesReceived = [n \in Nodes |-> {}]
  /\ messages = {}

SetOnline(s) ==
  /\ s \subseteq Nodes
  /\ online' = s
  /\ UNCHANGED <<currentTerm, votedFor, role, logLength, logTerm,
                 votesReceived, messages>>

StartElection(n) ==
  /\ n \in online
  /\ currentTerm[n] < MaxTerm
  /\ currentTerm' = [currentTerm EXCEPT ![n] = @ + 1]
  /\ votedFor' = [votedFor EXCEPT ![n] = n]
  /\ role' = [role EXCEPT ![n] = Candidate]
  /\ votesReceived' = [votesReceived EXCEPT ![n] = {n}]
  /\ messages' = messages \cup
       { [kind |-> "REQ", src |-> n, dst |-> p,
           term |-> currentTerm[n] + 1,
           lastIndex |-> logLength[n], lastTerm |-> logTerm[n]]
         : p \in Nodes \ {n} }
  /\ UNCHANGED <<online, logLength, logTerm>>

DeliverRequest(m) ==
  /\ m \in messages
  /\ m.kind = "REQ"
  /\ m.dst \in online
  /\ m.src \in online
  /\ LET n == m.dst IN
       /\ m.term >= currentTerm[n]
       /\ UpToDate(m.lastTerm, m.lastIndex, n)
       /\ (m.term > currentTerm[n] \/ votedFor[n] = NoVote
            \/ votedFor[n] = m.src)
       /\ currentTerm' = [currentTerm EXCEPT ![n] = m.term]
       /\ votedFor' = [votedFor EXCEPT ![n] = m.src]
       /\ role' = [role EXCEPT ![n] = Follower]
       /\ votesReceived' = [votesReceived EXCEPT ![n] = {}]
       /\ messages' = (messages \ {m}) \cup
          { [kind |-> "RESP", src |-> n, dst |-> m.src,
              term |-> m.term, granted |-> TRUE] }
  /\ UNCHANGED <<online, logLength, logTerm>>

DeliverDeniedRequest(m) ==
  /\ m \in messages
  /\ m.kind = "REQ"
  /\ m.dst \in online
  /\ m.src \in online
  /\ LET n == m.dst IN
       /\ m.term < currentTerm[n]
          \/ (m.term = currentTerm[n] /\
              (~UpToDate(m.lastTerm, m.lastIndex, n)
               \/ (votedFor[n] # NoVote /\ votedFor[n] # m.src)))
       /\ messages' = (messages \ {m}) \cup
          { [kind |-> "RESP", src |-> n, dst |-> m.src,
              term |-> currentTerm[n], granted |-> FALSE] }
  /\ UNCHANGED <<online, currentTerm, votedFor, role, logLength,
                 logTerm, votesReceived>>

ObserveHigherTermStaleRequest(m) ==
  /\ m \in messages
  /\ m.kind = "REQ"
  /\ m.dst \in online
  /\ m.src \in online
  /\ m.term > currentTerm[m.dst]
  /\ ~UpToDate(m.lastTerm, m.lastIndex, m.dst)
  /\ LET n == m.dst IN
       /\ currentTerm' = [currentTerm EXCEPT ![n] = m.term]
       /\ votedFor' = [votedFor EXCEPT ![n] = NoVote]
       /\ role' = [role EXCEPT ![n] = Follower]
       /\ votesReceived' = [votesReceived EXCEPT ![n] = {}]
       /\ messages' = (messages \ {m}) \cup
          { [kind |-> "RESP", src |-> n, dst |-> m.src,
              term |-> m.term, granted |-> FALSE] }
  /\ UNCHANGED <<online, logLength, logTerm>>

DeliverResponse(m) ==
  /\ m \in messages
  /\ m.kind = "RESP"
  /\ m.dst \in online
  /\ LET n == m.dst IN
       /\ m.term = currentTerm[n]
       /\ role[n] = Candidate
       /\ m.granted
       /\ votesReceived' = [votesReceived EXCEPT ![n] = @ \cup {m.src}]
       /\ messages' = messages \ {m}
  /\ UNCHANGED <<online, currentTerm, votedFor, role, logLength, logTerm>>

ObserveHigherTermResponse(m) ==
  /\ m \in messages
  /\ m.kind = "RESP"
  /\ m.dst \in online
  /\ m.term > currentTerm[m.dst]
  /\ currentTerm' = [currentTerm EXCEPT ![m.dst] = m.term]
  /\ votedFor' = [votedFor EXCEPT ![m.dst] = NoVote]
  /\ role' = [role EXCEPT ![m.dst] = Follower]
  /\ votesReceived' = [votesReceived EXCEPT ![m.dst] = {}]
  /\ messages' = messages \ {m}
  /\ UNCHANGED <<online, logLength, logTerm>>

BecomeLeader(n) ==
  /\ n \in online
  /\ role[n] = Candidate
  /\ Cardinality(votesReceived[n]) >= Quorum
  /\ role' = [role EXCEPT ![n] = Leader]
  /\ UNCHANGED <<online, currentTerm, votedFor, logLength, logTerm,
                 votesReceived, messages>>

DropMessage(m) ==
  /\ m \in messages
  /\ messages' = messages \ {m}
  /\ UNCHANGED <<online, currentTerm, votedFor, role, logLength,
                 logTerm, votesReceived>>

Next ==
  \/ \E s \in SUBSET Nodes : SetOnline(s)
  \/ \E n \in Nodes : StartElection(n)
  \/ \E m \in messages : DeliverRequest(m)
  \/ \E m \in messages : DeliverDeniedRequest(m)
  \/ \E m \in messages : ObserveHigherTermStaleRequest(m)
  \/ \E m \in messages : DeliverResponse(m)
  \/ \E m \in messages : ObserveHigherTermResponse(m)
  \/ \E n \in Nodes : BecomeLeader(n)
  \/ \E m \in messages : DropMessage(m)

Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ online \subseteq Nodes
  /\ currentTerm \in [Nodes -> 0..MaxTerm]
  /\ votedFor \in [Nodes -> (Nodes \cup {NoVote})]
  /\ role \in [Nodes -> {Follower, Candidate, Leader}]
  /\ logLength \in [Nodes -> 0..MaxLog]
  /\ logTerm \in [Nodes -> 0..MaxTerm]
  /\ votesReceived \in [Nodes -> SUBSET Nodes]

LeaderHasQuorum ==
  \A n \in Nodes : role[n] = Leader => Cardinality(votesReceived[n]) >= Quorum

LeaderUniquePerTerm ==
  \A a, b \in Nodes :
    (a # b /\ role[a] = Leader /\ role[b] = Leader)
      => currentTerm[a] # currentTerm[b]

VoteIsSelfOrNode ==
  \A n \in Nodes : votedFor[n] = NoVote \/ votedFor[n] \in Nodes

====
