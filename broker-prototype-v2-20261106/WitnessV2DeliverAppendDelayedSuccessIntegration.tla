---- MODULE WitnessV2DeliverAppendDelayedSuccessIntegration ----
EXTENDS Naturals, Sequences, FiniteSets, TLC
CONSTANT AllowFollowerRollback
VARIABLES leaderLog, followerLog, nextIndex, matchIndex, lastRequestId,
          pending, accepted, badAck, stage
vars == <<leaderLog, followerLog, nextIndex, matchIndex, lastRequestId,
          pending, accepted, badAck, stage>>
Init ==
  /\ leaderLog = <<1,1,1,1>>
  /\ followerLog = <<1,1,1>>
  /\ nextIndex = 4
  /\ matchIndex = 0
  /\ lastRequestId = 0
  /\ pending = {}
  /\ accepted = {}
  /\ badAck = FALSE
  /\ stage = "INIT"
SendOldAppend ==
  /\ stage = "INIT"
  /\ lastRequestId' = 1
  /\ pending' = pending \cup
       {[kind |-> "APPEND", requestId |-> 1, prevIndex |-> 1,
         prevTerm |-> 1, entries |-> <<1,1>>, term |-> 1]}
  /\ stage' = "OLD_SENT"
  /\ UNCHANGED <<leaderLog, followerLog, nextIndex, matchIndex, accepted, badAck>>
DeliverOldAppend(m) ==
  /\ m \in pending
  /\ m.kind = "APPEND"
  /\ m.requestId = 1
  /\ m.prevIndex <= Len(followerLog)
  /\ followerLog[m.prevIndex] = m.prevTerm
  /\ followerLog' = SubSeq(followerLog,1,m.prevIndex) \o m.entries
  /\ pending' = (pending \ {m}) \cup
       {[kind |-> "RESP", requestId |-> m.requestId,
         success |-> TRUE, matchIndex |-> m.prevIndex + Len(m.entries)]}
  /\ accepted' = accepted \cup {m.requestId}
  /\ stage' = "OLD_DELIVERED"
  /\ UNCHANGED <<leaderLog, nextIndex, matchIndex, lastRequestId, badAck>>
SendNewAppend ==
  /\ stage = "OLD_DELIVERED"
  /\ lastRequestId' = 2
  /\ pending' = pending \cup
       {[kind |-> "APPEND", requestId |-> 2, prevIndex |-> 4,
         prevTerm |-> 1, entries |-> <<>>, term |-> 1]}
  /\ stage' = "NEW_SENT"
  /\ UNCHANGED <<leaderLog, followerLog, nextIndex, matchIndex, accepted, badAck>>
RejectNewAppend(m) ==
  /\ m \in pending
  /\ m.kind = "APPEND"
  /\ m.requestId = 2
  /\ m.prevIndex > Len(followerLog)
  /\ pending' = (pending \ {m}) \cup
       {[kind |-> "RESP", requestId |-> 2,
         success |-> FALSE, matchIndex |-> 0]}
  /\ stage' = "NEW_REJECTED"
  /\ UNCHANGED <<leaderLog, followerLog, nextIndex, matchIndex,
                 lastRequestId, accepted, badAck>>
HandleNewFailure(m) ==
  /\ m \in pending
  /\ m.kind = "RESP"
  /\ m.requestId = 2
  /\ ~m.success
  /\ nextIndex' = IF nextIndex - 1 > matchIndex + 1
                  THEN nextIndex - 1 ELSE matchIndex + 1
  /\ pending' = pending \ {m}
  /\ stage' = "FAILURE_HANDLED"
  /\ UNCHANGED <<leaderLog, followerLog, matchIndex, lastRequestId, accepted, badAck>>
HandleDelayedOldSuccess(m) ==
  /\ m \in pending
  /\ m.kind = "RESP"
  /\ m.requestId = 1
  /\ m.success
  /\ stage = "FAILURE_HANDLED"
  /\ matchIndex' = IF m.matchIndex > matchIndex THEN m.matchIndex ELSE matchIndex
  /\ nextIndex' = IF m.matchIndex + 1 > nextIndex THEN m.matchIndex + 1 ELSE nextIndex
  /\ badAck' = IF matchIndex' > Len(followerLog) THEN TRUE ELSE badAck
  /\ pending' = pending \ {m}
  /\ stage' = "DONE"
  /\ UNCHANGED <<leaderLog, followerLog, lastRequestId, accepted>>
RollbackFollowerPrefix ==
  /\ AllowFollowerRollback
  /\ stage = "FAILURE_HANDLED"
  /\ Len(followerLog) = 3
  /\ followerLog' = SubSeq(followerLog,1,2)
  /\ UNCHANGED <<leaderLog, nextIndex, matchIndex, lastRequestId,
                 pending, accepted, badAck, stage>>
Terminal ==
  /\ stage = "DONE"
  /\ UNCHANGED vars
Next ==
  \/ Terminal
  \/ SendOldAppend
  \/ \E m \in pending : DeliverOldAppend(m)
  \/ SendNewAppend
  \/ \E m \in pending : RejectNewAppend(m)
  \/ \E m \in pending : HandleNewFailure(m)
  \/ \E m \in pending : HandleDelayedOldSuccess(m)
  \/ RollbackFollowerPrefix
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
TypeOK ==
  /\ leaderLog \in Seq(1..1)
  /\ followerLog \in Seq(1..1)
  /\ nextIndex \in 1..5
  /\ matchIndex \in 0..4
  /\ lastRequestId \in 0..2
  /\ pending \subseteq
       ([kind : {"APPEND"}, requestId : 1..2, prevIndex : 0..4,
         prevTerm : 0..1, entries : Seq(1..1), term : 1..1] \cup
        [kind : {"RESP"}, requestId : 1..2, success : BOOLEAN, matchIndex : 0..4])
  /\ accepted \subseteq {1,2}
  /\ badAck \in BOOLEAN
CursorLowerBound == nextIndex >= matchIndex + 1
MatchWithinFollowerLog == matchIndex <= Len(followerLog)
NoBadAckMonitor == badAck = FALSE
EventuallyOldSuccessHandled == <> (stage = "DONE")
====
