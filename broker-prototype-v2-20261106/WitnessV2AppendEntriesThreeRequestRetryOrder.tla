---- MODULE WitnessV2AppendEntriesThreeRequestRetryOrder ----
EXTENDS Naturals, FiniteSets, Sequences, TLC

CONSTANTS Order, GuardFailure
ASSUME Order \in {"Either", "FailureFirst"}
ASSUME GuardFailure \in BOOLEAN

VARIABLES stage, followerLog, followerCommit, messages,
          nextIndex, matchIndex, lastRequestId, handled
vars == <<stage, followerLog, followerCommit, messages,
          nextIndex, matchIndex, lastRequestId, handled>>

Init ==
  /\ stage = 0
  /\ followerLog = <<>>
  /\ followerCommit = 0
  /\ messages = {}
  /\ nextIndex = 1
  /\ matchIndex = 0
  /\ lastRequestId = 0
  /\ handled = {}

AppendReq(rid) ==
  [kind |-> "APPEND", requestId |-> rid,
   prevIndex |-> 0, prevTerm |-> 0,
   entries |-> <<1, 2>>, leaderCommit |-> 1]

AppendResp(rid, ok, ack) ==
  [kind |-> "APPEND_RESP", requestId |-> rid,
   success |-> ok, matchIndex |-> ack, term |-> 1]

Send1 ==
  /\ stage = 0
  /\ messages' = {AppendReq(1)}
  /\ lastRequestId' = 1
  /\ stage' = 1
  /\ UNCHANGED <<followerLog, followerCommit, nextIndex, matchIndex, handled>>

Send2 ==
  /\ stage = 1
  /\ messages' = messages \cup {AppendReq(2)}
  /\ lastRequestId' = 2
  /\ stage' = 2
  /\ UNCHANGED <<followerLog, followerCommit, nextIndex, matchIndex, handled>>

SendRetry3 ==
  /\ stage = 2
  /\ messages' = messages \cup {AppendReq(3)}
  /\ lastRequestId' = 3
  /\ stage' = 3
  /\ UNCHANGED <<followerLog, followerCommit, nextIndex, matchIndex, handled>>

Accept1 ==
  /\ stage = 3
  /\ followerLog = <<>>
  /\ followerCommit = 0
  /\ followerLog' = <<1, 2>>
  /\ followerCommit' = 1
  /\ messages' = (messages \ {AppendReq(1)}) \cup {AppendResp(1, TRUE, 2)}
  /\ stage' = 4
  /\ UNCHANGED <<nextIndex, matchIndex, lastRequestId, handled>>

Reject2 ==
  /\ stage = 4
  /\ followerCommit = 1
  /\ messages' = (messages \ {AppendReq(2)}) \cup {AppendResp(2, FALSE, 0)}
  /\ stage' = 5
  /\ UNCHANGED <<followerLog, followerCommit, nextIndex, matchIndex, lastRequestId, handled>>

RejectRetry3 ==
  /\ stage = 5
  /\ followerCommit = 1
  /\ messages' = (messages \ {AppendReq(3)}) \cup {AppendResp(3, FALSE, 0)}
  /\ stage' = 6
  /\ UNCHANGED <<followerLog, followerCommit, nextIndex, matchIndex, lastRequestId, handled>>

HandleSuccess(m) ==
  /\ stage = 6
  /\ m \in messages
  /\ m = AppendResp(1, TRUE, 2)
  /\ Order = "Either" \/ "failure" \in handled
  /\ matchIndex' = IF m.matchIndex > matchIndex THEN m.matchIndex ELSE matchIndex
  /\ nextIndex' = IF m.matchIndex + 1 > nextIndex THEN m.matchIndex + 1 ELSE nextIndex
  /\ messages' = messages \ {m}
  /\ handled' = handled \cup {"success"}
  /\ UNCHANGED <<stage, followerLog, followerCommit, lastRequestId>>

HandleLatestFailure(m) ==
  /\ stage = 6
  /\ m \in messages
  /\ m = AppendResp(3, FALSE, 0)
  /\ Order = "Either" \/ "failure" \in handled
  /\ nextIndex' =
       IF GuardFailure
       THEN (IF nextIndex - 1 > matchIndex + 1 THEN nextIndex - 1 ELSE matchIndex + 1)
       ELSE (IF nextIndex > 1 THEN nextIndex - 1 ELSE 1)
  /\ matchIndex' = matchIndex
  /\ messages' = messages \ {m}
  /\ handled' = handled \cup {"failure"}
  /\ UNCHANGED <<stage, followerLog, followerCommit, lastRequestId>>

DiscardStaleFailure ==
  /\ stage = 6
  /\ AppendResp(2, FALSE, 0) \in messages
  /\ lastRequestId = 3
  /\ messages' = messages \ {AppendResp(2, FALSE, 0)}
  /\ UNCHANGED <<stage, followerLog, followerCommit, nextIndex, matchIndex, lastRequestId, handled>>

Next ==
  \/ Send1
  \/ Send2
  \/ SendRetry3
  \/ Accept1
  \/ Reject2
  \/ RejectRetry3
  \/ \E m \in messages : HandleSuccess(m)
  \/ \E m \in messages : HandleLatestFailure(m)
  \/ DiscardStaleFailure

TypeOK ==
  /\ stage \in 0..6
  /\ followerLog \in {<<>>, <<1, 2>>}
  /\ followerCommit \in 0..1
  /\ followerCommit <= Len(followerLog)
  /\ nextIndex \in 1..3
  /\ matchIndex \in 0..2
  /\ lastRequestId \in 0..3
  /\ handled \subseteq {"success", "failure"}
  /\ messages \subseteq
       {AppendReq(1), AppendReq(2), AppendReq(3),
        AppendResp(1, TRUE, 2), AppendResp(2, FALSE, 0),
        AppendResp(3, FALSE, 0)}

NextIndexNotBelowMatchIndex == nextIndex >= matchIndex + 1
MatchIndexWithinLeaderLog == matchIndex <= 2
NextIndexWithinLeaderLog == nextIndex <= 3
RetryRequestWasStale == lastRequestId = 3

Spec == Init /\ [][Next]_vars
====
