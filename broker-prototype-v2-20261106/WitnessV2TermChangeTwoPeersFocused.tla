---- MODULE WitnessV2TermChangeTwoPeersFocused ----
EXTENDS Naturals, FiniteSets, TLC
CONSTANTS CheckTermGuard, CheckRequestIdGuard
Peers == {"B", "C"}
VARIABLES currentTerm, leader, lastRequestId, nextIndex, matchIndex,
          pending, handledKeys, badOldTermAcceptance, badStaleFailure
vars == <<currentTerm, leader, lastRequestId, nextIndex, matchIndex,
          pending, handledKeys, badOldTermAcceptance, badStaleFailure>>
Key(m) == [term |-> m.term, leader |-> m.dst, peer |-> m.src,
           requestId |-> m.requestId]
Msg(term, peer, kind, rid, matched, copy) ==
  [term |-> term, dst |-> "A", src |-> peer, kind |-> kind,
   requestId |-> rid, matchedThrough |-> matched, copyId |-> copy]
OldMessages ==
  {Msg(1,"B","SUCCESS",1,4,1), Msg(1,"B","SUCCESS",2,4,2),
   Msg(1,"C","SUCCESS",1,6,3), Msg(1,"C","SUCCESS",2,6,4)}
CurrentMessages ==
  {Msg(2,"B","SUCCESS",1,2,5), Msg(2,"B","FAILURE",1,0,6),
   Msg(2,"B","SUCCESS",2,3,7), Msg(2,"B","FAILURE",2,0,8),
   Msg(2,"C","SUCCESS",1,4,9), Msg(2,"C","FAILURE",1,0,10),
   Msg(2,"C","SUCCESS",2,5,11), Msg(2,"C","FAILURE",2,0,12)}
Init ==
  /\ currentTerm = 1
  /\ leader = "A"
  /\ lastRequestId = [p \in Peers |-> 2]
  /\ nextIndex = [p \in Peers |-> IF p = "B" THEN 5 ELSE 7]
  /\ matchIndex = [p \in Peers |-> IF p = "B" THEN 1 ELSE 2]
  /\ pending = OldMessages
  /\ handledKeys = {}
  /\ badOldTermAcceptance = FALSE
  /\ badStaleFailure = FALSE
AdvanceTermAndReuseIds ==
  /\ currentTerm = 1
  /\ currentTerm' = 2
  /\ leader' = leader
  /\ lastRequestId' = lastRequestId
  /\ nextIndex' = nextIndex
  /\ matchIndex' = [p \in Peers |-> 0]
  /\ pending' = pending \cup CurrentMessages
  /\ UNCHANGED <<handledKeys, badOldTermAcceptance, badStaleFailure>>
HandleSuccess(m) ==
  /\ m \in pending
  /\ m.kind = "SUCCESS"
  /\ (IF CheckTermGuard THEN m.term = currentTerm ELSE TRUE)
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
  /\ badOldTermAcceptance' =
       IF m.term = currentTerm THEN badOldTermAcceptance ELSE TRUE
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, lastRequestId, handledKeys,
                 badStaleFailure>>
HandleFailure(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ (IF CheckTermGuard THEN m.term = currentTerm ELSE TRUE)
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
  /\ badOldTermAcceptance' =
       IF m.term = currentTerm THEN badOldTermAcceptance ELSE TRUE
  /\ badStaleFailure' =
       IF m.requestId = lastRequestId[m.src] THEN badStaleFailure ELSE TRUE
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, lastRequestId>>
DiscardIneligible(m) ==
  /\ m \in pending
  /\ (m.term # currentTerm \/ m.dst # leader \/ m.src \notin Peers)
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, lastRequestId, nextIndex,
                 matchIndex, handledKeys, badOldTermAcceptance,
                 badStaleFailure>>
DiscardStaleFailure(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ m.term = currentTerm
  /\ m.requestId # lastRequestId[m.src]
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, lastRequestId, nextIndex,
                 matchIndex, handledKeys, badOldTermAcceptance,
                 badStaleFailure>>
DiscardDuplicateFailure(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ Key(m) \in handledKeys
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, lastRequestId, nextIndex,
                 matchIndex, handledKeys, badOldTermAcceptance,
                 badStaleFailure>>
Next ==
  \/ AdvanceTermAndReuseIds
  \/ \E m \in pending : HandleSuccess(m)
  \/ \E m \in pending : HandleFailure(m)
  \/ \E m \in pending : DiscardIneligible(m)
  \/ \E m \in pending : DiscardStaleFailure(m)
  \/ \E m \in pending : DiscardDuplicateFailure(m)
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
TypeOK ==
  /\ currentTerm \in 1..2
  /\ leader = "A"
  /\ lastRequestId = [p \in Peers |-> 2]
  /\ nextIndex \in [Peers -> 1..8]
  /\ matchIndex \in [Peers -> 0..6]
  /\ pending \subseteq OldMessages \cup CurrentMessages
  /\ handledKeys \subseteq
       [term : 1..2, leader : {"A"}, peer : Peers, requestId : 1..2]
  /\ badOldTermAcceptance \in BOOLEAN
  /\ badStaleFailure \in BOOLEAN
CursorLowerBound == \A p \in Peers : nextIndex[p] >= matchIndex[p] + 1
PeerCursorIsolation ==
  /\ nextIndex["B"] >= 1
  /\ nextIndex["C"] >= 1
  /\ matchIndex["B"] <= 4
  /\ matchIndex["C"] <= 6
NoOldTermAcceptance == badOldTermAcceptance = FALSE
NoStaleFailureAcceptance == badStaleFailure = FALSE
EventuallyCurrentFailuresHandled ==
  /\ <> ([term |-> 2, leader |-> "A", peer |-> "B", requestId |-> 2]
         \in handledKeys)
  /\ <> ([term |-> 2, leader |-> "A", peer |-> "C", requestId |-> 2]
         \in handledKeys)
EventuallyAllResponsesDrained == <> (pending = {})
====