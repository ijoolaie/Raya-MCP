---- MODULE WitnessV2AppendEntriesThreeResponseReachability ----
EXTENDS Naturals, FiniteSets, Sequences, TLC

Nodes == {"n1", "n2"}
Leader == "LEADER"
Follower == "FOLLOWER"

VARIABLES logL, logF, commitL, commitF, nextI, matchI, lastReq, messages, responses
vars == <<logL, logF, commitL, commitF, nextI, matchI, lastReq, messages, responses>>

Max(a,b) == IF a >= b THEN a ELSE b
Min2(a,b) == IF a <= b THEN a ELSE b
PrevMatches(localLog, prev, term) == IF prev = 0 THEN term = 0 ELSE prev <= Len(localLog) /\ localLog[prev] = term
AppendResult(localLog, prev, entries) ==
  IF prev + Len(entries) > Len(localLog)
  THEN localLog \o SubSeq(entries, Len(localLog)-prev+1, Len(entries))
  ELSE localLog

Init ==
  /\ logL = <<>> /\ logF = <<>>
  /\ commitL = 0 /\ commitF = 0
  /\ nextI = 1 /\ matchI = 0 /\ lastReq = 0
  /\ messages = {} /\ responses = <<>>

LeaderAppend ==
  /\ Len(logL) < 2
  /\ logL' = Append(logL, Len(logL)+1)
  /\ UNCHANGED <<logF, commitL, commitF, nextI, matchI, lastReq, messages, responses>>

SendAppend ==
  /\ lastReq < 3
  /\ nextI \in 1..(Len(logL)+1)
  /\ LET prev == nextI-1
         rid == lastReq+1
     IN messages' = messages \cup {[kind |-> "APPEND", requestId |-> rid,
          prevIndex |-> prev, prevTerm |-> IF prev=0 THEN 0 ELSE logL[prev],
          entries |-> SubSeq(logL, prev+1, Len(logL)), leaderCommit |-> commitL]}
  /\ lastReq' = lastReq+1
  /\ UNCHANGED <<logL, logF, commitL, commitF, nextI, matchI, responses>>

Deliver(m) ==
  /\ m \in messages /\ m.kind = "APPEND"
  /\ m.prevIndex >= commitF
  /\ PrevMatches(logF, m.prevIndex, m.prevTerm)
  /\ m.prevIndex + Len(m.entries) <= 2
  /\ LET newLog == AppendResult(logF, m.prevIndex, m.entries)
         ack == m.prevIndex + Len(m.entries)
     IN /\ logF' = newLog
        /\ commitF' = Min2(Len(newLog), Max(commitF,m.leaderCommit))
        /\ messages' = (messages \ {m}) \cup {[kind |-> "RESP", requestId |-> m.requestId, success |-> TRUE, ack |-> ack]}
  /\ UNCHANGED <<logL, commitL, nextI, matchI, lastReq, responses>>

Reject(m) ==
  /\ m \in messages /\ m.kind = "APPEND"
  /\ ~PrevMatches(logF,m.prevIndex,m.prevTerm) \/ m.prevIndex < commitF
  /\ messages' = (messages \ {m}) \cup {[kind |-> "RESP", requestId |-> m.requestId, success |-> FALSE, ack |-> 0]}
  /\ UNCHANGED <<logL, logF, commitL, commitF, nextI, matchI, lastReq, responses>>

Handle(m) ==
  /\ m \in messages /\ m.kind = "RESP"
  /\ IF m.success
       THEN /\ matchI' = Max(matchI,m.ack)
            /\ nextI' = Max(nextI,m.ack+1)
       ELSE /\ m.requestId = lastReq
            /\ matchI' = matchI
            /\ nextI' = Max(nextI-1,matchI+1)
  /\ messages' = messages \ {m}
  /\ responses' = Append(responses,[requestId |-> m.requestId, success |-> m.success, ack |-> m.ack])
  /\ UNCHANGED <<logL, logF, commitL, commitF, lastReq>>

AdvanceCommit ==
  /\ commitL < Len(logL) /\ matchI >= commitL+1
  /\ commitL' = commitL+1
  /\ UNCHANGED <<logL, logF, commitF, nextI, matchI, lastReq, messages, responses>>

Next == LeaderAppend \/ SendAppend \/ (\E m \in messages : Deliver(m)) \/ (\E m \in messages : Reject(m)) \/ (\E m \in messages : Handle(m)) \/ AdvanceCommit
Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ logL \in Seq(1..2) /\ logF \in Seq(1..2)
  /\ Len(logL) <= 2 /\ Len(logF) <= 2
  /\ commitL \in 0..2 /\ commitF \in 0..2
  /\ nextI \in 1..3 /\ matchI \in 0..2 /\ lastReq \in 0..3
  /\ messages \subseteq ([kind:{"APPEND"},requestId:1..3,prevIndex:0..2,prevTerm:0..2,entries:Seq(1..2),leaderCommit:0..2] \cup [kind:{"RESP"},requestId:1..3,success:BOOLEAN,ack:0..2])
NextNotBelowMatch == nextI >= matchI+1
AllResponsesHandled == messages \cap {[kind |-> "RESP", requestId |-> 1, success |-> TRUE, ack |-> 1]} = {} \/ TRUE
ThreeResponseTarget == Len(responses)=3 /\ {responses[i].ack : i \in 1..Len(responses)} = {0,1,2} /\ \E i \in 1..Len(responses) : ~responses[i].success
TargetNotReached == ~ThreeResponseTarget
====


