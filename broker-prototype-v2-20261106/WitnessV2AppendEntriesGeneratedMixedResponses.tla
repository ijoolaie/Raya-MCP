---- MODULE WitnessV2AppendEntriesGeneratedMixedResponses ----
EXTENDS Naturals, Sequences, FiniteSets, TLC

CONSTANT UseGuard

VARIABLES stage, leaderLog, followerLog, followerCommit,
          leaderNext, leaderMatch, lastRequestId, messages
vars == <<stage, leaderLog, followerLog, followerCommit,
          leaderNext, leaderMatch, lastRequestId, messages>>

LeaderLog == <<1, 1, 2, 3, 4>>
InitialFollowerLog == <<1, 9, 9, 8>>

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

Req1 == [kind |-> "APPEND", requestId |-> 1, term |-> 1,
         prevIndex |-> 1, prevTerm |-> LeaderLog[1],
         entries |-> SubSeq(LeaderLog, 2, Len(LeaderLog)),
         leaderCommit |-> 2]
Req2 == [kind |-> "APPEND", requestId |-> 2, term |-> 1,
         prevIndex |-> 1, prevTerm |-> LeaderLog[1],
         entries |-> SubSeq(LeaderLog, 2, Len(LeaderLog)),
         leaderCommit |-> 2]
Success1 == [kind |-> "APPEND_RESP", mid |-> 11, requestId |-> 1,
             term |-> 1, success |-> TRUE, matchIndex |-> 5]
Failure2 == [kind |-> "APPEND_RESP", mid |-> 22, requestId |-> 2,
             term |-> 1, success |-> FALSE, matchIndex |-> 0]

Init ==
  /\ stage = 0
  /\ leaderLog = LeaderLog
  /\ followerLog = InitialFollowerLog
  /\ followerCommit = 1
  /\ leaderNext = 2
  /\ leaderMatch = 1
  /\ lastRequestId = 0
  /\ messages = {}

Send1 ==
  /\ stage = 0
  /\ Req1.prevIndex = leaderNext - 1
  /\ Req1.prevTerm = leaderLog[Req1.prevIndex]
  /\ Req1.entries = SubSeq(leaderLog, leaderNext, Len(leaderLog))
  /\ messages' = {Req1}
  /\ lastRequestId' = 1
  /\ stage' = 1
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit,
                 leaderNext, leaderMatch>>

Send2 ==
  /\ stage = 1
  /\ leaderNext = 2
  /\ Req2.prevIndex = leaderNext - 1
  /\ Req2.prevTerm = leaderLog[Req2.prevIndex]
  /\ Req2.entries = SubSeq(leaderLog, leaderNext, Len(leaderLog))
  /\ Req1 \in messages
  /\ messages' = messages \cup {Req2}
  /\ lastRequestId' = 2
  /\ stage' = 2
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit,
                 leaderNext, leaderMatch>>

Deliver1 ==
  /\ stage = 2
  /\ Req1 \in messages
  /\ Req1.term >= 1
  /\ PrevMatches(followerLog, Req1.prevIndex, Req1.prevTerm)
  /\ Req1.prevIndex >= followerCommit
  /\ followerLog' = AppendResult(followerLog, Req1.prevIndex, Req1.entries)
  /\ followerCommit' = IF Req1.leaderCommit > followerCommit
                      THEN IF Req1.leaderCommit < Len(followerLog')
                           THEN Req1.leaderCommit ELSE Len(followerLog')
                      ELSE followerCommit
  /\ messages' = (messages \ {Req1}) \cup {Success1}
  /\ stage' = 3
  /\ UNCHANGED <<leaderLog, leaderNext, leaderMatch, lastRequestId>>

Reject2 ==
  /\ stage = 3
  /\ Req2 \in messages
  /\ Req2.term >= 1
  /\ (Req2.term < 1
      \/ ~PrevMatches(followerLog, Req2.prevIndex, Req2.prevTerm)
      \/ Req2.prevIndex < followerCommit)
  /\ messages' = (messages \ {Req2}) \cup {Failure2}
  /\ stage' = 4
  /\ UNCHANGED <<leaderLog, followerLog, followerCommit,
                 leaderNext, leaderMatch, lastRequestId>>

HandleSuccess ==
  /\ stage = 4
  /\ Success1 \in messages
  /\ Success1.term = 1
  /\ leaderMatch' = IF Success1.matchIndex > leaderMatch
                    THEN Success1.matchIndex ELSE leaderMatch
  /\ leaderNext' = IF Success1.matchIndex + 1 > leaderNext
                   THEN Success1.matchIndex + 1 ELSE leaderNext
  /\ messages' = messages \ {Success1}
  /\ UNCHANGED <<stage, leaderLog, followerLog, followerCommit,
                 lastRequestId>>

HandleFailure ==
  /\ stage = 4
  /\ Failure2 \in messages
  /\ Failure2.term = 1
  /\ Failure2.requestId = lastRequestId
  /\ leaderNext' =
       IF UseGuard
       THEN IF leaderNext - 1 > leaderMatch + 1
            THEN leaderNext - 1 ELSE leaderMatch + 1
       ELSE IF leaderNext > 1 THEN leaderNext - 1 ELSE 1
  /\ messages' = messages \ {Failure2}
  /\ UNCHANGED <<stage, leaderLog, followerLog, followerCommit,
                 leaderMatch, lastRequestId>>

Next == Send1 \/ Send2 \/ Deliver1 \/ Reject2 \/
        HandleSuccess \/ HandleFailure

TypeOK ==
  /\ stage \in 0..4
  /\ leaderLog = LeaderLog
  /\ followerLog \in {InitialFollowerLog, LeaderLog}
  /\ followerCommit \in 1..2
  /\ followerCommit <= Len(followerLog)
  /\ leaderNext \in 1..6
  /\ leaderMatch \in 0..5
  /\ lastRequestId \in 0..2
  /\ messages \subseteq {Req1, Req2, Success1, Failure2}

CommittedPrefixPreserved == followerLog[1] = leaderLog[1]
FollowerCommitBound == followerCommit <= Len(followerLog)
CursorLowerBound == leaderNext >= leaderMatch + 1
MatchWithinLeaderLog == leaderMatch <= Len(leaderLog)
NextWithinLeaderLog == leaderNext <= Len(leaderLog) + 1
RequestsUseSameBaseCursor ==
  stage \in 1..2 => leaderNext = 2
FollowerAcceptedFirst == stage >= 3 => followerLog = LeaderLog
FollowerCommitAdvanced == stage >= 3 => followerCommit = 2
BothResponsesGenerated == stage = 4 =>
  lastRequestId = 2 /\ followerCommit = 2
AllResponsesHandled == stage = 4 /\ messages = {} =>
  leaderMatch = 5 /\ leaderNext = 6

Spec == Init /\ [][Next]_vars
====
