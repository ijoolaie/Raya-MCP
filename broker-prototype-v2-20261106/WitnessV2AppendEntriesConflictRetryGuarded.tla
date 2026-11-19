---- MODULE WitnessV2AppendEntriesConflictRetryGuarded ----
EXTENDS Naturals, Sequences, TLC

VARIABLES stage, leaderLog, followerLog, followerCommit,
          messages, leaderNext, leaderMatch, lastRequestId
vars == <<stage, leaderLog, followerLog, followerCommit,
          messages, leaderNext, leaderMatch, lastRequestId>>

MinSet(S) == CHOOSE x \in S : \A y \in S : x <= y

FirstReq ==
  [kind |-> "APPEND", requestId |-> 1, prevIndex |-> 2,
   prevTerm |-> 1, entries |-> <<2>>, leaderCommit |-> 1]
FirstFail ==
  [kind |-> "APPEND_RESP", requestId |-> 1, success |-> FALSE,
   matchIndex |-> 0, term |-> 1]
RetryReq ==
  [kind |-> "APPEND", requestId |-> 2, prevIndex |-> 1,
   prevTerm |-> 1, entries |-> <<1, 2>>, leaderCommit |-> 1]
RetrySuccess ==
  [kind |-> "APPEND_RESP", requestId |-> 2, success |-> TRUE,
   matchIndex |-> 3, term |-> 1]

Init ==
  /\ stage = 0
  /\ leaderLog = <<1, 1, 2>>
  /\ followerLog = <<1, 9, 9>>
  /\ followerCommit = 1
  /\ messages = {}
  /\ leaderNext = 3
  /\ leaderMatch = 1
  /\ lastRequestId = 0

SendFirst ==
  /\ stage = 0
  /\ leaderNext = 3
  /\ messages' = {FirstReq}
  /\ lastRequestId' = 1
  /\ stage' = 1
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit, leaderNext, leaderMatch>>

RejectFirst ==
  /\ stage = 1
  /\ FirstReq \in messages
  /\ FirstReq.prevIndex <= Len(followerLog)
  /\ followerLog[FirstReq.prevIndex] # FirstReq.prevTerm
  /\ messages' = (messages \ {FirstReq}) \cup {FirstFail}
  /\ stage' = 2
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit, leaderNext, leaderMatch, lastRequestId>>

HandleFirstFailure ==
  /\ stage = 2
  /\ FirstFail \in messages
  /\ FirstFail.requestId = lastRequestId
  /\ leaderNext' = IF leaderNext - 1 > leaderMatch + 1 THEN leaderNext - 1 ELSE leaderMatch + 1
  /\ messages' = messages \ {FirstFail}
  /\ stage' = 3
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit, leaderMatch, lastRequestId>>

SendRetry ==
  /\ stage = 3
  /\ leaderNext = 2
  /\ lastRequestId = 1
  /\ messages' = {RetryReq}
  /\ lastRequestId' = 2
  /\ stage' = 4
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit, leaderNext, leaderMatch>>

DeliverRetry ==
  /\ stage = 4
  /\ RetryReq \in messages
  /\ RetryReq.prevIndex >= followerCommit
  /\ followerLog[RetryReq.prevIndex] = RetryReq.prevTerm
  /\ LET conflicts ==
       {k \in 1..Len(RetryReq.entries) :
          RetryReq.prevIndex + k <= Len(followerLog) /\
          followerLog[RetryReq.prevIndex + k] # RetryReq.entries[k]}
     IN
       /\ conflicts # {}
       /\ followerLog' =
          SubSeq(followerLog, 1, RetryReq.prevIndex + MinSet(conflicts) - 1)
          \o SubSeq(RetryReq.entries, MinSet(conflicts), Len(RetryReq.entries))
  /\ messages' = (messages \ {RetryReq}) \cup {RetrySuccess}
  /\ stage' = 5
  /\ UNCHANGED <<leaderLog, followerCommit, leaderNext, leaderMatch, lastRequestId>>

HandleRetrySuccess ==
  /\ stage = 5
  /\ RetrySuccess \in messages
  /\ leaderMatch' = IF RetrySuccess.matchIndex > leaderMatch
                       THEN RetrySuccess.matchIndex ELSE leaderMatch
  /\ leaderNext' = IF RetrySuccess.matchIndex + 1 > leaderNext
                      THEN RetrySuccess.matchIndex + 1 ELSE leaderNext
  /\ messages' = messages \ {RetrySuccess}
  /\ stage' = 6
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit, lastRequestId>>

Next == SendFirst \/ RejectFirst \/ HandleFirstFailure \/
        SendRetry \/ DeliverRetry \/ HandleRetrySuccess

TypeOK ==
  /\ stage \in 0..6
  /\ leaderLog = <<1, 1, 2>>
  /\ followerLog \in {<<1, 9, 9>>, <<1, 1, 2>>}
  /\ followerCommit = 1
  /\ messages \subseteq {FirstReq, FirstFail, RetryReq, RetrySuccess}
  /\ leaderNext \in 1..4
  /\ leaderMatch \in 0..3
  /\ lastRequestId \in 0..2

CommittedPrefixPreserved ==
  followerCommit = 1 => followerLog[1] = leaderLog[1]

RetryMovesBackOne ==
  stage \in 3..5 => leaderNext = 2

ConflictWasReal ==
  stage >= 1 => FirstReq.prevIndex <= Len(<<1, 9, 9>>)

FollowerConflictRepaired ==
  stage >= 5 => followerLog = leaderLog

LeaderProgressConsistent ==
  stage = 6 => leaderMatch = 3 /\ leaderNext = 4

CommitIndexBound ==
  followerCommit <= Len(followerLog)

Spec == Init /\ [][Next]_vars
====