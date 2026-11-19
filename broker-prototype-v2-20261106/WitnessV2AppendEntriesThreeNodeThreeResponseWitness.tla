---- MODULE WitnessV2AppendEntriesThreeNodeThreeResponseWitness ----
EXTENDS Naturals, FiniteSets, Sequences, TLC

Nodes == {"n1", "n2", "n3"}
MaxLog == 2

VARIABLES phase, log, commitIndex, nextIndex, matchIndex,
          lastRequestId, messages, handled
vars == <<phase, log, commitIndex, nextIndex, matchIndex,
          lastRequestId, messages, handled>>

Max(a, b) == IF a >= b THEN a ELSE b
Min(a, b) == IF a <= b THEN a ELSE b
PrevMatches(localLog, prevIndex, prevTerm) ==
  IF prevIndex = 0 THEN prevTerm = 0
  ELSE IF prevIndex <= Len(localLog)
       THEN localLog[prevIndex] = prevTerm
       ELSE FALSE

AppendResult(localLog, prevIndex, entries) ==
  IF prevIndex + Len(entries) > Len(localLog)
  THEN localLog \o
       SubSeq(entries, Len(localLog) - prevIndex + 1, Len(entries))
  ELSE localLog

Init ==
  /\ phase = 0
  /\ log = [n \in Nodes |-> <<>>]
  /\ commitIndex = [n \in Nodes |-> 0]
  /\ nextIndex = [n \in Nodes |-> [p \in Nodes |-> 1]]
  /\ matchIndex = [n \in Nodes |-> [p \in Nodes |-> 0]]
  /\ lastRequestId = [n \in Nodes |-> [p \in Nodes |-> 0]]
  /\ messages = {}
  /\ handled = <<>>

AppendFirst ==
  /\ phase = 0
  /\ log' = [log EXCEPT !["n1"] = Append(@, 1)]
  /\ phase' = 1
  /\ UNCHANGED <<commitIndex, nextIndex, matchIndex, lastRequestId,
                 messages, handled>>

SendN2First ==
  /\ phase = 1
  /\ messages' = messages \cup
       {[kind |-> "APPEND", src |-> "n1", dst |-> "n2",
         term |-> 1, requestId |-> 1, prevIndex |-> 0, prevTerm |-> 0,
         entries |-> <<1>>, leaderCommit |-> 0]}
  /\ lastRequestId' = [lastRequestId EXCEPT !["n1"]["n2"] = 1]
  /\ phase' = 2
  /\ UNCHANGED <<log, commitIndex, nextIndex, matchIndex, handled>>

DeliverN2First ==
  /\ phase = 2
  /\ \E m \in messages :
       /\ m.kind = "APPEND" /\ m.dst = "n2" /\ m.requestId = 1
       /\ PrevMatches(log["n2"], m.prevIndex, m.prevTerm)
       /\ m.prevIndex >= commitIndex["n2"]
       /\ LET newLog == AppendResult(log["n2"], m.prevIndex, m.entries)
          IN
            /\ log' = [log EXCEPT !["n2"] = newLog]
            /\ commitIndex' = [commitIndex EXCEPT !["n2"] =
                 Min(Len(newLog), Max(@, m.leaderCommit))]
            /\ messages' = (messages \ {m}) \cup
                 {[kind |-> "APPEND_RESP", src |-> "n2", dst |-> "n1",
                   term |-> 1, requestId |-> 1, success |-> TRUE,
                   matchIndex |-> 1]}
  /\ phase' = 3
  /\ UNCHANGED <<nextIndex, matchIndex, lastRequestId, handled>>

SendN3 ==
  /\ phase = 3
  /\ messages' = messages \cup
       {[kind |-> "APPEND", src |-> "n1", dst |-> "n3",
         term |-> 1, requestId |-> 1, prevIndex |-> 0, prevTerm |-> 0,
         entries |-> <<1>>, leaderCommit |-> 0]}
  /\ lastRequestId' = [lastRequestId EXCEPT !["n1"]["n3"] = 1]
  /\ phase' = 4
  /\ UNCHANGED <<log, commitIndex, nextIndex, matchIndex, handled>>

DeliverN3 ==
  /\ phase = 4
  /\ \E m \in messages :
       /\ m.kind = "APPEND" /\ m.dst = "n3" /\ m.requestId = 1
       /\ PrevMatches(log["n3"], m.prevIndex, m.prevTerm)
       /\ m.prevIndex >= commitIndex["n3"]
       /\ LET newLog == AppendResult(log["n3"], m.prevIndex, m.entries)
          IN
            /\ log' = [log EXCEPT !["n3"] = newLog]
            /\ commitIndex' = [commitIndex EXCEPT !["n3"] =
                 Min(Len(newLog), Max(@, m.leaderCommit))]
            /\ messages' = (messages \ {m}) \cup
                 {[kind |-> "APPEND_RESP", src |-> "n3", dst |-> "n1",
                   term |-> 1, requestId |-> 1, success |-> TRUE,
                   matchIndex |-> 1]}
  /\ phase' = 5
  /\ UNCHANGED <<nextIndex, matchIndex, lastRequestId, handled>>

HandleN3 ==
  /\ phase = 5
  /\ \E m \in messages :
       /\ m.kind = "APPEND_RESP" /\ m.src = "n3" /\ m.requestId = 1
       /\ m.term = 1 /\ m.success
       /\ matchIndex' = [matchIndex EXCEPT !["n1"]["n3"] = Max(@, 1)]
       /\ nextIndex' = [nextIndex EXCEPT !["n1"]["n3"] = Max(@, 2)]
       /\ messages' = messages \ {m}
       /\ handled' = Append(handled,
            [peer |-> "n3", requestId |-> 1, success |-> TRUE, ack |-> 1])
  /\ phase' = 6
  /\ UNCHANGED <<log, commitIndex, lastRequestId>>

CommitLeader ==
  /\ phase = 6
  /\ commitIndex["n1"] = 0
  /\ log["n1"] = <<1>>
  /\ matchIndex["n1"]["n3"] >= 1
  /\ commitIndex' = [commitIndex EXCEPT !["n1"] = 1]
  /\ phase' = 7
  /\ UNCHANGED <<log, nextIndex, matchIndex, lastRequestId, messages, handled>>

AppendSecond ==
  /\ phase = 7
  /\ log' = [log EXCEPT !["n1"] = Append(@, 1)]
  /\ phase' = 8
  /\ UNCHANGED <<commitIndex, nextIndex, matchIndex, lastRequestId,
                 messages, handled>>

SendN2Second ==
  /\ phase = 8
  /\ messages' = messages \cup
       {[kind |-> "APPEND", src |-> "n1", dst |-> "n2",
         term |-> 1, requestId |-> 2, prevIndex |-> 0, prevTerm |-> 0,
         entries |-> <<1, 1>>, leaderCommit |-> 1]}
  /\ lastRequestId' = [lastRequestId EXCEPT !["n1"]["n2"] = 2]
  /\ phase' = 9
  /\ UNCHANGED <<log, commitIndex, nextIndex, matchIndex, handled>>

SendN2Third ==
  /\ phase = 9
  /\ messages' = messages \cup
       {[kind |-> "APPEND", src |-> "n1", dst |-> "n2",
         term |-> 1, requestId |-> 3, prevIndex |-> 0, prevTerm |-> 0,
         entries |-> <<1, 1>>, leaderCommit |-> 1]}
  /\ lastRequestId' = [lastRequestId EXCEPT !["n1"]["n2"] = 3]
  /\ phase' = 10
  /\ UNCHANGED <<log, commitIndex, nextIndex, matchIndex, handled>>

DeliverN2Second ==
  /\ phase = 10
  /\ \E m \in messages :
       /\ m.kind = "APPEND" /\ m.dst = "n2" /\ m.requestId = 2
       /\ PrevMatches(log["n2"], m.prevIndex, m.prevTerm)
       /\ m.prevIndex >= commitIndex["n2"]
       /\ LET newLog == AppendResult(log["n2"], m.prevIndex, m.entries)
          IN
            /\ log' = [log EXCEPT !["n2"] = newLog]
            /\ commitIndex' = [commitIndex EXCEPT !["n2"] =
                 Min(Len(newLog), Max(@, m.leaderCommit))]
            /\ messages' = (messages \ {m}) \cup
                 {[kind |-> "APPEND_RESP", src |-> "n2", dst |-> "n1",
                   term |-> 1, requestId |-> 2, success |-> TRUE,
                   matchIndex |-> 2]}
  /\ phase' = 11
  /\ UNCHANGED <<nextIndex, matchIndex, lastRequestId, handled>>

RejectN2Third ==
  /\ phase = 11
  /\ \E m \in messages :
       /\ m.kind = "APPEND" /\ m.dst = "n2" /\ m.requestId = 3
       /\ m.prevIndex < commitIndex["n2"]
       /\ messages' = (messages \ {m}) \cup
            {[kind |-> "APPEND_RESP", src |-> "n2", dst |-> "n1",
              term |-> 1, requestId |-> 3, success |-> FALSE,
              matchIndex |-> 0]}
  /\ phase' = 12
  /\ UNCHANGED <<log, commitIndex, nextIndex, matchIndex,
                 lastRequestId, handled>>

HandleN2Response(m) ==
  /\ phase = 12
  /\ m \in messages
  /\ m.kind = "APPEND_RESP" /\ m.src = "n2" /\ m.dst = "n1"
  /\ m.term = 1
  /\ IF m.success
       THEN
         /\ matchIndex' = [matchIndex EXCEPT
              !["n1"]["n2"] = Max(@, m.matchIndex)]
         /\ nextIndex' = [nextIndex EXCEPT
              !["n1"]["n2"] = Max(@, m.matchIndex + 1)]
       ELSE
         /\ m.requestId = lastRequestId["n1"]["n2"]
         /\ nextIndex' = [nextIndex EXCEPT
              !["n1"]["n2"] =
                Max(IF @ > 1 THEN @ - 1 ELSE 1,
                    matchIndex["n1"]["n2"] + 1)]
         /\ UNCHANGED matchIndex
  /\ messages' = messages \ {m}
  /\ handled' = Append(handled,
       [peer |-> m.src, requestId |-> m.requestId,
        success |-> m.success, ack |-> m.matchIndex])
  /\ phase' = IF Cardinality({x \in messages \ {m} :
                  x.kind = "APPEND_RESP" /\ x.src = "n2"}) = 0
              THEN 13 ELSE 12
  /\ UNCHANGED <<log, commitIndex, lastRequestId>>

Next ==
  \/ AppendFirst
  \/ SendN2First
  \/ DeliverN2First
  \/ SendN3
  \/ DeliverN3
  \/ HandleN3
  \/ CommitLeader
  \/ AppendSecond
  \/ SendN2Second
  \/ SendN2Third
  \/ DeliverN2Second
  \/ RejectN2Third
  \/ \E m \in messages : HandleN2Response(m)

TargetResponses ==
  /\ phase = 13
  /\ Cardinality({i \in 1..Len(handled) : handled[i].peer = "n2"}) = 3
  /\ {handled[i].ack :
       i \in {j \in 1..Len(handled) : handled[j].peer = "n2"}} = {0, 1, 2}
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
  /\ phase \in 0..13
  /\ log \in [Nodes -> Seq(1..2)]
  /\ \A n \in Nodes : Len(log[n]) <= MaxLog
  /\ commitIndex \in [Nodes -> 0..MaxLog]
  /\ \A n \in Nodes : commitIndex[n] <= Len(log[n])
  /\ nextIndex \in [Nodes -> [Nodes -> 1..(MaxLog + 1)]]
  /\ matchIndex \in [Nodes -> [Nodes -> 0..MaxLog]]
  /\ lastRequestId \in [Nodes -> [Nodes -> 0..3]]
  /\ messages \subseteq
       ([kind : {"APPEND"}, src : Nodes, dst : Nodes, term : {1},
         requestId : 1..3, prevIndex : 0..MaxLog, prevTerm : 0..2,
         entries : Seq(1..2), leaderCommit : 0..MaxLog]
        \cup
        [kind : {"APPEND_RESP"}, src : Nodes, dst : Nodes, term : {1},
         requestId : 1..3, success : BOOLEAN, matchIndex : 0..MaxLog])
  /\ handled \in Seq([peer : Nodes, requestId : 1..3,
                      success : BOOLEAN, ack : 0..MaxLog])

NextIndexNotBelowMatch ==
  \A p \in {"n2", "n3"} :
    nextIndex["n1"][p] >= matchIndex["n1"][p] + 1

CommitBound ==
  \A n \in Nodes : commitIndex[n] <= Len(log[n])

Spec == Init /\ [][Next]_vars
====
