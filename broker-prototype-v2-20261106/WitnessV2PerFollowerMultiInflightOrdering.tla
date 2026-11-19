---- MODULE WitnessV2PerFollowerMultiInflightOrdering ----
EXTENDS Naturals, FiniteSets, TLC
CONSTANT CheckRequestIdGuard
Peers == {"B","D"}
VARIABLES currentTerm, leader, lastRequestId, nextIndex, matchIndex,
          pending, handledKeys, badStaleFailureAcceptance
vars == <<currentTerm, leader, lastRequestId, nextIndex, matchIndex,
          pending, handledKeys, badStaleFailureAcceptance>>
Key(m) == [term |-> m.term, leader |-> m.dst, peer |-> m.src,
           requestId |-> m.requestId]
Msg(peer, kind, rid, matched, copy) ==
  [term |-> 2, dst |-> "C", src |-> peer, kind |-> kind,
   requestId |-> rid, matchedThrough |-> matched, copyId |-> copy]
Messages ==
  {Msg("B","SUCCESS",1,2,1), Msg("B","SUCCESS",2,3,2),
   Msg("B","SUCCESS",3,4,3), Msg("B","FAILURE",1,0,4),
   Msg("B","FAILURE",2,0,5), Msg("B","FAILURE",3,0,6),
   Msg("D","SUCCESS",1,2,7), Msg("D","SUCCESS",2,3,8),
   Msg("D","SUCCESS",3,4,9), Msg("D","FAILURE",1,0,10),
   Msg("D","FAILURE",2,0,11), Msg("D","FAILURE",3,0,12)}
Init ==
  /\ currentTerm = 2
  /\ leader = "C"
  /\ lastRequestId = [p \in Peers |-> 3]
  /\ nextIndex = [p \in Peers |-> 5]
  /\ matchIndex = [p \in Peers |-> 0]
  /\ pending = Messages
  /\ handledKeys = {}
  /\ badStaleFailureAcceptance = FALSE
HandleSuccess(m) ==
  /\ m \in pending
  /\ m.kind = "SUCCESS"
  /\ m.term = currentTerm
  /\ m.dst = leader
  /\ m.src \in Peers
  /\ LET p == m.src
         newMatch == IF m.matchedThrough > matchIndex[p]
                     THEN m.matchedThrough ELSE matchIndex[p]
         newNext == IF m.matchedThrough + 1 > nextIndex[p]
                    THEN m.matchedThrough + 1 ELSE nextIndex[p]
     IN
       /\ matchIndex' = [matchIndex EXCEPT ![p] = newMatch]
       /\ nextIndex' = [nextIndex EXCEPT ![p] = newNext]
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, lastRequestId, handledKeys,
                 badStaleFailureAcceptance>>
HandleCurrentFailure(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ m.term = currentTerm
  /\ m.dst = leader
  /\ m.src \in Peers
  /\ (IF CheckRequestIdGuard THEN m.requestId = lastRequestId[m.src] ELSE TRUE)
  /\ Key(m) \notin handledKeys
  /\ LET p == m.src
         newNext == IF nextIndex[p] - 1 > matchIndex[p] + 1
                    THEN nextIndex[p] - 1 ELSE matchIndex[p] + 1
     IN
       /\ nextIndex' = [nextIndex EXCEPT ![p] = newNext]
       /\ matchIndex' = matchIndex
       /\ handledKeys' = handledKeys \cup {Key(m)}
  /\ badStaleFailureAcceptance' =
       IF m.requestId = lastRequestId[m.src]
       THEN badStaleFailureAcceptance ELSE TRUE
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, lastRequestId>>
DiscardStaleFailure(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ m.requestId # lastRequestId[m.src]
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, lastRequestId, nextIndex,
                 matchIndex, handledKeys, badStaleFailureAcceptance>>
DiscardDuplicateFailure(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ Key(m) \in handledKeys
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, lastRequestId, nextIndex,
                 matchIndex, handledKeys, badStaleFailureAcceptance>>
Next ==
  \/ \E m \in pending : HandleSuccess(m)
  \/ \E m \in pending : HandleCurrentFailure(m)
  \/ \E m \in pending : DiscardStaleFailure(m)
  \/ \E m \in pending : DiscardDuplicateFailure(m)
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
TypeOK ==
  /\ currentTerm = 2
  /\ leader = "C"
  /\ lastRequestId = [p \in Peers |-> 3]
  /\ nextIndex \in [Peers -> 1..6]
  /\ matchIndex \in [Peers -> 0..4]
  /\ pending \subseteq Messages
  /\ handledKeys \subseteq
       [term : {2}, leader : {"C"}, peer : Peers, requestId : 1..3]
  /\ badStaleFailureAcceptance \in BOOLEAN
CursorLowerBound == \A p \in Peers : nextIndex[p] >= matchIndex[p] + 1
NoStaleFailureAcceptance == badStaleFailureAcceptance = FALSE
EventuallyBothLatestFailuresHandled ==
  <> (\A p \in Peers : [term |-> 2, leader |-> "C", peer |-> p,
                          requestId |-> 3] \in handledKeys)
EventuallyAllResponsesDrained == <> (pending = {})
====
