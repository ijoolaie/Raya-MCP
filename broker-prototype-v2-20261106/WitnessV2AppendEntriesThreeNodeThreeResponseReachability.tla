---- MODULE WitnessV2AppendEntriesThreeNodeThreeResponseReachability ----
EXTENDS Naturals, FiniteSets, Sequences, TLC

Nodes == {"n1", "n2", "n3"}
Leader == "LEADER"
Follower == "FOLLOWER"
Quorum == 2
MaxLog == 2
MaxRequestId == 3

VARIABLES currentTerm, role, log, commitIndex, nextIndex, matchIndex,
          lastRequestId, messages, handled
vars == <<currentTerm, role, log, commitIndex, nextIndex, matchIndex,
          lastRequestId, messages, handled>>

PrevMatches(localLog, prevIndex, prevTerm) ==
  IF prevIndex = 0 THEN prevTerm = 0
  ELSE IF prevIndex <= Len(localLog)
       THEN localLog[prevIndex] = prevTerm
       ELSE FALSE

Max(a, b) == IF a >= b THEN a ELSE b

AppendResult(localLog, prevIndex, entries) ==
  LET conflicts ==
        {k \in 1..Len(entries) :
          prevIndex + k <= Len(localLog) /\
          localLog[prevIndex + k] # entries[k]}
  IN IF conflicts # {}
     THEN LET firstConflict == CHOOSE x \in conflicts :
                \A y \in conflicts : x <= y
          IN SubSeq(localLog, 1, prevIndex + firstConflict - 1) \o
             SubSeq(entries, firstConflict, Len(entries))
     ELSE IF prevIndex + Len(entries) > Len(localLog)
          THEN localLog \o
               SubSeq(entries, Len(localLog) - prevIndex + 1,
                      Len(entries))
          ELSE localLog

Init ==
  /\ currentTerm = [n \in Nodes |-> 1]
  /\ role = [n \in Nodes |-> IF n = "n1" THEN Leader ELSE Follower]
  /\ log = [n \in Nodes |-> <<>>]
  /\ commitIndex = [n \in Nodes |-> 0]
  /\ nextIndex = [n \in Nodes |-> [p \in Nodes |-> 1]]
  /\ matchIndex = [n \in Nodes |-> [p \in Nodes |-> 0]]
  /\ lastRequestId = [n \in Nodes |-> [p \in Nodes |-> 0]]
  /\ messages = {}
  /\ handled = <<>>

LeaderAppend ==
  /\ role["n1"] = Leader
  /\ Len(log["n1"]) < MaxLog
  /\ log' = [log EXCEPT !["n1"] = Append(@, currentTerm["n1"])]
  /\ UNCHANGED <<currentTerm, role, commitIndex, nextIndex,
                 matchIndex, lastRequestId, messages, handled>>

SendAppend(p) ==
  /\ p \in {"n2", "n3"}
  /\ role["n1"] = Leader
  /\ lastRequestId["n1"][p] < MaxRequestId
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
  /\ UNCHANGED <<currentTerm, role, log, commitIndex, nextIndex,
                 matchIndex, handled>>

DeliverAppend(m) ==
  /\ m \in messages
  /\ m.kind = "APPEND"
  /\ m.dst \in Nodes
  /\ m.term >= currentTerm[m.dst]
  /\ PrevMatches(log[m.dst], m.prevIndex, m.prevTerm)
  /\ m.prevIndex >= commitIndex[m.dst]
  /\ m.prevIndex + Len(m.entries) <= MaxLog
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
  /\ UNCHANGED <<nextIndex, matchIndex, lastRequestId, handled>>

RejectAppend(m) ==
  /\ m \in messages
  /\ m.kind = "APPEND"
  /\ m.dst \in Nodes
  /\ m.term < currentTerm[m.dst]
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
  /\ UNCHANGED <<log, commitIndex, nextIndex, matchIndex,
                 lastRequestId, handled>>

HandleAppendResponse(m) ==
  /\ m \in messages
  /\ m.kind = "APPEND_RESP"
  /\ m.dst \in Nodes
  /\ role[m.dst] = Leader
  /\ m.term = currentTerm[m.dst]
  /\ IF m.success
        THEN
          /\ matchIndex' = [matchIndex EXCEPT
               ![m.dst][m.src] =
                 IF m.matchIndex > @ THEN m.matchIndex ELSE @]
          /\ nextIndex' = [nextIndex EXCEPT
               ![m.dst][m.src] =
                 IF m.matchIndex + 1 > @ THEN m.matchIndex + 1 ELSE @]
        ELSE
          /\ m.requestId = lastRequestId[m.dst][m.src]
          /\ nextIndex' = [nextIndex EXCEPT
               ![m.dst][m.src] =
                 Max(IF @ > 1 THEN @ - 1 ELSE 1,
                     matchIndex[m.dst][m.src] + 1)]
          /\ UNCHANGED matchIndex
  /\ messages' = messages \ {m}
  /\ handled' = Append(handled,
       [peer |-> m.src, requestId |-> m.requestId,
        success |-> m.success, ack |-> m.matchIndex])
  /\ UNCHANGED <<currentTerm, role, log, commitIndex, lastRequestId>>

AdvanceCommit ==
  /\ role["n1"] = Leader
  /\ \E idx \in (commitIndex["n1"] + 1)..Len(log["n1"]) :
       /\ log["n1"][idx] = currentTerm["n1"]
       /\ Cardinality({p \in Nodes \ {"n1"} :
            matchIndex["n1"][p] >= idx}) + 1 >= Quorum
       /\ commitIndex' = [commitIndex EXCEPT !["n1"] = idx]
  /\ UNCHANGED <<currentTerm, role, log, nextIndex, matchIndex,
                 lastRequestId, messages, handled>>

Next ==
  \/ LeaderAppend
  \/ \E p \in {"n2", "n3"} : SendAppend(p)
  \/ \E m \in messages : DeliverAppend(m)
  \/ \E m \in messages : RejectAppend(m)
  \/ \E m \in messages : HandleAppendResponse(m)
  \/ AdvanceCommit

TargetResponses ==
  /\ Cardinality({i \in 1..Len(handled) : handled[i].peer = "n2"}) = 3
  /\ {handled[i].ack :
       i \in {j \in 1..Len(handled) : handled[j].peer = "n2"}} = {0, 1, 2}
  /\ \E i \in 1..Len(handled) :
       handled[i].peer = "n2" /\ ~handled[i].success
  /\ \E i \in 1..Len(handled) :
       handled[i].peer = "n2" /\ handled[i].requestId = 1 /\
       handled[i].success /\ handled[i].ack = 1
  /\ \E i \in 1..Len(handled) :
       handled[i].peer = "n2" /\ handled[i].requestId = 2 /\
       handled[i].success /\ handled[i].ack = 2
  /\ \E i \in 1..Len(handled) :
       handled[i].peer = "n2" /\ handled[i].requestId = 3 /\
       ~handled[i].success /\ handled[i].ack = 0

TargetNotReached == ~TargetResponses

TypeOK ==
  /\ currentTerm \in [Nodes -> 1..2]
  /\ role \in [Nodes -> {Follower, Leader}]
  /\ log \in [Nodes -> Seq(1..2)]
  /\ \A n \in Nodes : Len(log[n]) <= MaxLog
  /\ commitIndex \in [Nodes -> 0..MaxLog]
  /\ \A n \in Nodes : commitIndex[n] <= Len(log[n])
  /\ nextIndex \in [Nodes -> [Nodes -> 1..(MaxLog + 1)]]
  /\ matchIndex \in [Nodes -> [Nodes -> 0..MaxLog]]
  /\ lastRequestId \in [Nodes -> [Nodes -> 0..MaxRequestId]]
  /\ messages \subseteq
       ([kind : {"APPEND"}, src : Nodes, dst : Nodes,
         term : 1..2, requestId : 1..MaxRequestId,
         prevIndex : 0..MaxLog, prevTerm : 0..2,
         entries : Seq(1..2), leaderCommit : 0..MaxLog]
        \cup
        [kind : {"APPEND_RESP"}, src : Nodes, dst : Nodes,
         term : 1..2, requestId : 1..MaxRequestId,
         success : BOOLEAN, matchIndex : 0..MaxLog])
  /\ \A m \in messages :
       IF m.kind = "APPEND"
       THEN Len(m.entries) <= MaxLog
       ELSE m.matchIndex <= MaxLog
  /\ handled \in Seq([peer : Nodes, requestId : 1..MaxRequestId,
                      success : BOOLEAN, ack : 0..MaxLog])

NextIndexNotBelowMatch ==
  \A p \in {"n2", "n3"} :
    nextIndex["n1"][p] >= matchIndex["n1"][p] + 1

CommittedPrefixBound ==
  \A n \in Nodes : commitIndex[n] <= Len(log[n])

Spec == Init /\ [][Next]_vars
====
