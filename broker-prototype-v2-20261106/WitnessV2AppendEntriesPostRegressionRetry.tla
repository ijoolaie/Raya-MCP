---- MODULE WitnessV2AppendEntriesPostRegressionRetry ----
EXTENDS Naturals, Sequences, TLC

VARIABLES stage, followerLog, followerCommit, messages,
          leaderNext, leaderMatch, lastRequestId
vars == <<stage, followerLog, followerCommit, messages,
          leaderNext, leaderMatch, lastRequestId>>

Req4 == [kind |-> "APPEND", requestId |-> 4,
         prevIndex |-> 1, prevTerm |-> 1,
         entries |-> <<2>>, leaderCommit |-> 1]
Resp4 == [kind |-> "APPEND_RESP", requestId |-> 4,
          success |-> TRUE, matchIndex |-> 2, term |-> 1]

Init ==
  /\ stage = 0
  /\ followerLog = <<1, 2>>
  /\ followerCommit = 1
  /\ messages = {}
  /\ leaderNext = 2
  /\ leaderMatch = 2
  /\ lastRequestId = 3

SendRetry4 ==
  /\ stage = 0
  /\ leaderNext = 2
  /\ lastRequestId = 3
  /\ messages' = {Req4}
  /\ lastRequestId' = 4
  /\ stage' = 1
  /\ UNCHANGED <<followerLog, followerCommit, leaderNext, leaderMatch>>

AcceptRetry4 ==
  /\ stage = 1
  /\ Req4 \in messages
  /\ Req4.prevIndex >= followerCommit
  /\ followerLog[Req4.prevIndex] = Req4.prevTerm
  /\ followerLog' = followerLog
  /\ followerCommit' = followerCommit
  /\ messages' = (messages \ {Req4}) \cup {Resp4}
  /\ stage' = 2
  /\ UNCHANGED <<leaderNext, leaderMatch, lastRequestId>>

HandleSuccess4 ==
  /\ stage = 2
  /\ Resp4 \in messages
  /\ leaderMatch' = IF Resp4.matchIndex > leaderMatch
                       THEN Resp4.matchIndex ELSE leaderMatch
  /\ leaderNext' = IF Resp4.matchIndex + 1 > leaderNext
                      THEN Resp4.matchIndex + 1 ELSE leaderNext
  /\ messages' = messages \ {Resp4}
  /\ stage' = 3
  /\ UNCHANGED <<followerLog, followerCommit, lastRequestId>>

Next == SendRetry4 \/ AcceptRetry4 \/ HandleSuccess4

TypeOK ==
  /\ stage \in 0..3
  /\ followerLog \in {<<1, 2>>}
  /\ followerCommit \in 0..2
  /\ followerCommit <= Len(followerLog)
  /\ messages \subseteq {Req4, Resp4}
  /\ leaderNext \in 1..3
  /\ leaderMatch \in 0..2
  /\ lastRequestId \in 3..4

CommittedPrefixPreserved ==
  followerCommit >= 1 => followerLog[1] = 1

CursorBounds ==
  /\ leaderNext <= 3
  /\ leaderMatch <= 2

RetryUsesRegressedCursor ==
  stage >= 1 => lastRequestId = 4

RetryReplaysAlreadyMatchedSuffix ==
  stage >= 1 => Req4.prevIndex + Len(Req4.entries) <= leaderMatch

RecoveredCursor ==
  stage = 3 => leaderNext = leaderMatch + 1

Spec == Init /\ [][Next]_vars
====
