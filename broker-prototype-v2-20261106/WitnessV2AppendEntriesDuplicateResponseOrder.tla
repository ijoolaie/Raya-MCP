---- MODULE WitnessV2AppendEntriesDuplicateResponseOrder ----
EXTENDS Naturals, FiniteSets, Sequences, TLC

CONSTANTS Order, GuardFailure
ASSUME Order \in {"Either", "FailureFirst"}
ASSUME GuardFailure \in BOOLEAN

Leader == "n1"
Follower == "n2"

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

SendRequest1 ==
  /\ stage = 0
  /\ messages' = {[kind |-> "APPEND", requestId |-> 1,
                    prevIndex |-> 0, prevTerm |-> 0,
                    entries |-> <<1, 2>>, leaderCommit |-> 1]}
  /\ lastRequestId' = 1
  /\ stage' = 1
  /\ UNCHANGED <<followerLog, followerCommit, nextIndex, matchIndex, handled>>

SendRequest2 ==
  /\ stage = 1
  /\ messages' = messages \cup
       {[kind |-> "APPEND", requestId |-> 2,
         prevIndex |-> 0, prevTerm |-> 0,
         entries |-> <<1, 2>>, leaderCommit |-> 1]}
  /\ lastRequestId' = 2
  /\ stage' = 2
  /\ UNCHANGED <<followerLog, followerCommit, nextIndex, matchIndex, handled>>

DeliverRequest1 ==
  /\ stage = 2
  /\ followerLog = <<>>
  /\ followerCommit = 0
  /\ followerLog' = <<1, 2>>
  /\ followerCommit' = 1
  /\ messages' = (messages \ {[kind |-> "APPEND", requestId |-> 1,
                                  prevIndex |-> 0, prevTerm |-> 0,
                                  entries |-> <<1, 2>>, leaderCommit |-> 1]})
       \cup {[kind |-> "APPEND_RESP", requestId |-> 1,
               success |-> TRUE, matchIndex |-> 2, term |-> 1]}
  /\ stage' = 3
  /\ UNCHANGED <<nextIndex, matchIndex, lastRequestId, handled>>

RejectRequest2 ==
  /\ stage = 3
  /\ followerCommit = 1
  /\ 0 < followerCommit
  /\ messages' = (messages \ {[kind |-> "APPEND", requestId |-> 2,
                                  prevIndex |-> 0, prevTerm |-> 0,
                                  entries |-> <<1, 2>>, leaderCommit |-> 1]})
       \cup {[kind |-> "APPEND_RESP", requestId |-> 2,
               success |-> FALSE, matchIndex |-> 0, term |-> 1]}
  /\ stage' = 4
  /\ UNCHANGED <<followerLog, followerCommit, nextIndex, matchIndex,
                 lastRequestId, handled>>

HandleSuccess(m) ==
  /\ stage = 4
  /\ m \in messages
  /\ m.kind = "APPEND_RESP"
  /\ m.success = TRUE
  /\ Order = "Either" \/ "failure" \in handled
  /\ matchIndex' = IF m.matchIndex > matchIndex THEN m.matchIndex ELSE matchIndex
  /\ nextIndex' = IF m.matchIndex + 1 > nextIndex THEN m.matchIndex + 1 ELSE nextIndex
  /\ messages' = messages \ {m}
  /\ handled' = handled \cup {"success"}
  /\ UNCHANGED <<stage, followerLog, followerCommit, lastRequestId>>

HandleFailure(m) ==
  /\ stage = 4
  /\ m \in messages
  /\ m.kind = "APPEND_RESP"
  /\ m.success = FALSE
  /\ m.requestId = lastRequestId
  /\ Order = "Either" \/ handled = {}
  /\ nextIndex' = IF GuardFailure
       THEN (IF nextIndex - 1 > matchIndex + 1 THEN nextIndex - 1 ELSE matchIndex + 1)
       ELSE (IF nextIndex > 1 THEN nextIndex - 1 ELSE 1)
  /\ matchIndex' = matchIndex
  /\ messages' = messages \ {m}
  /\ handled' = handled \cup {"failure"}
  /\ UNCHANGED <<stage, followerLog, followerCommit, lastRequestId>>

Next ==
  \/ SendRequest1
  \/ SendRequest2
  \/ DeliverRequest1
  \/ RejectRequest2
  \/ \E m \in messages : HandleSuccess(m)
  \/ \E m \in messages : HandleFailure(m)

TypeOK ==
  /\ stage \in 0..4
  /\ followerLog \in {<<>>, <<1, 2>>}
  /\ followerCommit \in 0..1
  /\ followerCommit <= Len(followerLog)
  /\ nextIndex \in 1..3
  /\ matchIndex \in 0..2
  /\ lastRequestId \in 0..2
  /\ handled \subseteq {"success", "failure"}
  /\ messages \subseteq
       ([kind : {"APPEND"}, requestId : 1..2,
         prevIndex : 0..0, prevTerm : 0..0,
         entries : {<<1, 2>>}, leaderCommit : 1..1]
        \cup
        [kind : {"APPEND_RESP"}, requestId : 1..2,
         success : BOOLEAN, matchIndex : 0..2, term : 1..1])

NextIndexNotBelowMatchIndex == nextIndex >= matchIndex + 1
MatchIndexWithinLeaderLog == matchIndex <= 2
NextIndexWithinLeaderLog == nextIndex <= 3
BothResponsesHandled == handled = {"success", "failure"}

Spec == Init /\ [][Next]_vars
====