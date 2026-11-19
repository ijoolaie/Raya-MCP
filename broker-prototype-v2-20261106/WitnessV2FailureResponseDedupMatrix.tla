---- MODULE WitnessV2FailureResponseDedupMatrix ----
EXTENDS Naturals, FiniteSets, TLC

CONSTANTS ClampFailures, DeduplicateFailures

VARIABLES nextIndex, matchIndex, lastRequestId, pending, handledFailureIds
vars == <<nextIndex, matchIndex, lastRequestId, pending, handledFailureIds>>

Copies == {1, 2}
Kinds == {"SUCCESS", "FAILURE"}

Init ==
  /\ nextIndex = 3
  /\ matchIndex = 1
  /\ lastRequestId = 2
  /\ handledFailureIds = {}
  /\ pending =
       {[kind |-> "FAILURE", requestId |-> 2, matchIndex |-> 0, copyId |-> c]
          : c \in Copies}
       \cup
       {[kind |-> "SUCCESS", requestId |-> 1, matchIndex |-> 2, copyId |-> c]
          : c \in Copies}

HandleResponse(m) ==
  /\ m \in pending
  /\ IF m.kind = "SUCCESS"
        THEN
          /\ matchIndex' = IF m.matchIndex > matchIndex
                            THEN m.matchIndex ELSE matchIndex
          /\ nextIndex' = IF m.matchIndex + 1 > nextIndex
                            THEN m.matchIndex + 1 ELSE nextIndex
          /\ UNCHANGED handledFailureIds
        ELSE
          /\ m.requestId = lastRequestId
          /\ (IF DeduplicateFailures
                THEN m.requestId \notin handledFailureIds
                ELSE TRUE)
          /\ nextIndex' =
               IF ClampFailures
               THEN IF nextIndex - 1 > matchIndex + 1
                    THEN nextIndex - 1 ELSE matchIndex + 1
               ELSE IF nextIndex > 1 THEN nextIndex - 1 ELSE 1
          /\ UNCHANGED matchIndex
          /\ handledFailureIds' = handledFailureIds \cup {m.requestId}
  /\ pending' = pending \ {m}
  /\ UNCHANGED lastRequestId

DiscardDuplicateFailure(m) ==
  /\ DeduplicateFailures
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ m.requestId \in handledFailureIds
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<nextIndex, matchIndex, lastRequestId, handledFailureIds>>

StartNextRequest ==
  /\ lastRequestId = 2
  /\ ~\E m \in pending : m.kind = "FAILURE"
  /\ lastRequestId' = 3
  /\ pending' = pending \cup
       {[kind |-> "FAILURE", requestId |-> 3, matchIndex |-> 0, copyId |-> 1]}
  /\ UNCHANGED <<nextIndex, matchIndex, handledFailureIds>>

Next ==
  \/ \E m \in pending : HandleResponse(m)
  \/ \E m \in pending : DiscardDuplicateFailure(m)
  \/ StartNextRequest

Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(Next)

TypeOK ==
  /\ nextIndex \in 1..4
  /\ matchIndex \in 0..3
  /\ lastRequestId \in 2..3
  /\ pending \subseteq
       [kind : Kinds, requestId : 1..3, matchIndex : 0..3, copyId : Copies]
  /\ handledFailureIds \subseteq 1..3

CursorLowerBound == nextIndex >= matchIndex + 1
EventuallyHandlesRequest3 == <> (3 \in handledFailureIds)

====