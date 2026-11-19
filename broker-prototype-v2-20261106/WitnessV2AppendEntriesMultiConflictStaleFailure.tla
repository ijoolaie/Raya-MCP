---- MODULE WitnessV2AppendEntriesMultiConflictStaleFailure ----
EXTENDS Naturals, Sequences, TLC

VARIABLES stage, leaderLog, followerLog, followerCommit,
          messages, leaderNext, leaderMatch, lastRequestId
vars == <<stage, leaderLog, followerLog, followerCommit,
          messages, leaderNext, leaderMatch, lastRequestId>>
MinSet(S) == CHOOSE x \in S : \A y \in S : x <= y

Req1 ==
  [kind |-> "APPEND", requestId |-> 1, prevIndex |-> 3,
   prevTerm |-> 1, entries |-> <<3>>, leaderCommit |-> 1]
Fail1 ==
  [kind |-> "APPEND_RESP", requestId |-> 1, success |-> FALSE,
   matchIndex |-> 0, term |-> 1, mid |-> 11]
Req2 ==
  [kind |-> "APPEND", requestId |-> 2, prevIndex |-> 2,
   prevTerm |-> 1, entries |-> <<2,3>>, leaderCommit |-> 1]
Fail2a ==
  [kind |-> "APPEND_RESP", requestId |-> 2, success |-> FALSE,
   matchIndex |-> 0, term |-> 1, mid |-> 21]
Fail2b ==
  [kind |-> "APPEND_RESP", requestId |-> 2, success |-> FALSE,
   matchIndex |-> 0, term |-> 1, mid |-> 22]
Req3 ==
  [kind |-> "APPEND", requestId |-> 3, prevIndex |-> 1,
   prevTerm |-> 1, entries |-> <<1,2,3>>, leaderCommit |-> 1]
Success3 ==
  [kind |-> "APPEND_RESP", requestId |-> 3, success |-> TRUE,
   matchIndex |-> 4, term |-> 1, mid |-> 31]

Init ==
  /\ stage = 0
  /\ leaderLog = <<1,1,2,3>>
  /\ followerLog = <<1,9,9,8>>
  /\ followerCommit = 1
  /\ messages = {}
  /\ leaderNext = 4
  /\ leaderMatch = 1
  /\ lastRequestId = 0

Send1 ==
  /\ stage = 0
  /\ leaderNext = 4
  /\ messages' = {Req1}
  /\ lastRequestId' = 1
  /\ stage' = 1
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit, leaderNext, leaderMatch>>

Reject1 ==
  /\ stage = 1
  /\ Req1 \in messages
  /\ followerLog[Req1.prevIndex] # Req1.prevTerm
  /\ messages' = (messages \ {Req1}) \cup {Fail1}
  /\ stage' = 2
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit, leaderNext, leaderMatch, lastRequestId>>

HandleFail1 ==
  /\ stage = 2
  /\ Fail1 \in messages
  /\ Fail1.requestId = lastRequestId
  /\ leaderNext' = IF leaderNext > 1 THEN leaderNext - 1 ELSE 1
  /\ messages' = messages \ {Fail1}
  /\ stage' = 3
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit, leaderMatch, lastRequestId>>

Send2 ==
  /\ stage = 3
  /\ leaderNext = 3
  /\ messages' = messages \cup {Req2}
  /\ lastRequestId' = 2
  /\ stage' = 4
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit, leaderNext, leaderMatch>>

Reject2 ==
  /\ stage = 4
  /\ Req2 \in messages
  /\ followerLog[Req2.prevIndex] # Req2.prevTerm
  /\ messages' = (messages \ {Req2}) \cup {Fail2a, Fail2b}
  /\ stage' = 5
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit, leaderNext, leaderMatch, lastRequestId>>

HandleOneCopyOfFail2 ==
  /\ stage = 5
  /\ Fail2a \in messages
  /\ Fail2a.requestId = lastRequestId
  /\ leaderNext' = IF leaderNext > 1 THEN leaderNext - 1 ELSE 1
  /\ messages' = messages \ {Fail2a}
  /\ stage' = 6
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit, leaderMatch, lastRequestId>>

Send3 ==
  /\ stage = 6
  /\ leaderNext = 2
  /\ messages' = messages \cup {Req3}
  /\ lastRequestId' = 3
  /\ stage' = 7
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit, leaderNext, leaderMatch>>

Accept3 ==
  /\ stage = 7
  /\ Req3 \in messages
  /\ Req3.prevIndex >= followerCommit
  /\ followerLog[Req3.prevIndex] = Req3.prevTerm
  /\ LET conflicts ==
       {k \in 1..Len(Req3.entries) :
          Req3.prevIndex + k <= Len(followerLog) /\
          followerLog[Req3.prevIndex + k] # Req3.entries[k]}
     IN
       /\ conflicts # {}
       /\ followerLog' =
          SubSeq(followerLog, 1, Req3.prevIndex + MinSet(conflicts) - 1)
          \o SubSeq(Req3.entries, MinSet(conflicts), Len(Req3.entries))
  /\ messages' = (messages \ {Req3}) \cup {Success3}
  /\ stage' = 8
  /\ UNCHANGED <<leaderLog, followerCommit, leaderNext, leaderMatch, lastRequestId>>

HandleSuccess3 ==
  /\ stage = 8
  /\ Success3 \in messages
  /\ leaderMatch' = IF Success3.matchIndex > leaderMatch
                       THEN Success3.matchIndex ELSE leaderMatch
  /\ leaderNext' = IF Success3.matchIndex + 1 > leaderNext
                      THEN Success3.matchIndex + 1 ELSE leaderNext
  /\ messages' = messages \ {Success3}
  /\ stage' = 9
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit, lastRequestId>>

DropStaleDuplicate ==
  /\ stage = 9
  /\ Fail2b \in messages
  /\ Fail2b.requestId # lastRequestId
  /\ messages' = messages \ {Fail2b}
  /\ stage' = 10
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit, leaderNext, leaderMatch, lastRequestId>>

Next == Send1 \/ Reject1 \/ HandleFail1 \/ Send2 \/ Reject2 \/
        HandleOneCopyOfFail2 \/ Send3 \/ Accept3 \/ HandleSuccess3 \/
        DropStaleDuplicate

TypeOK ==
  /\ stage \in 0..10
  /\ leaderLog = <<1,1,2,3>>
  /\ followerLog \in {<<1,9,9,8>>, <<1,1,2,3>>}
  /\ followerCommit = 1
  /\ messages \subseteq {Req1,Fail1,Req2,Fail2a,Fail2b,Req3,Success3}
  /\ leaderNext \in 1..5
  /\ leaderMatch \in 0..4
  /\ lastRequestId \in 0..3

CommittedPrefixPreserved ==
  followerLog[1] = leaderLog[1]

CursorInvariant ==
  leaderNext >= leaderMatch + 1

EachFailureMovesOneStep ==
  stage \in 3..6 => leaderNext \in {3,2}

TwoDistinctConflicts ==
  stage >= 1 => Req1.prevIndex # Req2.prevIndex

RetryUsesCurrentCursor ==
  stage \in 7..8 => Req3.prevIndex = leaderNext - 1

ConflictSuffixRepaired ==
  stage >= 8 => followerLog = leaderLog

SuccessAdvancesLeader ==
  stage >= 9 => leaderMatch = 4 /\ leaderNext = 5

StaleDuplicateCannotRegress ==
  stage = 10 => leaderNext = 5 /\ leaderMatch = 4

CommitBound ==
  followerCommit <= Len(followerLog)

Spec == Init /\ [][Next]_vars
====
