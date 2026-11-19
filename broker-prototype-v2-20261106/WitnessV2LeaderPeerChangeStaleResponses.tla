---- MODULE WitnessV2LeaderPeerChangeStaleResponses ----
EXTENDS Naturals, FiniteSets, TLC
CONSTANT CheckPeerGuard
VARIABLES currentTerm, leader, activePeer, lastRequestId, nextIndex,
          matchIndex, pending, handledKeys, badOldContext, badPeerContext
vars == <<currentTerm, leader, activePeer, lastRequestId, nextIndex,
          matchIndex, pending, handledKeys, badOldContext, badPeerContext>>
Key(m) == [term |-> m.term, leader |-> m.dst, peer |-> m.src,
           requestId |-> m.requestId]
OldLeaderSuccess == [kind |-> "SUCCESS", term |-> 1, src |-> "B",
  dst |-> "A", requestId |-> 1, matchedThrough |-> 4, copyId |-> 1]
OldLeaderFailure == [kind |-> "FAILURE", term |-> 1, src |-> "B",
  dst |-> "A", requestId |-> 1, matchedThrough |-> 0, copyId |-> 2]
WrongPeerSuccess == [kind |-> "SUCCESS", term |-> 2, src |-> "B",
  dst |-> "C", requestId |-> 1, matchedThrough |-> 4, copyId |-> 3]
WrongPeerFailure == [kind |-> "FAILURE", term |-> 2, src |-> "B",
  dst |-> "C", requestId |-> 1, matchedThrough |-> 0, copyId |-> 4]
CurrentSuccess == [kind |-> "SUCCESS", term |-> 2, src |-> "D",
  dst |-> "C", requestId |-> 1, matchedThrough |-> 2, copyId |-> 5]
CurrentFailure == [kind |-> "FAILURE", term |-> 2, src |-> "D",
  dst |-> "C", requestId |-> 1, matchedThrough |-> 0, copyId |-> 6]
Init ==
  /\ currentTerm = 1
  /\ leader = "A"
  /\ activePeer = "B"
  /\ lastRequestId = 1
  /\ nextIndex = 5
  /\ matchIndex = 1
  /\ pending = {OldLeaderSuccess, OldLeaderFailure}
  /\ handledKeys = {}
  /\ badOldContext = FALSE
  /\ badPeerContext = FALSE
ChangeLeaderAndPeer ==
  /\ currentTerm = 1
  /\ currentTerm' = 2
  /\ leader' = "C"
  /\ activePeer' = "D"
  /\ lastRequestId' = 0
  /\ nextIndex' = 3
  /\ matchIndex' = 0
  /\ pending' = pending \cup {WrongPeerSuccess, WrongPeerFailure,
                                  CurrentSuccess, CurrentFailure}
  /\ UNCHANGED <<handledKeys, badOldContext, badPeerContext>>
SendCurrentRequest ==
  /\ currentTerm = 2
  /\ leader = "C"
  /\ activePeer = "D"
  /\ lastRequestId = 0
  /\ lastRequestId' = 1
  /\ UNCHANGED <<currentTerm, leader, activePeer, nextIndex, matchIndex,
                 pending, handledKeys, badOldContext, badPeerContext>>
HandleSuccess(m) ==
  /\ m \in pending
  /\ m.kind = "SUCCESS"
  /\ m.term = currentTerm
  /\ m.dst = leader
  /\ (IF CheckPeerGuard THEN m.src = activePeer ELSE TRUE)
  /\ matchIndex' = IF m.matchedThrough > matchIndex THEN m.matchedThrough ELSE matchIndex
  /\ nextIndex' = IF m.matchedThrough + 1 > nextIndex THEN m.matchedThrough + 1 ELSE nextIndex
  /\ badOldContext' = IF m.term = currentTerm /\ m.dst = leader
                      THEN badOldContext ELSE TRUE
  /\ badPeerContext' = IF m.src = activePeer THEN badPeerContext ELSE TRUE
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, activePeer, lastRequestId, handledKeys>>
HandleFailure(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ m.term = currentTerm
  /\ m.dst = leader
  /\ (IF CheckPeerGuard THEN m.src = activePeer ELSE TRUE)
  /\ m.requestId = lastRequestId
  /\ Key(m) \notin handledKeys
  /\ nextIndex' = IF nextIndex - 1 > matchIndex + 1
                  THEN nextIndex - 1 ELSE matchIndex + 1
  /\ handledKeys' = handledKeys \cup {Key(m)}
  /\ badOldContext' = IF m.term = currentTerm /\ m.dst = leader
                      THEN badOldContext ELSE TRUE
  /\ badPeerContext' = IF m.src = activePeer THEN badPeerContext ELSE TRUE
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, activePeer, lastRequestId, matchIndex>>
DiscardIneligible(m) ==
  /\ m \in pending
  /\ (m.term # currentTerm \/ m.dst # leader \/ m.src # activePeer)
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, activePeer, lastRequestId, nextIndex,
                 matchIndex, handledKeys, badOldContext, badPeerContext>>
DiscardDuplicate(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ Key(m) \in handledKeys
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, activePeer, lastRequestId, nextIndex,
                 matchIndex, handledKeys, badOldContext, badPeerContext>>
Next ==
  \/ ChangeLeaderAndPeer
  \/ SendCurrentRequest
  \/ \E m \in pending : HandleSuccess(m)
  \/ \E m \in pending : HandleFailure(m)
  \/ \E m \in pending : DiscardIneligible(m)
  \/ \E m \in pending : DiscardDuplicate(m)
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
TypeOK ==
  /\ currentTerm \in 1..2
  /\ leader \in {"A","C"}
  /\ activePeer \in {"B","D"}
  /\ lastRequestId \in 0..1
  /\ nextIndex \in 1..6
  /\ matchIndex \in 0..4
  /\ pending \subseteq {OldLeaderSuccess, OldLeaderFailure,
       WrongPeerSuccess, WrongPeerFailure, CurrentSuccess, CurrentFailure}
  /\ handledKeys \subseteq
       [term : 1..2, leader : {"A","C"}, peer : {"B","D"}, requestId : 1..1]
  /\ badOldContext \in BOOLEAN
  /\ badPeerContext \in BOOLEAN
CursorLowerBound == nextIndex >= matchIndex + 1
NoOldContextAcceptance == badOldContext = FALSE
NoWrongPeerAcceptance == badPeerContext = FALSE
EventuallyCurrentFailureHandled ==
  <> ([term |-> 2, leader |-> "C", peer |-> "D", requestId |-> 1] \in handledKeys)
EventuallyPendingDrained == <> (pending = {})
====
