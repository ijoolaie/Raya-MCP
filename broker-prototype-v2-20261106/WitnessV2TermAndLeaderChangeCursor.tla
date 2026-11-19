---- MODULE WitnessV2TermAndLeaderChangeCursor ----
EXTENDS Naturals, FiniteSets, TLC
CONSTANT CheckContextGuards
VARIABLES currentTerm, leader, peer, lastRequestId, nextIndex,
          matchIndex, pending, handledKeys, badOldAcceptance
vars == <<currentTerm, leader, peer, lastRequestId, nextIndex,
          matchIndex, pending, handledKeys, badOldAcceptance>>
Key(m) == [term |-> m.term, leader |-> m.dst, peer |-> m.src,
           requestId |-> m.requestId]
OldSuccess == [kind |-> "SUCCESS", term |-> 1, src |-> "B",
               dst |-> "A", requestId |-> 1, matchedThrough |-> 4,
               copyId |-> 1]
OldFailure == [kind |-> "FAILURE", term |-> 1, src |-> "B",
               dst |-> "A", requestId |-> 1, matchedThrough |-> 0,
               copyId |-> 2]
NewSuccess == [kind |-> "SUCCESS", term |-> 2, src |-> "B",
               dst |-> "C", requestId |-> 1, matchedThrough |-> 2,
               copyId |-> 3]
NewFailure == [kind |-> "FAILURE", term |-> 2, src |-> "B",
               dst |-> "C", requestId |-> 1, matchedThrough |-> 0,
               copyId |-> 4]
Init ==
  /\ currentTerm = 1
  /\ leader = "A"
  /\ peer = "B"
  /\ lastRequestId = 1
  /\ nextIndex = 5
  /\ matchIndex = 1
  /\ pending = {OldSuccess, OldFailure}
  /\ handledKeys = {}
  /\ badOldAcceptance = FALSE
ChangeTermAndLeader ==
  /\ currentTerm = 1
  /\ currentTerm' = 2
  /\ leader' = "C"
  /\ peer' = "B"
  /\ lastRequestId' = 0
  /\ nextIndex' = 3
  /\ matchIndex' = 0
  /\ pending' = pending \cup {NewSuccess, NewFailure}
  /\ UNCHANGED <<handledKeys, badOldAcceptance>>
SendNewLeaderRequest ==
  /\ currentTerm = 2
  /\ leader = "C"
  /\ lastRequestId = 0
  /\ lastRequestId' = 1
  /\ UNCHANGED <<currentTerm, leader, peer, nextIndex, matchIndex,
                 pending, handledKeys, badOldAcceptance>>
HandleSuccess(m) ==
  /\ m \in pending
  /\ m.kind = "SUCCESS"
  /\ (IF CheckContextGuards THEN
        /\ m.term = currentTerm
        /\ m.dst = leader
        /\ m.src = peer
      ELSE TRUE)
  /\ matchIndex' = IF m.matchedThrough > matchIndex THEN m.matchedThrough ELSE matchIndex
  /\ nextIndex' = IF m.matchedThrough + 1 > nextIndex THEN m.matchedThrough + 1 ELSE nextIndex
  /\ badOldAcceptance' = IF m.term = currentTerm /\ m.dst = leader
                         THEN badOldAcceptance ELSE TRUE
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, peer, lastRequestId, handledKeys>>
HandleFailure(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ (IF CheckContextGuards THEN
        /\ m.term = currentTerm
        /\ m.dst = leader
        /\ m.src = peer
      ELSE TRUE)
  /\ m.requestId = lastRequestId
  /\ Key(m) \notin handledKeys
  /\ nextIndex' = IF nextIndex - 1 > matchIndex + 1
                  THEN nextIndex - 1 ELSE matchIndex + 1
  /\ handledKeys' = handledKeys \cup {Key(m)}
  /\ badOldAcceptance' = IF m.term = currentTerm /\ m.dst = leader
                         THEN badOldAcceptance ELSE TRUE
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, peer, lastRequestId, matchIndex>>
DiscardIneligible(m) ==
  /\ m \in pending
  /\ (m.term # currentTerm \/ m.dst # leader \/ m.src # peer)
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, peer, lastRequestId, nextIndex,
                 matchIndex, handledKeys, badOldAcceptance>>
DiscardDuplicate(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ Key(m) \in handledKeys
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, peer, lastRequestId, nextIndex,
                 matchIndex, handledKeys, badOldAcceptance>>
Next ==
  \/ ChangeTermAndLeader
  \/ SendNewLeaderRequest
  \/ \E m \in pending : HandleSuccess(m)
  \/ \E m \in pending : HandleFailure(m)
  \/ \E m \in pending : DiscardIneligible(m)
  \/ \E m \in pending : DiscardDuplicate(m)
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
TypeOK ==
  /\ currentTerm \in 1..2
  /\ leader \in {"A","C"}
  /\ peer = "B"
  /\ lastRequestId \in 0..1
  /\ nextIndex \in 1..6
  /\ matchIndex \in 0..4
  /\ pending \subseteq {OldSuccess, OldFailure, NewSuccess, NewFailure}
  /\ handledKeys \subseteq
       [term : 1..2, leader : {"A","C"}, peer : {"B"}, requestId : 1..1]
  /\ badOldAcceptance \in BOOLEAN
CursorLowerBound == nextIndex >= matchIndex + 1
NoOldLeaderResponseAccepted == badOldAcceptance = FALSE
EventuallyNewLeaderFailureHandled ==
  <> ([term |-> 2, leader |-> "C", peer |-> "B", requestId |-> 1] \in handledKeys)
EventuallyPendingDrained == <> (pending = {})
====
