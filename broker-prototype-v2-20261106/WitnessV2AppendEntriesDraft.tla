---- MODULE WitnessV2AppendEntriesDraft ----
EXTENDS Naturals, FiniteSets, Sequences

CONSTANTS Nodes, MaxTerm, MaxLog
ASSUME Cardinality(Nodes) = 3
ASSUME MaxTerm \in Nat
ASSUME MaxLog \in Nat

Follower == "FOLLOWER"
Candidate == "CANDIDATE"
Leader == "LEADER"

VARIABLES online, currentTerm, role, log, commitIndex, messages
vars == <<online, currentTerm, role, log, commitIndex, messages>>

Quorum == (Cardinality(Nodes) \div 2) + 1
LogTerms == 0..MaxTerm
EmptyLog == <<>>

PrefixCompatible(oldLog, newLog, committed) ==
  /\ Len(newLog) >= committed
  /\ \A i \in 1..committed : oldLog[i] = newLog[i]

Init ==
  /\ online = Nodes
  /\ currentTerm = [n \in Nodes |-> 0]
  /\ role = [n \in Nodes |-> Follower]
  /\ log = [n \in Nodes |-> EmptyLog]
  /\ commitIndex = [n \in Nodes |-> 0]
  /\ messages = {}

SetOnline(s) ==
  /\ s \subseteq Nodes
  /\ online' = s
  /\ UNCHANGED <<currentTerm, role, log, commitIndex, messages>>

AssumeLeader(n) ==
  /\ n \in online
  /\ currentTerm[n] < MaxTerm
  /\ role' = [role EXCEPT ![n] = Leader]
  /\ currentTerm' = [currentTerm EXCEPT ![n] = @ + 1]
  /\ UNCHANGED <<online, log, commitIndex, messages>>

LeaderAppend(n) ==
  /\ n \in online
  /\ role[n] = Leader
  /\ Len(log[n]) < MaxLog
  /\ log' = [log EXCEPT ![n] = Append(@, currentTerm[n])]
  /\ UNCHANGED <<online, currentTerm, role, commitIndex, messages>>

SendAppend(n, p) ==
  /\ n \in online
  /\ p \in Nodes \ {n}
  /\ role[n] = Leader
  /\ messages' = messages \cup
       {[kind |-> "APPEND", src |-> n, dst |-> p,
         term |-> currentTerm[n], entries |-> log[n],
         leaderCommit |-> commitIndex[n]]}
  /\ UNCHANGED <<online, currentTerm, role, log, commitIndex>>

DeliverAppend(m) ==
  /\ m \in messages
  /\ m.kind = "APPEND"
  /\ m.dst \in online
  /\ m.src \in online
  /\ m.term >= currentTerm[m.dst]
  /\ PrefixCompatible(log[m.dst], m.entries, commitIndex[m.dst])
  /\ currentTerm' = [currentTerm EXCEPT ![m.dst] = m.term]
  /\ role' = [role EXCEPT ![m.dst] = Follower]
  /\ log' = [log EXCEPT ![m.dst] = m.entries]
  /\ commitIndex' = [commitIndex EXCEPT
       ![m.dst] = IF m.leaderCommit < Len(m.entries)
                    THEN m.leaderCommit ELSE Len(m.entries)]
  /\ messages' = messages \ {m}
  /\ UNCHANGED online

RejectAppend(m) ==
  /\ m \in messages
  /\ m.kind = "APPEND"
  /\ m.dst \in online
  /\ m.src \in online
  /\ \/ m.term < currentTerm[m.dst]
     \/ ~PrefixCompatible(log[m.dst], m.entries, commitIndex[m.dst])
  /\ messages' = messages \ {m}
  /\ UNCHANGED <<online, currentTerm, role, log, commitIndex>>

ObserveHigherTerm(m) ==
  /\ m \in messages
  /\ m.kind = "APPEND"
  /\ m.dst \in online
  /\ m.src \in online
  /\ m.term > currentTerm[m.dst]
  /\ ~PrefixCompatible(log[m.dst], m.entries, commitIndex[m.dst])
  /\ currentTerm' = [currentTerm EXCEPT ![m.dst] = m.term]
  /\ role' = [role EXCEPT ![m.dst] = Follower]
  /\ messages' = messages \ {m}
  /\ UNCHANGED <<online, log, commitIndex>>

AdvanceLeaderCommit(n) ==
  /\ n \in online
  /\ role[n] = Leader
  /\ commitIndex[n] < Len(log[n])
  /\ LET replicated ==
       {p \in Nodes : p \in online /\ log[p] = log[n] /\ commitIndex[p] >= commitIndex[n]}
     IN Cardinality(replicated) >= Quorum
  /\ commitIndex' = [commitIndex EXCEPT ![n] = Len(log[n])]
  /\ UNCHANGED <<online, currentTerm, role, log, messages>>

DropMessage(m) ==
  /\ m \in messages
  /\ messages' = messages \ {m}
  /\ UNCHANGED <<online, currentTerm, role, log, commitIndex>>

Next ==
  \/ \E s \in SUBSET Nodes : SetOnline(s)
  \/ \E n \in Nodes : AssumeLeader(n)
  \/ \E n \in Nodes : LeaderAppend(n)
  \/ \E n \in Nodes, p \in Nodes \ {n} : SendAppend(n, p)
  \/ \E m \in messages : DeliverAppend(m)
  \/ \E m \in messages : RejectAppend(m)
  \/ \E m \in messages : ObserveHigherTerm(m)
  \/ \E n \in Nodes : AdvanceLeaderCommit(n)
  \/ \E m \in messages : DropMessage(m)

Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ online \subseteq Nodes
  /\ currentTerm \in [Nodes -> 0..MaxTerm]
  /\ role \in [Nodes -> {Follower, Candidate, Leader}]
  /\ log \in [Nodes -> Seq(LogTerms)]
  /\ \A n \in Nodes : Len(log[n]) <= MaxLog
  /\ commitIndex \in [Nodes -> 0..MaxLog]
  /\ \A n \in Nodes : commitIndex[n] <= Len(log[n])
  /\ messages \subseteq
       [kind : {"APPEND"}, src : Nodes, dst : Nodes,
        term : 0..MaxTerm, entries : Seq(LogTerms),
        leaderCommit : 0..MaxLog]

CommitWithinLog == \A n \in Nodes : commitIndex[n] <= Len(log[n])

====
