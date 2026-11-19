---- MODULE WitnessV2AppendEntriesDuplicateDeliveryThreeNode ----
EXTENDS Naturals, FiniteSets, Sequences, TLC

Nodes == {"n1", "n2", "n3"}
Leader == "n1"
Follower == "n2"

VARIABLES log, commitIndex, messages, accepted, rejected, dropped
vars == <<log, commitIndex, messages, accepted, rejected, dropped>>

MinSet(S) == CHOOSE x \in S : \A y \in S : x <= y
PrevMatches(localLog, prevIndex, prevTerm) ==
  IF prevIndex = 0 THEN prevTerm = 0
  ELSE IF prevIndex <= Len(localLog)
       THEN localLog[prevIndex] = prevTerm
       ELSE FALSE

Conflicts(localLog, prevIndex, entries) ==
  {k \in 1..Len(entries) :
    prevIndex + k <= Len(localLog) /\
    localLog[prevIndex + k] # entries[k]}

AppendResult(localLog, prevIndex, entries) ==
  LET conflicts == Conflicts(localLog, prevIndex, entries)
  IN IF conflicts # {}
     THEN LET firstConflict == MinSet(conflicts)
          IN SubSeq(localLog, 1, prevIndex + firstConflict - 1) \o
             SubSeq(entries, firstConflict, Len(entries))
     ELSE IF prevIndex + Len(entries) > Len(localLog)
          THEN localLog \o
               SubSeq(entries, Len(localLog) - prevIndex + 1, Len(entries))
          ELSE localLog

Init ==
  /\ log = [n \in Nodes |-> IF n = Leader THEN <<1, 2>> ELSE <<>>]
  /\ commitIndex = [n \in Nodes |-> 0]
  /\ messages = {[kind |-> "APPEND", src |-> Leader, dst |-> Follower,
                    term |-> 1, requestId |-> 1, mid |-> 1,
                    prevIndex |-> 0, prevTerm |-> 0,
                    entries |-> <<1, 2>>, leaderCommit |-> 1]}
  /\ accepted = {}
  /\ rejected = {}
  /\ dropped = {}

DuplicateAppend ==
  /\ \E m \in messages :
       m.kind = "APPEND" /\ m.mid = 1
  /\ 2 \notin accepted \cup rejected \cup dropped
  /\ messages' = messages \cup
       {[kind |-> "APPEND", src |-> Leader, dst |-> Follower,
         term |-> 1, requestId |-> 1, mid |-> 2,
         prevIndex |-> 0, prevTerm |-> 0,
         entries |-> <<1, 2>>, leaderCommit |-> 1]}
  /\ UNCHANGED <<log, commitIndex, accepted, rejected, dropped>>

DeliverAppend(m) ==
  /\ m \in messages
  /\ m.kind = "APPEND"
  /\ m.dst = Follower
  /\ m.term >= 1
  /\ PrevMatches(log[Follower], m.prevIndex, m.prevTerm)
  /\ m.prevIndex >= commitIndex[Follower]
  /\ LET newLog == AppendResult(log[Follower], m.prevIndex, m.entries)
     IN /\ log' = [log EXCEPT ![Follower] = newLog]
        /\ commitIndex' = [commitIndex EXCEPT
             ![Follower] = IF m.leaderCommit > @
                            THEN (IF m.leaderCommit < Len(newLog)
                                  THEN m.leaderCommit ELSE Len(newLog))
                            ELSE @]
  /\ messages' = messages \ {m}
  /\ accepted' = accepted \cup {m.mid}
  /\ UNCHANGED <<rejected, dropped>>

RejectAppend(m) ==
  /\ m \in messages
  /\ m.kind = "APPEND"
  /\ m.dst = Follower
  /\ \/ ~PrevMatches(log[Follower], m.prevIndex, m.prevTerm)
     \/ m.prevIndex < commitIndex[Follower]
  /\ messages' = messages \ {m}
  /\ rejected' = rejected \cup {m.mid}
  /\ UNCHANGED <<log, commitIndex, accepted, dropped>>

DropAppend(m) ==
  /\ m \in messages
  /\ m.kind = "APPEND"
  /\ messages' = messages \ {m}
  /\ dropped' = dropped \cup {m.mid}
  /\ UNCHANGED <<log, commitIndex, accepted, rejected>>

Next ==
  \/ DuplicateAppend
  \/ \E m \in messages : DeliverAppend(m)
  \/ \E m \in messages : RejectAppend(m)
  \/ \E m \in messages : DropAppend(m)

TypeOK ==
  /\ log \in [Nodes -> Seq(0..2)]
  /\ \A n \in Nodes : Len(log[n]) <= 2
  /\ commitIndex \in [Nodes -> 0..2]
  /\ \A n \in Nodes : commitIndex[n] <= Len(log[n])
  /\ messages \subseteq
       [kind : {"APPEND"}, src : Nodes, dst : Nodes,
        term : 1..1, requestId : 1..1, mid : 1..2,
        prevIndex : 0..0, prevTerm : 0..0,
        entries : {<<1, 2>>}, leaderCommit : 1..1]
  /\ accepted \subseteq 1..2
  /\ rejected \subseteq 1..2
  /\ dropped \subseteq 1..2

DuplicateDeliveryIsIdempotent ==
  accepted # {} => log[Follower] = <<1, 2>>

CommittedPrefixPreserved ==
  commitIndex[Follower] >= 1 => log[Follower][1] = 1

DuplicateRejected ==
  2 \in rejected

DuplicateNotRejected == ~DuplicateRejected

NoAppendDropped == dropped = {}

DuplicateRejectedByCommittedPrefixGuard ==
  DuplicateRejected => commitIndex[Follower] >= 1

AccountingDisjoint ==
  accepted \cap rejected = {} /\
  accepted \cap dropped = {} /\
  rejected \cap dropped = {}

Spec == Init /\ [][Next]_vars
====