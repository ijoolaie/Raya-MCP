---- MODULE WitnessV2ExactDuplicateResponseCursor ----
EXTENDS Naturals, FiniteSets, TLC

CONSTANT ClampFailures

VARIABLES nextIndex, matchIndex, lastRequestId, pending
vars == <<nextIndex, matchIndex, lastRequestId, pending>>

ResponseKinds == {"SUCCESS", "FAILURE"}
Copies == {1, 2}

Init ==
  /\ nextIndex = 3
  /\ matchIndex = 1
  /\ lastRequestId = 2
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
        ELSE
          /\ m.requestId = lastRequestId
          /\ nextIndex' =
               IF ClampFailures
               THEN IF nextIndex - 1 > matchIndex + 1
                    THEN nextIndex - 1 ELSE matchIndex + 1
               ELSE IF nextIndex > 1 THEN nextIndex - 1 ELSE 1
          /\ UNCHANGED matchIndex
  /\ pending' = pending \ {m}
  /\ UNCHANGED lastRequestId

Next ==
  \E m \in pending : HandleResponse(m)

Spec == Init /\ [][Next]_vars

TypeOK ==
  /\ nextIndex \in 1..4
  /\ matchIndex \in 0..3
  /\ lastRequestId = 2
  /\ pending \subseteq
       [kind : ResponseKinds, requestId : 1..2,
        matchIndex : 0..3, copyId : Copies]

CursorLowerBound == nextIndex >= matchIndex + 1

====
