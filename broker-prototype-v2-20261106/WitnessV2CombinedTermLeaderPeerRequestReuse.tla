---- MODULE WitnessV2CombinedTermLeaderPeerRequestReuse ----
EXTENDS Naturals, FiniteSets, TLC
CONSTANTS CheckContextGuard, CheckRequestIdGuard, RouteBySource
OldPeers == {"B","D"}
NewPeers == {"B","E"}
AllPeers == OldPeers \cup NewPeers
VARIABLES currentTerm, leader, activePeers, lastRequestId, nextIndex,
          matchIndex, pending, handledKeys, badOldContext, badStaleFailure, badCrossPeer
vars == <<currentTerm, leader, activePeers, lastRequestId, nextIndex,
          matchIndex, pending, handledKeys, badOldContext, badStaleFailure, badCrossPeer>>
TargetPeer(m) == IF RouteBySource THEN m.src ELSE (IF m.src = "B" THEN "E" ELSE "B")
Key(m) == [term |-> m.term, leader |-> m.dst, peer |-> m.src,
           requestId |-> m.requestId]
OldMsgs ==
  {[kind |-> k, term |-> 1, dst |-> "A", src |-> p,
    requestId |-> r, matchedThrough |-> 4] :
       k \in {"SUCCESS"}, p \in OldPeers, r \in {1}}
NewMsgs ==
  {[kind |-> k, term |-> 2, dst |-> "C", src |-> p,
    requestId |-> r, matchedThrough |-> (IF p = "B" THEN 2 ELSE 3)] :
       k \in {"SUCCESS","FAILURE"}, p \in NewPeers, r \in 1..2}
Init ==
  /\ currentTerm = 1
  /\ leader = "A"
  /\ activePeers = OldPeers
  /\ lastRequestId = [p \in AllPeers |-> 2]
  /\ nextIndex = [p \in AllPeers |-> 5]
  /\ matchIndex = [p \in AllPeers |-> 0]
  /\ pending = OldMsgs
  /\ handledKeys = {}
  /\ badOldContext = FALSE
  /\ badStaleFailure = FALSE
  /\ badCrossPeer = FALSE
ChangeTermLeaderPeerAndReuseIds ==
  /\ currentTerm = 1
  /\ currentTerm' = 2
  /\ leader' = "C"
  /\ activePeers' = NewPeers
  /\ lastRequestId' = [lastRequestId EXCEPT !["B"] = 2, !["E"] = 2]
  /\ nextIndex' = [nextIndex EXCEPT !["B"] = 5, !["E"] = 5]
  /\ matchIndex' = [matchIndex EXCEPT !["B"] = 0, !["E"] = 0]
  /\ pending' = pending \cup NewMsgs
  /\ UNCHANGED <<handledKeys, badOldContext, badStaleFailure, badCrossPeer>>
ContextOK(m) ==
  m.term = currentTerm /\ m.dst = leader /\ m.src \in activePeers
HandleSuccess(m) ==
  /\ m \in pending
  /\ m.kind = "SUCCESS"
  /\ (IF CheckContextGuard THEN ContextOK(m) ELSE TRUE)
  /\ LET p == TargetPeer(m)
         nm == IF m.matchedThrough > matchIndex[p]
               THEN m.matchedThrough ELSE matchIndex[p]
         nn == IF m.matchedThrough + 1 > nextIndex[p]
               THEN m.matchedThrough + 1 ELSE nextIndex[p]
     IN
       /\ matchIndex' = [matchIndex EXCEPT ![p] = nm]
       /\ nextIndex' = [nextIndex EXCEPT ![p] = nn]
  /\ badOldContext' = IF ContextOK(m) THEN badOldContext ELSE TRUE
  /\ badStaleFailure' = badStaleFailure
  /\ badCrossPeer' = IF TargetPeer(m) = m.src THEN badCrossPeer ELSE TRUE
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, activePeers, lastRequestId, handledKeys>>
HandleFailure(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ (IF CheckContextGuard THEN ContextOK(m) ELSE TRUE)
  /\ (IF CheckRequestIdGuard THEN m.requestId = lastRequestId[m.src] ELSE TRUE)
  /\ Key(m) \notin handledKeys
  /\ LET p == TargetPeer(m)
         nn == IF nextIndex[p] - 1 > matchIndex[p] + 1
               THEN nextIndex[p] - 1 ELSE matchIndex[p] + 1
     IN
       /\ nextIndex' = [nextIndex EXCEPT ![p] = nn]
       /\ matchIndex' = matchIndex
       /\ handledKeys' = handledKeys \cup {Key(m)}
  /\ badOldContext' = IF ContextOK(m) THEN badOldContext ELSE TRUE
  /\ badStaleFailure' =
       IF m.requestId = lastRequestId[m.src] THEN badStaleFailure ELSE TRUE
  /\ badCrossPeer' = IF TargetPeer(m) = m.src THEN badCrossPeer ELSE TRUE
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, activePeers, lastRequestId>>
DiscardIneligible(m) ==
  /\ m \in pending
  /\ ~ContextOK(m)
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, activePeers, lastRequestId,
                 nextIndex, matchIndex, handledKeys, badOldContext,
                 badStaleFailure, badCrossPeer>>
DiscardStaleFailure(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ ContextOK(m)
  /\ m.requestId # lastRequestId[m.src]
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, activePeers, lastRequestId,
                 nextIndex, matchIndex, handledKeys, badOldContext,
                 badStaleFailure, badCrossPeer>>
DiscardDuplicateFailure(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ Key(m) \in handledKeys
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<currentTerm, leader, activePeers, lastRequestId,
                 nextIndex, matchIndex, handledKeys, badOldContext,
                 badStaleFailure, badCrossPeer>>
Next ==
  \/ ChangeTermLeaderPeerAndReuseIds
  \/ \E m \in pending : HandleSuccess(m)
  \/ \E m \in pending : HandleFailure(m)
  \/ \E m \in pending : DiscardIneligible(m)
  \/ \E m \in pending : DiscardStaleFailure(m)
  \/ \E m \in pending : DiscardDuplicateFailure(m)
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
TypeOK ==
  /\ currentTerm \in 1..2
  /\ leader \in {"A","C"}
  /\ activePeers \in {OldPeers, NewPeers}
  /\ lastRequestId \in [AllPeers -> 0..3]
  /\ nextIndex \in [AllPeers -> 1..6]
  /\ matchIndex \in [AllPeers -> 0..4]
  /\ pending \subseteq OldMsgs \cup NewMsgs
  /\ handledKeys \subseteq
       [term : 1..2, leader : {"A","C"}, peer : AllPeers, requestId : 1..2]
  /\ badOldContext \in BOOLEAN
  /\ badStaleFailure \in BOOLEAN
  /\ badCrossPeer \in BOOLEAN
CursorLowerBound == \A p \in AllPeers : nextIndex[p] >= matchIndex[p] + 1
NoOldContextAcceptance == badOldContext = FALSE
NoStaleFailureAcceptance == badStaleFailure = FALSE
NoCrossPeerMutation == badCrossPeer = FALSE
EventuallyNewPeerFailuresHandled ==
  <> (\A p \in NewPeers : [term |-> 2, leader |-> "C", peer |-> p,
                            requestId |-> 2] \in handledKeys)
EventuallyPendingDrained == <> (pending = {})
====