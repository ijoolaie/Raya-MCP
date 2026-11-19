---- MODULE WitnessV2AppendEntriesTermChangeStaleResponses ----
EXTENDS Naturals, Sequences, FiniteSets, TLC

VARIABLES followerLog, followerCommit, leaderNext, leaderMatch,
          lastRequestId, messages, accepted, rejected,
          currentTerm, leaderRole
vars == <<followerLog, followerCommit, leaderNext, leaderMatch,
          lastRequestId, messages, accepted, rejected,
          currentTerm, leaderRole>>

LeaderLog == <<1, 1, 2, 3, 4>>
InitialFollowerLog == <<1, 9, 9, 8>>

MinSet(S) == CHOOSE x \in S : \A y \in S : x <= y

PrevMatches(localLog, prevIndex, prevTerm) ==
  IF prevIndex = 0 THEN prevTerm = 0
  ELSE IF prevIndex <= Len(localLog)
       THEN localLog[prevIndex] = prevTerm ELSE FALSE

AppendResult(localLog, prevIndex, entries) ==
  LET conflicts == {k \in 1..Len(entries) :
    prevIndex + k <= Len(localLog) /\ localLog[prevIndex + k] # entries[k]}
  IN IF conflicts # {}
     THEN LET firstConflict == MinSet(conflicts)
          IN SubSeq(localLog, 1, prevIndex + firstConflict - 1) \o
             SubSeq(entries, firstConflict, Len(entries))
     ELSE IF prevIndex + Len(entries) > Len(localLog)
          THEN localLog \o SubSeq(entries, Len(localLog) - prevIndex + 1, Len(entries))
          ELSE localLog

Req1 == [kind |-> "APPEND", requestId |-> 1, term |-> 1,
         prevIndex |-> 1, prevTerm |-> LeaderLog[1],
         entries |-> SubSeq(LeaderLog, 2, Len(LeaderLog)), leaderCommit |-> 2]
Req2 == [kind |-> "APPEND", requestId |-> 2, term |-> 1,
         prevIndex |-> 1, prevTerm |-> LeaderLog[1],
         entries |-> SubSeq(LeaderLog, 2, Len(LeaderLog)), leaderCommit |-> 2]
ReqNew2 == [kind |-> "APPEND", requestId |-> 2, term |-> 2,
           prevIndex |-> 5, prevTerm |-> LeaderLog[5],
           entries |-> <<>>, leaderCommit |-> 2]
Requests == {Req1, Req2}

SuccessResp(req) ==
  [kind |-> "APPEND_RESP", requestId |-> req.requestId,
   term |-> req.term, success |-> TRUE, matchIndex |-> Len(LeaderLog)]
FailureResp(req) ==
  [kind |-> "APPEND_RESP", requestId |-> req.requestId,
   term |-> req.term, success |-> FALSE, matchIndex |-> 0]

Init ==
  /\ followerLog = InitialFollowerLog
  /\ followerCommit = 1
  /\ leaderNext = 2
  /\ leaderMatch = 1
  /\ lastRequestId = 2
  /\ messages = Requests
  /\ accepted = {}
  /\ rejected = {}
  /\ currentTerm = 1
  /\ leaderRole = TRUE

DeliverAppend(req) ==
  /\ req \in messages
  /\ req.kind = "APPEND"
  /\ req.term = currentTerm
  /\ PrevMatches(followerLog, req.prevIndex, req.prevTerm)
  /\ req.prevIndex >= followerCommit
  /\ followerLog' = AppendResult(followerLog, req.prevIndex, req.entries)
  /\ followerCommit' = IF req.leaderCommit > followerCommit
                       THEN IF req.leaderCommit < Len(followerLog')
                            THEN req.leaderCommit ELSE Len(followerLog')
                       ELSE followerCommit
  /\ messages' = (messages \ {req}) \cup {SuccessResp(req)}
  /\ accepted' = accepted \cup {req.requestId}
  /\ UNCHANGED <<leaderNext, leaderMatch, lastRequestId, rejected,
                 currentTerm, leaderRole>>

RejectAppend(req) ==
  /\ req \in messages
  /\ req.kind = "APPEND"
  /\ req.term = currentTerm
  /\ (~PrevMatches(followerLog, req.prevIndex, req.prevTerm)
      \/ req.prevIndex < followerCommit)
  /\ messages' = (messages \ {req}) \cup {FailureResp(req)}
  /\ rejected' = rejected \cup {req.requestId}
  /\ UNCHANGED <<followerLog, followerCommit, leaderNext, leaderMatch,
                 lastRequestId, accepted, currentTerm, leaderRole>>

HandleSuccess(resp) ==
  /\ resp \in messages
  /\ resp.kind = "APPEND_RESP" /\ resp.success = TRUE
  /\ leaderRole /\ resp.term = currentTerm
  /\ leaderMatch' = IF resp.matchIndex > leaderMatch THEN resp.matchIndex ELSE leaderMatch
  /\ leaderNext' = IF resp.matchIndex + 1 > leaderNext THEN resp.matchIndex + 1 ELSE leaderNext
  /\ messages' = messages \ {resp}
  /\ UNCHANGED <<followerLog, followerCommit, lastRequestId, accepted,
                 rejected, currentTerm, leaderRole>>

HandleCurrentFailure(resp) ==
  /\ resp \in messages
  /\ resp.kind = "APPEND_RESP" /\ resp.success = FALSE
  /\ leaderRole /\ resp.term = currentTerm
  /\ resp.requestId = lastRequestId
  /\ leaderNext' = IF leaderNext - 1 > leaderMatch + 1
                   THEN leaderNext - 1 ELSE leaderMatch + 1
  /\ messages' = messages \ {resp}
  /\ UNCHANGED <<followerLog, followerCommit, leaderMatch, lastRequestId,
                 accepted, rejected, currentTerm, leaderRole>>

AdvanceTerm ==
  /\ currentTerm = 1
  /\ accepted \cup rejected = {1, 2}
  /\ currentTerm' = 2
  /\ leaderRole' = FALSE
  /\ UNCHANGED <<followerLog, followerCommit, leaderNext, leaderMatch,
                 lastRequestId, messages, accepted, rejected>>

AssumeNewLeader ==
  /\ currentTerm = 2
  /\ ~leaderRole
  /\ leaderRole' = TRUE
  /\ leaderNext' = Len(LeaderLog) + 1
  /\ leaderMatch' = 0
  /\ lastRequestId' = 0
  /\ UNCHANGED <<followerLog, followerCommit, messages, accepted,
                 rejected, currentTerm>>

SendNewTermAppend ==
  /\ currentTerm = 2
  /\ leaderRole
  /\ lastRequestId = 0
  /\ ReqNew2 \notin messages
  /\ messages' = messages \cup {ReqNew2}
  /\ lastRequestId' = 2
  /\ UNCHANGED <<followerLog, followerCommit, leaderNext, leaderMatch,
                 accepted, rejected, currentTerm, leaderRole>>

DeliverNewTermAppend ==
  /\ ReqNew2 \in messages
  /\ currentTerm = 2
  /\ leaderRole
  /\ PrevMatches(followerLog, ReqNew2.prevIndex, ReqNew2.prevTerm)
  /\ ReqNew2.prevIndex >= followerCommit
  /\ followerLog' = AppendResult(followerLog, ReqNew2.prevIndex, ReqNew2.entries)
  /\ followerCommit' = followerCommit
  /\ messages' = (messages \ {ReqNew2}) \cup {SuccessResp(ReqNew2)}
  /\ UNCHANGED <<leaderNext, leaderMatch, lastRequestId, accepted, rejected,
                 currentTerm, leaderRole>>
DropOldTermResponse(resp) ==
  /\ resp \in messages
  /\ resp.kind = "APPEND_RESP"
  /\ resp.term < currentTerm
  /\ messages' = messages \ {resp}
  /\ UNCHANGED <<followerLog, followerCommit, leaderNext, leaderMatch,
                 lastRequestId, accepted, rejected, currentTerm, leaderRole>>

Next ==
  \/ \E req \in Requests : DeliverAppend(req)
  \/ \E req \in Requests : RejectAppend(req)
  \/ \E resp \in messages : HandleSuccess(resp)
  \/ \E resp \in messages : HandleCurrentFailure(resp)
  \/ AdvanceTerm
  \/ AssumeNewLeader
  \/ \E resp \in messages : DropOldTermResponse(resp)

TypeOK ==
  /\ followerLog \in {InitialFollowerLog, LeaderLog}
  /\ followerCommit \in 1..2 /\ followerCommit <= Len(followerLog)
  /\ leaderNext \in 1..6 /\ leaderMatch \in 0..5
  /\ lastRequestId \in 0..2
  /\ currentTerm \in {1, 2}
  /\ leaderRole \in BOOLEAN
  /\ messages \subseteq Requests \cup
       {SuccessResp(Req1), SuccessResp(Req2), FailureResp(Req1), FailureResp(Req2)}
  /\ accepted \subseteq {1, 2} /\ rejected \subseteq {1, 2}

CommittedPrefixPreserved == followerLog[1] = LeaderLog[1]
CursorLowerBound == leaderNext >= leaderMatch + 1
MatchWithinLeaderLog == leaderMatch <= Len(LeaderLog)
NextWithinLeaderLog == leaderNext <= Len(LeaderLog) + 1
FollowerCommitBound == followerCommit <= Len(followerLog)
OutcomeSetsDisjoint == accepted \cap rejected = {}
AtMostOneAcceptance == Cardinality(accepted) <= 1
AtMostOneRejection == Cardinality(rejected) <= 1
ExactlyOneAccepted == accepted \cup rejected = {1, 2} => Cardinality(accepted) = 1
ExactlyOneRejected == accepted \cup rejected = {1, 2} => Cardinality(rejected) = 1
FollowerStateAfterAcceptance == accepted # {} => followerLog = LeaderLog
CommitAfterAcceptance == accepted # {} => followerCommit = 2
NewTermLeaderCursorReset == currentTerm = 2 /\ leaderRole =>
  leaderNext = Len(LeaderLog) + 1 /\ leaderMatch = 0 /\ lastRequestId = 0
OldTermResponsesDoNotAdvanceNewLeader ==
  currentTerm = 2 /\ leaderRole => leaderNext = Len(LeaderLog) + 1 /\ leaderMatch = 0
AllResponsesHandled == accepted \cup rejected = {1, 2} /\ messages = {} =>
  (IF currentTerm = 1 THEN leaderMatch = 5 /\ leaderNext = 6
   ELSE IF leaderRole THEN leaderMatch = 0 /\ leaderNext = 6 ELSE TRUE)

Spec == Init /\ [][Next]_vars
====