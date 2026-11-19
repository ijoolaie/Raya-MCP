---- MODULE WitnessV2AppendEntriesNondeterministicDeliveryOrder ----
EXTENDS Naturals, Sequences, FiniteSets, TLC

VARIABLES followerLog, followerCommit, leaderNext, leaderMatch,
          lastRequestId, messages, accepted, rejected
vars == <<followerLog, followerCommit, leaderNext, leaderMatch,
          lastRequestId, messages, accepted, rejected>>

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
Requests == {Req1, Req2}

SuccessResp(req) ==
  [kind |-> "APPEND_RESP", requestId |-> req.requestId,
   term |-> 1, success |-> TRUE, matchIndex |-> Len(LeaderLog)]
FailureResp(req) ==
  [kind |-> "APPEND_RESP", requestId |-> req.requestId,
   term |-> 1, success |-> FALSE, matchIndex |-> 0]

Init ==
  /\ followerLog = InitialFollowerLog
  /\ followerCommit = 1
  /\ leaderNext = 2
  /\ leaderMatch = 1
  /\ lastRequestId = 2
  /\ messages = Requests
  /\ accepted = {}
  /\ rejected = {}

DeliverAppend(req) ==
  /\ req \in messages
  /\ PrevMatches(followerLog, req.prevIndex, req.prevTerm)
  /\ req.prevIndex >= followerCommit
  /\ followerLog' = AppendResult(followerLog, req.prevIndex, req.entries)
  /\ followerCommit' = IF req.leaderCommit > followerCommit
                       THEN IF req.leaderCommit < Len(followerLog')
                            THEN req.leaderCommit ELSE Len(followerLog')
                       ELSE followerCommit
  /\ messages' = (messages \ {req}) \cup {SuccessResp(req)}
  /\ accepted' = accepted \cup {req.requestId}
  /\ UNCHANGED <<leaderNext, leaderMatch, lastRequestId, rejected>>

RejectAppend(req) ==
  /\ req \in messages
  /\ (req.term < 1
      \/ ~PrevMatches(followerLog, req.prevIndex, req.prevTerm)
      \/ req.prevIndex < followerCommit)
  /\ messages' = (messages \ {req}) \cup {FailureResp(req)}
  /\ rejected' = rejected \cup {req.requestId}
  /\ UNCHANGED <<followerLog, followerCommit, leaderNext, leaderMatch,
                 lastRequestId, accepted>>

HandleSuccess(resp) ==
  /\ resp \in messages
  /\ resp.kind = "APPEND_RESP"
  /\ resp.success = TRUE
  /\ resp.term = 1
  /\ leaderMatch' = IF resp.matchIndex > leaderMatch
                    THEN resp.matchIndex ELSE leaderMatch
  /\ leaderNext' = IF resp.matchIndex + 1 > leaderNext
                   THEN resp.matchIndex + 1 ELSE leaderNext
  /\ messages' = messages \ {resp}
  /\ UNCHANGED <<followerLog, followerCommit, lastRequestId,
                 accepted, rejected>>

HandleCurrentFailure(resp) ==
  /\ resp \in messages
  /\ resp.kind = "APPEND_RESP"
  /\ resp.success = FALSE
  /\ resp.term = 1
  /\ resp.requestId = lastRequestId
  /\ leaderNext' = IF leaderNext - 1 > leaderMatch + 1
                   THEN leaderNext - 1 ELSE leaderMatch + 1
  /\ messages' = messages \ {resp}
  /\ UNCHANGED <<followerLog, followerCommit, leaderMatch,
                 lastRequestId, accepted, rejected>>

DropStaleFailure(resp) ==
  /\ resp \in messages
  /\ resp.kind = "APPEND_RESP"
  /\ resp.success = FALSE
  /\ resp.term = 1
  /\ resp.requestId # lastRequestId
  /\ messages' = messages \ {resp}
  /\ UNCHANGED <<followerLog, followerCommit, leaderNext, leaderMatch,
                 lastRequestId, accepted, rejected>>

Next ==
  \/ \E req \in Requests : DeliverAppend(req)
  \/ \E req \in Requests : RejectAppend(req)
  \/ \E resp \in messages : HandleSuccess(resp)
  \/ \E resp \in messages : HandleCurrentFailure(resp)
  \/ \E resp \in messages : DropStaleFailure(resp)

TypeOK ==
  /\ followerLog \in {InitialFollowerLog, LeaderLog}
  /\ followerCommit \in 1..2
  /\ followerCommit <= Len(followerLog)
  /\ leaderNext \in 1..6
  /\ leaderMatch \in 0..5
  /\ lastRequestId = 2
  /\ messages \subseteq Requests \cup
       {SuccessResp(Req1), SuccessResp(Req2),
        FailureResp(Req1), FailureResp(Req2)}
  /\ accepted \subseteq {1, 2}
  /\ rejected \subseteq {1, 2}

CommittedPrefixPreserved == followerLog[1] = LeaderLog[1]
CursorLowerBound == leaderNext >= leaderMatch + 1
MatchWithinLeaderLog == leaderMatch <= Len(LeaderLog)
NextWithinLeaderLog == leaderNext <= Len(LeaderLog) + 1
FollowerCommitBound == followerCommit <= Len(followerLog)
OutcomeSetsDisjoint == accepted \cap rejected = {}
AtMostOneAcceptance == Cardinality(accepted) <= 1
AtMostOneRejection == Cardinality(rejected) <= 1
BothRequestsResolved == accepted \cup rejected = {1, 2}
ExactlyOneAccepted == BothRequestsResolved => Cardinality(accepted) = 1
ExactlyOneRejected == BothRequestsResolved => Cardinality(rejected) = 1
FollowerStateAfterAcceptance == accepted # {} => followerLog = LeaderLog
CommitAfterAcceptance == accepted # {} => followerCommit = 2
AllResponsesHandled ==
  BothRequestsResolved /\ messages = {} =>
    leaderMatch = 5 /\ leaderNext = 6

Spec == Init /\ [][Next]_vars
====
