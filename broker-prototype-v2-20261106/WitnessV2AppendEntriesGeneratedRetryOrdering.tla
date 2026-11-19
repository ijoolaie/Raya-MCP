---- MODULE WitnessV2AppendEntriesGeneratedRetryOrdering ----
EXTENDS Naturals, Sequences, FiniteSets, TLC

VARIABLES stage, leaderLog, followerLog, followerCommit,
          messages, leaderNext, leaderMatch, lastRequestId
vars == <<stage, leaderLog, followerLog, followerCommit,
          messages, leaderNext, leaderMatch, lastRequestId>>

LeaderLog == <<1, 1, 2, 3, 4>>
InitialFollowerLog == <<1, 9, 9, 8, 7>>

MinSet(S) == CHOOSE x \in S : \A y \in S : x <= y

PrevMatches(localLog, prevIndex, prevTerm) ==
  IF prevIndex = 0
  THEN prevTerm = 0
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
               SubSeq(entries, Len(localLog) - prevIndex + 1,
                      Len(entries))
          ELSE localLog


Req1 == [kind |-> "APPEND", requestId |-> 1, prevIndex |-> 4,
         prevTerm |-> LeaderLog[4], entries |-> <<4>>, leaderCommit |-> 1]
Fail1 == [kind |-> "APPEND_RESP", mid |-> 11, requestId |-> 1,
          success |-> FALSE, matchIndex |-> 0, term |-> 1]
Req2 == [kind |-> "APPEND", requestId |-> 2, prevIndex |-> 3,
         prevTerm |-> LeaderLog[3], entries |-> <<3, 4>>, leaderCommit |-> 1]
Fail2a == [kind |-> "APPEND_RESP", mid |-> 21, requestId |-> 2,
           success |-> FALSE, matchIndex |-> 0, term |-> 1]
Fail2b == [kind |-> "APPEND_RESP", mid |-> 22, requestId |-> 2,
           success |-> FALSE, matchIndex |-> 0, term |-> 1]
Req3 == [kind |-> "APPEND", requestId |-> 3, prevIndex |-> 2,
         prevTerm |-> LeaderLog[2], entries |-> <<2, 3, 4>>, leaderCommit |-> 1]
Fail3 == [kind |-> "APPEND_RESP", mid |-> 31, requestId |-> 3,
          success |-> FALSE, matchIndex |-> 0, term |-> 1]
Req4 == [kind |-> "APPEND", requestId |-> 4, prevIndex |-> 1,
         prevTerm |-> LeaderLog[1], entries |-> <<1, 2, 3, 4>>, leaderCommit |-> 1]
Success4 == [kind |-> "APPEND_RESP", mid |-> 41, requestId |-> 4,
             success |-> TRUE, matchIndex |-> 5, term |-> 1]

Init ==
  /\ stage = 0
  /\ leaderLog = LeaderLog
  /\ followerLog = InitialFollowerLog
  /\ followerCommit = 1
  /\ messages = {}
  /\ leaderNext = 5
  /\ leaderMatch = 1
  /\ lastRequestId = 0

Send1 ==
  /\ stage = 0
  /\ leaderNext = 5
  /\ Req1.prevIndex = leaderNext - 1
  /\ Req1.entries = SubSeq(leaderLog, leaderNext, Len(leaderLog))
  /\ messages' = {Req1}
  /\ lastRequestId' = 1
  /\ stage' = 1
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit, leaderNext, leaderMatch>>

Reject1 ==
  /\ stage = 1 /\ Req1 \in messages
  /\ ~PrevMatches(followerLog, Req1.prevIndex, Req1.prevTerm)
  /\ messages' = (messages \ {Req1}) \cup {Fail1}
  /\ stage' = 2
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit, leaderNext, leaderMatch, lastRequestId>>

HandleFail1 ==
  /\ stage = 2 /\ Fail1 \in messages
  /\ Fail1.requestId = lastRequestId
  /\ leaderNext' = IF leaderNext - 1 > leaderMatch + 1
                    THEN leaderNext - 1 ELSE leaderMatch + 1
  /\ messages' = messages \ {Fail1}
  /\ stage' = 3
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit, leaderMatch, lastRequestId>>

Send2 ==
  /\ stage = 3 /\ leaderNext = 4
  /\ Req2.prevIndex = leaderNext - 1
  /\ Req2.entries = SubSeq(leaderLog, leaderNext, Len(leaderLog))
  /\ messages' = messages \cup {Req2}
  /\ lastRequestId' = 2
  /\ stage' = 4
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit, leaderNext, leaderMatch>>

Reject2 ==
  /\ stage = 4 /\ Req2 \in messages
  /\ ~PrevMatches(followerLog, Req2.prevIndex, Req2.prevTerm)
  /\ messages' = (messages \ {Req2}) \cup {Fail2a, Fail2b}
  /\ stage' = 5
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit, leaderNext, leaderMatch, lastRequestId>>

HandleFail2a ==
  /\ stage = 5 /\ Fail2a \in messages
  /\ Fail2a.requestId = lastRequestId
  /\ leaderNext' = IF leaderNext - 1 > leaderMatch + 1
                    THEN leaderNext - 1 ELSE leaderMatch + 1
  /\ messages' = messages \ {Fail2a}
  /\ stage' = 6
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit, leaderMatch, lastRequestId>>

Send3 ==
  /\ stage = 6 /\ leaderNext = 3
  /\ Req3.prevIndex = leaderNext - 1
  /\ Req3.entries = SubSeq(leaderLog, leaderNext, Len(leaderLog))
  /\ messages' = messages \cup {Req3}
  /\ lastRequestId' = 3
  /\ stage' = 7
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit, leaderNext, leaderMatch>>

Reject3 ==
  /\ stage = 7 /\ Req3 \in messages
  /\ ~PrevMatches(followerLog, Req3.prevIndex, Req3.prevTerm)
  /\ messages' = (messages \ {Req3}) \cup {Fail3}
  /\ stage' = 8
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit, leaderNext, leaderMatch, lastRequestId>>

HandleFail3 ==
  /\ stage = 8 /\ Fail3 \in messages
  /\ Fail3.requestId = lastRequestId
  /\ leaderNext' = IF leaderNext - 1 > leaderMatch + 1
                    THEN leaderNext - 1 ELSE leaderMatch + 1
  /\ messages' = messages \ {Fail3}
  /\ stage' = 9
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit, leaderMatch, lastRequestId>>

Send4 ==
  /\ stage = 9 /\ leaderNext = 2
  /\ Req4.prevIndex = leaderNext - 1
  /\ Req4.entries = SubSeq(leaderLog, leaderNext, Len(leaderLog))
  /\ messages' = messages \cup {Req4}
  /\ lastRequestId' = 4
  /\ stage' = 10
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit, leaderNext, leaderMatch>>

Accept4 ==
  /\ stage = 10 /\ Req4 \in messages
  /\ Req4.prevIndex >= followerCommit
  /\ Req4.prevIndex <= Len(followerLog)
  /\ PrevMatches(followerLog, Req4.prevIndex, Req4.prevTerm)
  /\ followerLog' = AppendResult(followerLog, Req4.prevIndex, Req4.entries)
  /\ messages' = (messages \ {Req4}) \cup {Success4}
  /\ stage' = 11
  /\ UNCHANGED <<leaderLog, followerCommit, leaderNext, leaderMatch, lastRequestId>>

HandleSuccess4 ==
  /\ stage = 11 /\ Success4 \in messages
  /\ leaderMatch' = IF Success4.matchIndex > leaderMatch
                     THEN Success4.matchIndex ELSE leaderMatch
  /\ leaderNext' = IF Success4.matchIndex + 1 > leaderNext
                    THEN Success4.matchIndex + 1 ELSE leaderNext
  /\ messages' = messages \ {Success4}
  /\ stage' = 12
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit, lastRequestId>>

HandleDelayedDuplicate ==
  /\ stage \in {11, 12} /\ Fail2b \in messages
  /\ Fail2b.requestId # lastRequestId
  /\ messages' = messages \ {Fail2b}
  /\ stage' = stage
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit, leaderNext, leaderMatch, lastRequestId>>

Next == Send1 \/ Reject1 \/ HandleFail1 \/ Send2 \/ Reject2 \/
        HandleFail2a \/ Send3 \/ Reject3 \/ HandleFail3 \/ Send4 \/
        Accept4 \/ HandleSuccess4 \/ HandleDelayedDuplicate

TypeOK ==
  /\ stage \in 0..12
  /\ leaderLog = LeaderLog
  /\ followerLog \in {InitialFollowerLog, LeaderLog}
  /\ followerCommit = 1
  /\ messages \subseteq {Req1, Fail1, Req2, Fail2a, Fail2b,
                         Req3, Fail3, Req4, Success4}
  /\ leaderNext \in 1..6
  /\ leaderMatch \in 0..5
  /\ lastRequestId \in 0..4

CommittedPrefixPreserved ==
  followerCommit = 1 => followerLog[1] = leaderLog[1]

CursorLowerBound == leaderNext >= leaderMatch + 1
FollowerCommitBound == followerCommit <= Len(followerLog)
FollowerRepairAtSuccess == stage >= 11 => followerLog = leaderLog
RequestCursorConsistency ==
  /\ (stage \in 0..2 => leaderNext = 5)
  /\ (stage \in 3..5 => leaderNext = 4)
  /\ (stage \in 6..8 => leaderNext = 3)
  /\ (stage \in 9..11 => leaderNext = 2)
  /\ (stage = 12 => leaderNext = 6)
RequestIdsMonotonic ==
  /\ (stage = 0 => lastRequestId = 0)
  /\ (stage = 1 => lastRequestId = 1)
  /\ (stage \in 2..3 => lastRequestId = 1)
  /\ (stage \in 4..5 => lastRequestId = 2)
  /\ (stage = 6 => lastRequestId = 2)
  /\ (stage = 7 => lastRequestId = 3)
  /\ (stage \in 8..9 => lastRequestId = 3)
  /\ (stage \in 10..11 => lastRequestId = 4)
  /\ (stage = 12 => lastRequestId = 4)
DelayedDuplicateStale ==
  stage \in {11, 12} /\ Fail2b \in messages => Fail2b.requestId # lastRequestId
SuccessCursor ==
  stage = 12 => leaderMatch = 5 /\ leaderNext = 6

Spec == Init /\ [][Next]_vars
====