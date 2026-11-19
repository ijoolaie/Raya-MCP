---- MODULE WitnessV2IdentityScopeFailureDedup ----
EXTENDS Naturals, FiniteSets, TLC
CONSTANTS Scenario, IncludeLeader, IncludePeer
Nodes == {"A", "B", "C"}
Unknown == "UNKNOWN"
VARIABLES currentTerm, leader, peer, lastRequestId, nextIndex,
          matchIndex, pending, handledKeys, identityChanged
vars == <<currentTerm, leader, peer, lastRequestId, nextIndex,
          matchIndex, pending, handledKeys, identityChanged>>

FailureKey(m) ==
  [term |-> m.term,
   leader |-> IF IncludeLeader THEN m.dst ELSE Unknown,
   peer |-> IF IncludePeer THEN m.src ELSE Unknown,
   requestId |-> m.requestId]
OldFailure(c) ==
  [kind |-> "FAILURE", term |-> 1, src |-> "B",
   dst |-> "A", requestId |-> 1, copyId |-> c]

Init ==
  /\ currentTerm = 1
  /\ leader = "A"
  /\ peer = "B"
  /\ lastRequestId = 1
  /\ nextIndex = 3
  /\ matchIndex = 1
  /\ handledKeys = {}
  /\ identityChanged = FALSE
  /\ pending = {OldFailure(1), OldFailure(2)}

HandleCurrentFailure(m) ==
  /\ m \in pending
  /\ m.term = currentTerm
  /\ m.dst = leader
  /\ m.src = peer
  /\ m.requestId = lastRequestId
  /\ FailureKey(m) \notin handledKeys
  /\ nextIndex' = IF nextIndex - 1 > matchIndex + 1
                  THEN nextIndex - 1 ELSE matchIndex + 1
  /\ handledKeys' = handledKeys \cup {FailureKey(m)}
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, peer, lastRequestId,
                 matchIndex, identityChanged>>

DiscardDuplicate(m) ==
  /\ m \in pending
  /\ FailureKey(m) \in handledKeys
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, peer, lastRequestId,
                 nextIndex, matchIndex, handledKeys, identityChanged>>

ChangeIdentity ==
  /\ identityChanged = FALSE
  /\ handledKeys # {}
  /\ IF Scenario = "PEER"
     THEN /\ leader' = leader
          /\ peer' = "C"
     ELSE /\ leader' = "C"
          /\ peer' = peer
  /\ lastRequestId' = 0
  /\ identityChanged' = TRUE
  /\ UNCHANGED <<currentTerm, nextIndex, matchIndex, pending, handledKeys>>

SendNewIdentityFailure ==
  /\ identityChanged = TRUE
  /\ lastRequestId = 0
  /\ lastRequestId' = 1
  /\ pending' = pending \cup
       {[kind |-> "FAILURE", term |-> 1, src |-> peer,
         dst |-> leader, requestId |-> 1, copyId |-> 3]}
  /\ UNCHANGED <<currentTerm, leader, peer, nextIndex,
                 matchIndex, handledKeys, identityChanged>>

Next ==
  \/ \E m \in pending : HandleCurrentFailure(m)
  \/ \E m \in pending : DiscardDuplicate(m)
  \/ ChangeIdentity
  \/ SendNewIdentityFailure

Spec == Init /\ [][Next]_vars
FairSpec == Spec /\ WF_vars(Next)

TypeOK ==
  /\ currentTerm = 1
  /\ leader \in Nodes
  /\ peer \in Nodes
  /\ lastRequestId \in 0..1
  /\ nextIndex \in 1..3
  /\ matchIndex \in 0..2
  /\ identityChanged \in BOOLEAN
  /\ pending \subseteq
       [kind : {"FAILURE"}, term : {1}, src : Nodes, dst : Nodes,
        requestId : {1}, copyId : 1..3]
  /\ handledKeys \subseteq
       [term : {1}, leader : Nodes \cup {Unknown},
        peer : Nodes \cup {Unknown}, requestId : {1}]

CursorLowerBound == nextIndex >= matchIndex + 1
EventuallyNewIdentityFailureHandled ==
  <> ([term |-> 1,
       leader |-> IF Scenario = "LEADER" THEN "C" ELSE "A",
       peer |-> IF Scenario = "PEER" THEN "C" ELSE "B",
       requestId |-> 1] \in handledKeys)
====
