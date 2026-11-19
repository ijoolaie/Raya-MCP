---- MODULE WitnessV2AppendEntriesRequestIdCollisionAcrossTerms ----
EXTENDS Naturals, FiniteSets, TLC

VARIABLES currentTerm, leaderRole, leaderNext, leaderMatch,
          lastRequestId, messages
vars == <<currentTerm, leaderRole, leaderNext, leaderMatch,
          lastRequestId, messages>>

OldFailure == [kind |-> "APPEND_RESP", term |-> 1,
               requestId |-> 2, success |-> FALSE, matchIndex |-> 0]
NewSuccess == [kind |-> "APPEND_RESP", term |-> 2,
               requestId |-> 2, success |-> TRUE, matchIndex |-> 5]

Init ==
  /\ currentTerm = 2
  /\ leaderRole = TRUE
  /\ leaderNext = 6
  /\ leaderMatch = 0
  /\ lastRequestId = 2
  /\ messages = {OldFailure, NewSuccess}

HandleSuccess(resp) ==
  /\ resp \in messages
  /\ resp.kind = "APPEND_RESP"
  /\ resp.success = TRUE
  /\ leaderRole
  /\ resp.term = currentTerm
  /\ leaderMatch' = IF resp.matchIndex > leaderMatch
                    THEN resp.matchIndex ELSE leaderMatch
  /\ leaderNext' = IF resp.matchIndex + 1 > leaderNext
                   THEN resp.matchIndex + 1 ELSE leaderNext
  /\ messages' = messages \ {resp}
  /\ UNCHANGED <<currentTerm, leaderRole, lastRequestId>>

HandleFailure(resp) ==
  /\ resp \in messages
  /\ resp.kind = "APPEND_RESP"
  /\ resp.success = FALSE
  /\ leaderRole
  /\ resp.term = currentTerm
  /\ resp.requestId = lastRequestId
  /\ leaderNext' = IF leaderNext - 1 > leaderMatch + 1
                   THEN leaderNext - 1 ELSE leaderMatch + 1
  /\ messages' = messages \ {resp}
  /\ UNCHANGED <<currentTerm, leaderRole, leaderMatch, lastRequestId>>

DropOldTerm(resp) ==
  /\ resp \in messages
  /\ resp.kind = "APPEND_RESP"
  /\ resp.term < currentTerm
  /\ messages' = messages \ {resp}
  /\ UNCHANGED <<currentTerm, leaderRole, leaderNext, leaderMatch, lastRequestId>>

Next ==
  \/ \E resp \in messages : HandleSuccess(resp)
  \/ \E resp \in messages : HandleFailure(resp)
  \/ \E resp \in messages : DropOldTerm(resp)

TypeOK ==
  /\ currentTerm = 2
  /\ leaderRole = TRUE
  /\ leaderNext \in 1..6
  /\ leaderMatch \in 0..5
  /\ lastRequestId = 2
  /\ messages \subseteq {OldFailure, NewSuccess}
CursorLowerBound == leaderNext >= leaderMatch + 1
NextWithinLeaderLog == leaderNext <= 6
MatchWithinLeaderLog == leaderMatch <= 5
OldTermFailureCannotDecrementCursor == leaderNext = 6
NewTermSuccessMayAdvanceMatch == leaderMatch \in {0, 5}

Spec == Init /\ [][Next]_vars
====