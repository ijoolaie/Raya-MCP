---- MODULE WitnessV2ThreeInflightMixedCursor ----
EXTENDS Naturals, FiniteSets, TLC
CONSTANT ClampFailures
ReqIds == {1, 2, 3}
VARIABLES nextIndex, matchIndex, lastRequestId, inFlight, pending,
          handledFailures, acceptedSuccesses
vars == <<nextIndex, matchIndex, lastRequestId, inFlight, pending,
          handledFailures, acceptedSuccesses>>
Request(r, prev) == [requestId |-> r, prevIndex |-> prev]
Success(r, mi, c) == [kind |-> "SUCCESS", requestId |-> r,
                       matchIndex |-> mi, copyId |-> c]
Failure(r, c) == [kind |-> "FAILURE", requestId |-> r,
                   matchIndex |-> 0, copyId |-> c]
Init ==
  /\ nextIndex = 5
  /\ matchIndex = 1
  /\ lastRequestId = 3
  /\ inFlight = {Request(1, 1), Request(2, 2), Request(3, 3)}
  /\ pending = {Success(1, 2, 1), Success(2, 3, 2), Success(3, 4, 3),
                 Failure(1, 4), Failure(2, 5), Failure(3, 6)}
  /\ handledFailures = {}
  /\ acceptedSuccesses = {}
Sent(r) == \E q \in inFlight : q.requestId = r
HandleSuccess(m) ==
  /\ m \in pending
  /\ m.kind = "SUCCESS"
  /\ Sent(m.requestId)
  /\ matchIndex' = IF m.matchIndex > matchIndex THEN m.matchIndex ELSE matchIndex
  /\ nextIndex' = IF m.matchIndex + 1 > nextIndex THEN m.matchIndex + 1 ELSE nextIndex
  /\ acceptedSuccesses' = acceptedSuccesses \cup {m.requestId}
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<lastRequestId, inFlight, handledFailures>>
HandleCurrentFailure(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ Sent(m.requestId)
  /\ m.requestId = lastRequestId
  /\ m.requestId \notin handledFailures
  /\ IF ClampFailures
       THEN nextIndex' = IF nextIndex - 1 > matchIndex + 1
                          THEN nextIndex - 1 ELSE matchIndex + 1
       ELSE nextIndex' = nextIndex - 1
  /\ handledFailures' = handledFailures \cup {m.requestId}
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<lastRequestId, inFlight, matchIndex, acceptedSuccesses>>
DiscardStaleFailure(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ m.requestId # lastRequestId
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<nextIndex, matchIndex, lastRequestId, inFlight,
                 handledFailures, acceptedSuccesses>>
DiscardDuplicateFailure(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ m.requestId = lastRequestId
  /\ m.requestId \in handledFailures
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<nextIndex, matchIndex, lastRequestId, inFlight,
                 handledFailures, acceptedSuccesses>>
Next ==
  \/ \E m \in pending : HandleSuccess(m)
  \/ \E m \in pending : HandleCurrentFailure(m)
  \/ \E m \in pending : DiscardStaleFailure(m)
  \/ \E m \in pending : DiscardDuplicateFailure(m)
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
TypeOK ==
  /\ nextIndex \in 1..6
  /\ matchIndex \in 0..4
  /\ lastRequestId = 3
  /\ inFlight \subseteq [requestId : ReqIds, prevIndex : 1..3]
  /\ pending \subseteq [kind : {"SUCCESS","FAILURE"},
       requestId : ReqIds, matchIndex : 0..4, copyId : 1..6]
  /\ handledFailures \subseteq ReqIds
  /\ acceptedSuccesses \subseteq ReqIds
CursorLowerBound == nextIndex >= matchIndex + 1
EventuallyLatestFailureHandled == <> (3 \in handledFailures)
EventuallyAllResponsesDrained == <> (pending = {})
====
