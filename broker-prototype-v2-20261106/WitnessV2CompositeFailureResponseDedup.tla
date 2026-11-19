---- MODULE WitnessV2CompositeFailureResponseDedup ----
EXTENDS Naturals, FiniteSets, TLC

CONSTANTS ClampFailures, CheckTermGuard

Nodes == {"A", "B", "C"}

VARIABLES currentTerm, leader, peer, lastRequestId,
          nextIndex, matchIndex, pending, handledKeys,
          badOldTermAcceptance
vars == <<currentTerm, leader, peer, lastRequestId,
          nextIndex, matchIndex, pending, handledKeys,
          badOldTermAcceptance>>

FailureKey(m) ==
  [term |-> m.term, leader |-> m.dst, peer |-> m.src,
   requestId |-> m.requestId]

Init ==
  /\ currentTerm = 1
  /\ leader = "A"
  /\ peer = "B"
  /\ lastRequestId = 1
  /\ nextIndex = 3
  /\ matchIndex = 1
  /\ handledKeys = {}
  /\ badOldTermAcceptance = FALSE
  /\ pending =
       {[kind |-> "FAILURE", term |-> 1, src |-> "B",
         dst |-> "A", requestId |-> 1, copyId |-> c]
          : c \in {1, 2}}

HandleCurrentFailure(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ (IF CheckTermGuard THEN m.term = currentTerm ELSE TRUE)
  /\ m.dst = leader
  /\ m.src = peer
  /\ m.requestId = lastRequestId
  /\ FailureKey(m) \notin handledKeys
  /\ nextIndex' =
       IF ClampFailures
       THEN IF nextIndex - 1 > matchIndex + 1
            THEN nextIndex - 1 ELSE matchIndex + 1
       ELSE IF nextIndex > 1 THEN nextIndex - 1 ELSE 1
  /\ handledKeys' = handledKeys \cup {FailureKey(m)}
  /\ badOldTermAcceptance' =
       IF m.term = currentTerm THEN badOldTermAcceptance ELSE TRUE
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, peer, lastRequestId, matchIndex>>

DiscardDuplicate(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ FailureKey(m) \in handledKeys
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, peer, lastRequestId,
                 nextIndex, matchIndex, handledKeys, badOldTermAcceptance>>

ChangeLeadership ==
  /\ currentTerm = 1
  /\ currentTerm' = 2
  /\ leader' = "A"
  /\ peer' = "B"
  /\ lastRequestId' = 0
  /\ UNCHANGED <<nextIndex, matchIndex, pending, handledKeys,
                 badOldTermAcceptance>>

SendNewTermFailure ==
  /\ currentTerm = 2
  /\ leader = "A"
  /\ lastRequestId = 0
  /\ lastRequestId' = 1
  /\ pending' = pending \cup
       {[kind |-> "FAILURE", term |-> 2, src |-> "B",
         dst |-> "A", requestId |-> 1, copyId |-> 1]}
  /\ UNCHANGED <<currentTerm, leader, peer, nextIndex,
                 matchIndex, handledKeys, badOldTermAcceptance>>

Next ==
  \/ \E m \in pending : HandleCurrentFailure(m)
  \/ \E m \in pending : DiscardDuplicate(m)
  \/ ChangeLeadership
  \/ SendNewTermFailure

Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(Next)

TypeOK ==
  /\ currentTerm \in 1..2
  /\ leader = "A"
  /\ peer = "B"
  /\ lastRequestId \in 0..1
  /\ nextIndex \in 1..3
  /\ matchIndex \in 0..2
  /\ pending \subseteq
       [kind : {"FAILURE"}, term : 1..2,
        src : Nodes, dst : Nodes, requestId : 1..1, copyId : 1..2]
  /\ handledKeys \subseteq
       [term : 1..2, leader : Nodes, peer : Nodes, requestId : 1..1]
  /\ badOldTermAcceptance \in BOOLEAN

CursorLowerBound == nextIndex >= matchIndex + 1
NoOldTermResponseAccepted == badOldTermAcceptance = FALSE
EventuallyNewTermFailureHandled ==
  <> ([term |-> 2, leader |-> "A", peer |-> "B", requestId |-> 1]
        \in handledKeys)

====