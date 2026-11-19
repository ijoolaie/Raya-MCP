---- MODULE WitnessV2MultiInflightTermChangeIsolation ----
EXTENDS Naturals, FiniteSets, TLC
CONSTANT CheckTermGuard
Peers == {"B","D"}
VARIABLES currentTerm, leader, lastRequestId, nextIndex, matchIndex,
          pending, handledKeys, badOldTermAcceptance
vars == <<currentTerm, leader, lastRequestId, nextIndex, matchIndex,
          pending, handledKeys, badOldTermAcceptance>>
Key(m) == [term |-> m.term, leader |-> m.dst, peer |-> m.src,
           requestId |-> m.requestId]
Msg(term, peer, kind, rid, matched, copy) ==
  [term |-> term, dst |-> "A", src |-> peer, kind |-> kind,
   requestId |-> rid, matchedThrough |-> matched, copyId |-> copy]
OldMessages ==
  {Msg(1,"B","SUCCESS",1,4,1), Msg(1,"B","FAILURE",1,0,2),
   Msg(1,"D","SUCCESS",1,4,3), Msg(1,"D","FAILURE",1,0,4)}
CurrentMessages ==
  {Msg(2,"B","SUCCESS",1,2,5), Msg(2,"B","FAILURE",1,0,6),
   Msg(2,"D","SUCCESS",1,3,7), Msg(2,"D","FAILURE",1,0,8)}
Init ==
  /\ currentTerm = 1
  /\ leader = "A"
  /\ lastRequestId = [p \in Peers |-> 1]
  /\ nextIndex = [p \in Peers |-> 5]
  /\ matchIndex = [p \in Peers |-> 1]
  /\ pending = OldMessages
  /\ handledKeys = {}
  /\ badOldTermAcceptance = FALSE
AdvanceTermAndSend ==
  /\ currentTerm = 1
  /\ currentTerm' = 2
  /\ leader' = leader
  /\ lastRequestId' = [p \in Peers |-> 1]
  /\ nextIndex' = [p \in Peers |-> 4]
  /\ matchIndex' = [p \in Peers |-> 0]
  /\ pending' = pending \cup CurrentMessages
  /\ UNCHANGED <<handledKeys, badOldTermAcceptance>>
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
  /\ UNCHANGED <<currentTerm, leader, lastRequestId, handledKeys>>
HandleCurrentFailure(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ (IF CheckTermGuard THEN m.term = currentTerm ELSE TRUE)
  /\ m.dst = leader
  /\ m.src \in Peers
  /\ m.requestId = lastRequestId[m.src]
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
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, lastRequestId>>
DiscardIneligible(m) ==
  /\ m \in pending
  /\ (m.term # currentTerm \/ m.dst # leader \/ m.src \notin Peers)
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, lastRequestId, nextIndex,
                 matchIndex, handledKeys, badOldTermAcceptance>>
DiscardDuplicateFailure(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ Key(m) \in handledKeys
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, lastRequestId, nextIndex,
                 matchIndex, handledKeys, badOldTermAcceptance>>
Next ==
  \/ AdvanceTermAndSend
  \/ \E m \in pending : HandleSuccess(m)
  \/ \E m \in pending : HandleCurrentFailure(m)
  \/ \E m \in pending : DiscardIneligible(m)
  \/ \E m \in pending : DiscardDuplicateFailure(m)
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
TypeOK ==
  /\ currentTerm \in 1..2
  /\ leader = "A"
  /\ lastRequestId = [p \in Peers |-> 1]
  /\ nextIndex \in [Peers -> 1..6]
  /\ matchIndex \in [Peers -> 0..4]
  /\ pending \subseteq OldMessages \cup CurrentMessages
  /\ handledKeys \subseteq
       [term : 1..2, leader : {"A"}, peer : Peers, requestId : 1..1]
  /\ badOldTermAcceptance \in BOOLEAN
CursorLowerBound == \A p \in Peers : nextIndex[p] >= matchIndex[p] + 1
NoOldTermAcceptance == badOldTermAcceptance = FALSE
EventuallyBothCurrentFailuresHandled ==
  <> (\A p \in Peers : [term |-> 2, leader |-> "A", peer |-> p,
                          requestId |-> 1] \in handledKeys)
EventuallyAllResponsesDrained == <> (pending = {})
====
