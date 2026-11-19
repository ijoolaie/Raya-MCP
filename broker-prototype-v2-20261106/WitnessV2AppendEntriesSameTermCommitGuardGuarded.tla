---- MODULE WitnessV2AppendEntriesSameTermCommitGuardGuarded ----
EXTENDS Naturals, FiniteSets, Sequences, TLC

Nodes == {"n1", "n2", "n3"}
Follower == "FOLLOWER"
Leader == "LEADER"
Quorum == 2

VARIABLES currentTerm, role, log, commitIndex, nextIndex, matchIndex,
          lastRequestId, messages, responseOrder
vars == <<currentTerm, role, log, commitIndex, nextIndex, matchIndex,
          lastRequestId, messages, responseOrder>>

MinSet(S) == CHOOSE x \in S : \A y \in S : x <= y
Max(a,b) == IF a >= b THEN a ELSE b
PrevMatches(localLog, prevIndex, prevTerm) ==
  IF prevIndex = 0 THEN prevTerm = 0
  ELSE IF prevIndex <= Len(localLog)
       THEN localLog[prevIndex] = prevTerm
       ELSE FALSE

AppendResult(localLog, prevIndex, entries) ==
  LET conflicts ==
        {k \in 1..Len(entries) :
          prevIndex + k <= Len(localLog) /\
          localLog[prevIndex + k] # entries[k]}
  IN IF conflicts # {}
     THEN LET firstConflict == MinSet(conflicts)
          IN SubSeq(localLog, 1, prevIndex + firstConflict - 1) \o
             SubSeq(entries, firstConflict, Len(entries))
     ELSE IF prevIndex + Len(entries) > Len(localLog)
          THEN localLog \o
               SubSeq(entries, Len(localLog) - prevIndex + 1, Len(entries))
          ELSE localLog

Init ==
  /\ currentTerm = [n \in Nodes |-> 0]
  /\ role = [n \in Nodes |-> Follower]
  /\ log = [n \in Nodes |-> <<>>]
  /\ commitIndex = [n \in Nodes |-> 0]
  /\ nextIndex = [n \in Nodes |-> [p \in Nodes |-> 1]]
  /\ matchIndex = [n \in Nodes |-> [p \in Nodes |-> 0]]
  /\ lastRequestId = [n \in Nodes |-> [p \in Nodes |-> 0]]
  /\ messages = {}
  /\ responseOrder = <<>>

AssumeLeader ==
  /\ currentTerm["n1"] = 0
  /\ currentTerm["n2"] = 0
  /\ log["n1"] = <<>>
  /\ log["n2"] = <<>>
  /\ role' = [role EXCEPT !["n1"] = Leader, !["n2"] = Follower]
  /\ currentTerm' = [currentTerm EXCEPT !["n1"] = 1, !["n2"] = 1]
  /\ nextIndex' = [nextIndex EXCEPT !["n1"] = [p \in Nodes |-> Len(log["n1"]) + 1]]
  /\ matchIndex' = [matchIndex EXCEPT !["n1"] = [p \in Nodes |-> 0]]
  /\ lastRequestId' = [lastRequestId EXCEPT !["n1"] = [p \in Nodes |-> 0]]
  /\ UNCHANGED <<log, commitIndex, messages, responseOrder>>

LeaderAppend ==
  /\ role["n1"] = Leader
  /\ Len(log["n1"]) = 0
  /\ log' = [log EXCEPT !["n1"] = Append(@, currentTerm["n1"])]
  /\ UNCHANGED <<currentTerm, role, commitIndex, nextIndex, matchIndex,
                 lastRequestId, messages, responseOrder>>

SendAppend(p) ==
  /\ p \in {"n2", "n3"}
  /\ role["n1"] = Leader
  /\ lastRequestId["n1"][p] < 3
  /\ nextIndex["n1"][p] \in 1..(Len(log["n1"]) + 1)
  /\ LET prev == nextIndex["n1"][p] - 1
         rid == lastRequestId["n1"][p] + 1
     IN
       /\ messages' = messages \cup
            {[kind |-> "APPEND", src |-> "n1", dst |-> p,
              term |-> currentTerm["n1"], requestId |-> rid,
              prevIndex |-> prev,
              prevTerm |-> IF prev = 0 THEN 0 ELSE log["n1"][prev],
              entries |-> SubSeq(log["n1"], prev + 1, Len(log["n1"])),
              leaderCommit |-> commitIndex["n1"]]}
       /\ lastRequestId' = [lastRequestId EXCEPT !["n1"][p] = rid]
  /\ UNCHANGED <<currentTerm, role, log, commitIndex, nextIndex, matchIndex,
                 responseOrder>>

DeliverAppend(m) ==
  /\ m \in messages
  /\ m.kind = "APPEND"
  /\ m.dst \in Nodes
  /\ m.src \in Nodes
  /\ m.term >= currentTerm[m.dst]
  /\ PrevMatches(log[m.dst], m.prevIndex, m.prevTerm)
  /\ m.prevIndex >= commitIndex[m.dst]
  /\ m.prevIndex + Len(m.entries) <= 1
  /\ LET n == m.dst
         nextLog == AppendResult(log[n], m.prevIndex, m.entries)
         acceptedIndex == m.prevIndex + Len(m.entries)
     IN
       /\ log' = [log EXCEPT ![n] = nextLog]
       /\ commitIndex' = [commitIndex EXCEPT
            ![n] = IF m.leaderCommit > @
                   THEN (IF m.leaderCommit < Len(nextLog)
                         THEN m.leaderCommit ELSE Len(nextLog))
                   ELSE @]
       /\ currentTerm' = [currentTerm EXCEPT ![n] = m.term]
       /\ role' = [role EXCEPT ![n] = Follower]
       /\ messages' = (messages \ {m}) \cup
            {[kind |-> "APPEND_RESP", src |-> n, dst |-> m.src,
              term |-> m.term, requestId |-> m.requestId,
              success |-> TRUE, matchIndex |-> acceptedIndex]}
  /\ UNCHANGED <<nextIndex, matchIndex, lastRequestId, responseOrder>>

RejectAppend(m) ==
  /\ m \in messages
  /\ m.kind = "APPEND"
  /\ m.dst \in Nodes
  /\ m.src \in Nodes
  /\ \/ m.term < currentTerm[m.dst]
     \/ ~PrevMatches(log[m.dst], m.prevIndex, m.prevTerm)
     \/ m.prevIndex < commitIndex[m.dst]
  /\ currentTerm' =
       IF m.term > currentTerm[m.dst]
       THEN [currentTerm EXCEPT ![m.dst] = m.term]
       ELSE currentTerm
  /\ role' =
       IF m.term >= currentTerm[m.dst]
       THEN [role EXCEPT ![m.dst] = Follower]
       ELSE role
  /\ messages' = (messages \ {m}) \cup
       {[kind |-> "APPEND_RESP", src |-> m.dst, dst |-> m.src,
         term |-> IF m.term > currentTerm[m.dst]
                  THEN m.term ELSE currentTerm[m.dst],
         requestId |-> m.requestId,
         success |-> FALSE, matchIndex |-> 0]}
  /\ UNCHANGED <<log, commitIndex, nextIndex, matchIndex, lastRequestId,
                 responseOrder>>

HandleAppendResponse(m) ==
  /\ m \in messages
  /\ m.kind = "APPEND_RESP"
  /\ role[m.dst] = Leader
  /\ m.term = currentTerm[m.dst]
  /\ IF m.success
        THEN
          /\ matchIndex' = [matchIndex EXCEPT
               ![m.dst][m.src] =
                 Max(@, m.matchIndex)]
          /\ nextIndex' = [nextIndex EXCEPT
               ![m.dst][m.src] =
                 Max(@, m.matchIndex + 1)]
        ELSE
          /\ m.requestId = lastRequestId[m.dst][m.src]
          /\ nextIndex' = [nextIndex EXCEPT
               ![m.dst][m.src] =
                 Max(@ - 1, matchIndex[m.dst][m.src] + 1)]
          /\ UNCHANGED matchIndex
  /\ messages' = messages \ {m}
  /\ responseOrder' = Append(responseOrder, [requestId |-> m.requestId,
                                              success |-> m.success])
  /\ UNCHANGED <<currentTerm, role, log, commitIndex, lastRequestId>>

AdvanceCommit ==
  /\ role["n1"] = Leader
  /\ commitIndex["n1"] = 0
  /\ log["n1"] = <<1>>
  /\ Cardinality({p \in Nodes \ {"n1"} : matchIndex["n1"][p] >= 1}) + 1 >= Quorum
  /\ commitIndex' = [commitIndex EXCEPT !["n1"] = 1]
  /\ UNCHANGED <<currentTerm, role, log, nextIndex, matchIndex,
                 lastRequestId, messages, responseOrder>>

Next ==
  \/ AssumeLeader
  \/ LeaderAppend
  \/ \E p \in {"n2", "n3"} : SendAppend(p)
  \/ \E m \in messages : DeliverAppend(m)
  \/ \E m \in messages : RejectAppend(m)
  \/ \E m \in messages : HandleAppendResponse(m)
  \/ AdvanceCommit

GuardedTargetReached ==
  /\ role["n1"] = Leader
  /\ currentTerm["n1"] = currentTerm["n2"]
  /\ matchIndex["n1"]["n2"] = 1
  /\ nextIndex["n1"]["n2"] = 2
  /\ lastRequestId["n1"]["n2"] = 2
  /\ commitIndex["n2"] = 1
  /\ Len(responseOrder) = 3
  /\ responseOrder[2].requestId = 1
  /\ responseOrder[2].success = TRUE
  /\ responseOrder[3].requestId = 2
  /\ responseOrder[3].success = FALSE

GuardedTargetNotReached == ~GuardedTargetReached

NextIndexNotBelowMatchIndex ==
  \A n, p \in Nodes : nextIndex[n][p] >= matchIndex[n][p] + 1

MatchIndexWithinLeaderLog ==
  \A n, p \in Nodes : matchIndex[n][p] <= Len(log[n])

NextIndexWithinLeaderLog ==
  \A n, p \in Nodes : nextIndex[n][p] <= Len(log[n]) + 1

TypeOK ==
  /\ currentTerm \in [Nodes -> 0..1]
  /\ role \in [Nodes -> {Follower, Leader}]
  /\ log \in [Nodes -> Seq(0..1)]
  /\ \A n \in Nodes : Len(log[n]) <= 1
  /\ commitIndex \in [Nodes -> 0..1]
  /\ \A n \in Nodes : commitIndex[n] <= Len(log[n])
  /\ nextIndex \in [Nodes -> [Nodes -> 1..2]]
  /\ matchIndex \in [Nodes -> [Nodes -> 0..1]]
  /\ lastRequestId \in [Nodes -> [Nodes -> 0..3]]
  /\ messages \subseteq
       ([kind : {"APPEND"}, src : Nodes, dst : Nodes,
         term : 0..1, requestId : 1..3, prevIndex : 0..1,
         prevTerm : 0..1, entries : Seq(0..1), leaderCommit : 0..1]
        \cup
        [kind : {"APPEND_RESP"}, src : Nodes, dst : Nodes,
         term : 0..1, requestId : 1..3, success : BOOLEAN,
         matchIndex : 0..1])

Spec == Init /\ [][Next]_vars
====
