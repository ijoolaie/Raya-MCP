---- MODULE WitnessV2ThreeConcurrentDelayedFailureDedup ----
EXTENDS Naturals, FiniteSets, TLC
CONSTANTS IncludeRequestId
Nodes == {"A", "B"}
VARIABLES currentTerm, leader, peer, lastRequestId, nextIndex,
          matchIndex, pending, handledKeys, currentFailureAccepted
vars == <<currentTerm, leader, peer, lastRequestId, nextIndex,
          matchIndex, pending, handledKeys, currentFailureAccepted>>
FailureKey(m) ==
  [term |-> m.term, leader |-> m.dst, peer |-> m.src,
   requestId |-> IF IncludeRequestId THEN m.requestId ELSE 0]
Failure(t, r, c) ==
  [kind |-> "FAILURE", term |-> t, src |-> "B",
   dst |-> "A", requestId |-> r, matchIndex |-> 0, copyId |-> c]
Success(t, r, mi, c) ==
  [kind |-> "SUCCESS", term |-> t, src |-> "B",
   dst |-> "A", requestId |-> r, matchIndex |-> mi, copyId |-> c]
OldKey ==
  [term |-> 1, leader |-> "A", peer |-> "B",
   requestId |-> IF IncludeRequestId THEN 1 ELSE 0]
Init ==
  /\ currentTerm = 1
  /\ leader = "A"
  /\ peer = "B"
  /\ lastRequestId = 3
  /\ nextIndex = 5
  /\ matchIndex = 1
  /\ handledKeys = {OldKey}
  /\ currentFailureAccepted = FALSE
  /\ pending = {Failure(1,1,1), Failure(1,1,2),
                 Failure(1,2,3), Failure(1,2,4),
                 Failure(1,3,5), Failure(1,3,6),
                 Success(1,1,2,7)}
HandleSuccess(m) ==
  /\ m \in pending
  /\ m.kind = "SUCCESS"
  /\ m.term = currentTerm
  /\ m.src = peer
  /\ m.dst = leader
  /\ matchIndex' = IF m.matchIndex > matchIndex THEN m.matchIndex ELSE matchIndex
  /\ nextIndex' = IF m.matchIndex + 1 > nextIndex THEN m.matchIndex + 1 ELSE nextIndex
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, peer, lastRequestId, handledKeys, currentFailureAccepted>>
HandleCurrentFailure(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ m.term = currentTerm
  /\ m.src = peer
  /\ m.dst = leader
  /\ m.requestId = lastRequestId
  /\ FailureKey(m) \notin handledKeys
  /\ nextIndex' = IF nextIndex - 1 > matchIndex + 1
                  THEN nextIndex - 1 ELSE matchIndex + 1
  /\ handledKeys' = handledKeys \cup {FailureKey(m)}
  /\ currentFailureAccepted' = TRUE
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, peer, lastRequestId, matchIndex>>
DiscardStaleFailure(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ m.requestId # lastRequestId
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, peer, lastRequestId,
                 nextIndex, matchIndex, handledKeys, currentFailureAccepted>>
DiscardDuplicateFailure(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ m.requestId = lastRequestId
  /\ FailureKey(m) \in handledKeys
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, peer, lastRequestId,
                 nextIndex, matchIndex, handledKeys, currentFailureAccepted>>
Next ==
  \/ \E m \in pending : HandleSuccess(m)
  \/ \E m \in pending : HandleCurrentFailure(m)
  \/ \E m \in pending : DiscardStaleFailure(m)
  \/ \E m \in pending : DiscardDuplicateFailure(m)
Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(Next)
TypeOK ==
  /\ currentTerm = 1
  /\ currentFailureAccepted \in BOOLEAN
  /\ leader \in Nodes
  /\ peer \in Nodes
  /\ lastRequestId = 3
  /\ nextIndex \in 1..5
  /\ matchIndex \in 0..3
  /\ pending \subseteq
       [kind : {"SUCCESS","FAILURE"}, term : {1}, src : Nodes,
        dst : Nodes, requestId : 1..3, matchIndex : 0..2, copyId : 1..7]
  /\ handledKeys \subseteq
       [term : {1}, leader : Nodes, peer : Nodes, requestId : 0..3]
CursorLowerBound == nextIndex >= matchIndex + 1
CurrentFailureHandled == currentFailureAccepted
EventuallyCurrentFailureHandled == <> CurrentFailureHandled
====
