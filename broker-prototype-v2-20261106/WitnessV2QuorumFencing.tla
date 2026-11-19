---- MODULE WitnessV2QuorumFencing ----
EXTENDS Naturals, FiniteSets, Sequences

CONSTANTS Replicas, MaxIndex, MaxTerm

ASSUME Replicas # {}
ASSUME MaxIndex \in Nat
ASSUME MaxTerm \in Nat
ASSUME Cardinality(Replicas) >= 3

Quorum == (Cardinality(Replicas) \div 2) + 1
NoLeader == "NO_LEADER"

VARIABLES online, leader, leaderTerm, currentTerm,
          replicaTerm, replicaIndex, committedIndex,
          committedSequence, acceptedIndex, acceptedTerm

vars == <<online, leader, leaderTerm, currentTerm,
          replicaTerm, replicaIndex, committedIndex,
          committedSequence, acceptedIndex, acceptedTerm>>

Init ==
  /\ online = Replicas
  /\ leader = NoLeader
  /\ leaderTerm = 0
  /\ currentTerm = 0
  /\ replicaTerm = [r \in Replicas |-> 0]
  /\ replicaIndex = [r \in Replicas |-> 0]
  /\ committedIndex = 0
  /\ committedSequence = 0
  /\ acceptedIndex = [r \in Replicas |-> 0]
  /\ acceptedTerm = [r \in Replicas |-> 0]

SetOnline(newOnline) ==
  /\ newOnline \subseteq Replicas
  /\ online' = newOnline
  /\ UNCHANGED <<leader, leaderTerm, currentTerm, replicaTerm,
                  replicaIndex, committedIndex, committedSequence,
                  acceptedIndex, acceptedTerm>>

Elect(candidate) ==
  /\ candidate \in online
  /\ Cardinality(online) >= Quorum
  /\ replicaIndex[candidate] >= committedIndex
  /\ currentTerm < MaxTerm
  /\ currentTerm' = currentTerm + 1
  /\ leader' = candidate
  /\ leaderTerm' = currentTerm + 1
  /\ replicaTerm' = [replicaTerm EXCEPT ![candidate] = currentTerm + 1]
  /\ UNCHANGED <<online, replicaIndex, committedIndex,
                  committedSequence, acceptedIndex, acceptedTerm>>

AppendAndCommit(suppliedLeader, suppliedTerm, ackSet) ==
  /\ suppliedLeader \in Replicas
  /\ suppliedLeader = leader
  /\ suppliedTerm = leaderTerm
  /\ suppliedTerm = currentTerm
  /\ suppliedLeader \in online
  /\ ackSet \subseteq online
  /\ suppliedLeader \in ackSet
  /\ Cardinality(ackSet) >= Quorum
  /\ committedIndex < MaxIndex
  /\ \A r \in ackSet : replicaIndex[r] >= committedIndex
  /\ committedIndex' = committedIndex + 1
  /\ committedSequence' = committedSequence + 1
  /\ replicaIndex' = [r \in ackSet |-> committedIndex + 1]
                  @@ [r \in Replicas \ ackSet |-> replicaIndex[r]]
  /\ replicaTerm' = [r \in ackSet |-> suppliedTerm]
                 @@ [r \in Replicas \ ackSet |-> replicaTerm[r]]
  /\ acceptedIndex' = [r \in ackSet |-> committedIndex + 1]
                    @@ [r \in Replicas \ ackSet |-> acceptedIndex[r]]
  /\ acceptedTerm' = [r \in ackSet |-> suppliedTerm]
                   @@ [r \in Replicas \ ackSet |-> acceptedTerm[r]]
  /\ UNCHANGED <<online, leader, leaderTerm, currentTerm>>

RejectStaleLeaderWrite(oldLeader, oldTerm) ==
  /\ oldLeader \in Replicas
  /\ oldTerm < currentTerm
  /\ UNCHANGED vars

Spec == Init /\ [][Next]_vars

Next ==
  \/ \E s \in SUBSET Replicas : SetOnline(s)
  \/ \E r \in Replicas : Elect(r)
  \/ \E r \in Replicas, t \in 0..MaxTerm, a \in SUBSET Replicas :
       AppendAndCommit(r, t, a)
  \/ \E r \in Replicas, t \in 0..MaxTerm :
       RejectStaleLeaderWrite(r, t)

TypeOK ==
  /\ online \subseteq Replicas
  /\ leader \in (Replicas \cup {NoLeader})
  /\ leaderTerm \in 0..MaxTerm
  /\ currentTerm \in 0..MaxTerm
  /\ replicaTerm \in [Replicas -> 0..MaxTerm]
  /\ replicaIndex \in [Replicas -> 0..MaxIndex]
  /\ committedIndex \in 0..MaxIndex
  /\ committedSequence = committedIndex
  /\ acceptedIndex \in [Replicas -> 0..MaxIndex]
  /\ acceptedTerm \in [Replicas -> 0..MaxTerm]

CommitMonotonic ==
  committedIndex <= MaxIndex

CommitNeverDecreases ==
  [](committedIndex' >= committedIndex)

LeaderTermCurrent ==
  leader # NoLeader => leaderTerm = currentTerm

LeaderOnline ==
  leader # NoLeader => leader \in online

CommittedPrefixOnQuorum ==
  Cardinality({r \in Replicas : replicaIndex[r] >= committedIndex}) >= Quorum

NoReplicaAheadOfItsAcceptedIndex ==
  \A r \in Replicas : replicaIndex[r] = acceptedIndex[r]

====
