---- MODULE WitnessV2PerFollowerCursorIsolation ----
EXTENDS Naturals, FiniteSets, TLC
CONSTANT RouteBySource
Peers == {"B","D"}
VARIABLES currentTerm, leader, lastRequestId, nextIndex, matchIndex,
          pending, handledKeys, badCrossPeerMutation, badOldContext
vars == <<currentTerm, leader, lastRequestId, nextIndex, matchIndex,
          pending, handledKeys, badCrossPeerMutation, badOldContext>>
Key(m) == [term |-> m.term, leader |-> m.dst, peer |-> m.src,
           requestId |-> m.requestId]
Target(m) == IF RouteBySource THEN m.src ELSE
             IF m.src = "B" THEN "D" ELSE "B"
OldSuccessB == [kind |-> "SUCCESS", term |-> 1, dst |-> "A",
  src |-> "B", requestId |-> 1, matchedThrough |-> 5, copyId |-> 1]
OldFailureD == [kind |-> "FAILURE", term |-> 1, dst |-> "A",
  src |-> "D", requestId |-> 1, matchedThrough |-> 0, copyId |-> 2]
SuccessB == [kind |-> "SUCCESS", term |-> 2, dst |-> "C",
  src |-> "B", requestId |-> 1, matchedThrough |-> 2, copyId |-> 3]
SuccessD == [kind |-> "SUCCESS", term |-> 2, dst |-> "C",
  src |-> "D", requestId |-> 1, matchedThrough |-> 4, copyId |-> 4]
FailureB == [kind |-> "FAILURE", term |-> 2, dst |-> "C",
  src |-> "B", requestId |-> 1, matchedThrough |-> 0, copyId |-> 5]
FailureD == [kind |-> "FAILURE", term |-> 2, dst |-> "C",
  src |-> "D", requestId |-> 1, matchedThrough |-> 0, copyId |-> 6]
Init ==
  /\ currentTerm = 1
  /\ leader = "A"
  /\ lastRequestId = 1
  /\ nextIndex = [p \in Peers |-> 5]
  /\ matchIndex = [p \in Peers |-> 1]
  /\ pending = {}
  /\ handledKeys = {}
  /\ badCrossPeerMutation = FALSE
  /\ badOldContext = FALSE
ChangeLeader ==
  /\ currentTerm = 1
  /\ currentTerm' = 2
  /\ leader' = "C"
  /\ lastRequestId' = 0
  /\ nextIndex' = [p \in Peers |-> 3]
  /\ matchIndex' = [p \in Peers |-> 0]
  /\ pending' = pending \cup {OldSuccessB, OldFailureD, SuccessB, SuccessD, FailureB, FailureD}
  /\ UNCHANGED <<handledKeys, badCrossPeerMutation, badOldContext>>
SendRequests ==
  /\ currentTerm = 2
  /\ leader = "C"
  /\ lastRequestId = 0
  /\ lastRequestId' = 1
  /\ UNCHANGED <<currentTerm, leader, nextIndex, matchIndex, pending,
                 handledKeys, badCrossPeerMutation, badOldContext>>
HandleSuccess(m) ==
  /\ m \in pending
  /\ m.kind = "SUCCESS"
  /\ m.term = currentTerm
  /\ m.dst = leader
  /\ m.src \in Peers
  /\ LET t == Target(m)
         newMatch == IF m.matchedThrough > matchIndex[t]
                     THEN m.matchedThrough ELSE matchIndex[t]
         newNext == IF m.matchedThrough + 1 > nextIndex[t]
                    THEN m.matchedThrough + 1 ELSE nextIndex[t]
     IN
       /\ matchIndex' = [matchIndex EXCEPT ![t] = newMatch]
       /\ nextIndex' = [nextIndex EXCEPT ![t] = newNext]
       /\ badCrossPeerMutation' =
            IF t = m.src THEN badCrossPeerMutation ELSE TRUE
  /\ badOldContext' = badOldContext
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, lastRequestId, handledKeys>>
HandleFailure(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ m.term = currentTerm
  /\ m.dst = leader
  /\ m.src \in Peers
  /\ m.requestId = lastRequestId
  /\ Key(m) \notin handledKeys
  /\ LET t == Target(m)
         newNext == IF nextIndex[t] - 1 > matchIndex[t] + 1
                    THEN nextIndex[t] - 1 ELSE matchIndex[t] + 1
     IN
       /\ nextIndex' = [nextIndex EXCEPT ![t] = newNext]
       /\ matchIndex' = matchIndex
       /\ handledKeys' = handledKeys \cup {Key(m)}
       /\ badCrossPeerMutation' =
            IF t = m.src THEN badCrossPeerMutation ELSE TRUE
  /\ badOldContext' = badOldContext
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, lastRequestId>>
DiscardIneligible(m) ==
  /\ m \in pending
  /\ (m.term # currentTerm \/ m.dst # leader \/ m.src \notin Peers)
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, lastRequestId, nextIndex,
                 matchIndex, handledKeys, badCrossPeerMutation, badOldContext>>
DiscardDuplicate(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ Key(m) \in handledKeys
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, lastRequestId, nextIndex,
                 matchIndex, handledKeys, badCrossPeerMutation, badOldContext>>
Next ==
  \/ ChangeLeader
  \/ SendRequests
  \/ \E m \in pending : HandleSuccess(m)
  \/ \E m \in pending : HandleFailure(m)
  \/ \E m \in pending : DiscardIneligible(m)
  \/ \E m \in pending : DiscardDuplicate(m)
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
TypeOK ==
  /\ currentTerm \in 1..2
  /\ leader \in {"A","C"}
  /\ lastRequestId \in 0..1
  /\ nextIndex \in [Peers -> 1..7]
  /\ matchIndex \in [Peers -> 0..6]
  /\ pending \subseteq {OldSuccessB, OldFailureD, SuccessB, SuccessD,
                           FailureB, FailureD}
  /\ handledKeys \subseteq
       [term : 1..2, leader : {"A","C"}, peer : Peers, requestId : 1..1]
  /\ badCrossPeerMutation \in BOOLEAN
  /\ badOldContext \in BOOLEAN
CursorLowerBound == \A p \in Peers : nextIndex[p] >= matchIndex[p] + 1
NoCrossPeerMutation == badCrossPeerMutation = FALSE
NoOldContextAcceptance == badOldContext = FALSE
EventuallyBothFailuresHandled ==
  <> (\A p \in Peers : [term |-> 2, leader |-> "C", peer |-> p,
                          requestId |-> 1] \in handledKeys)
EventuallyPendingDrained == <> (pending = {})
====
