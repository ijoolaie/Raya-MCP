---- MODULE WitnessV2MixedDuplicateMultiPeerCursor ----
EXTENDS Naturals, FiniteSets, TLC
CONSTANTS DeduplicateFailures, ClampFailures
Peers == {"B","D"}
VARIABLES nextIndex, matchIndex, peerMatch, pending, handledKeys,
          badCursorBound, badMatchRegression, badPeerIsolation,
          badFailureDuplicate
vars == <<nextIndex, matchIndex, peerMatch, pending, handledKeys,
          badCursorBound, badMatchRegression, badPeerIsolation,
          badFailureDuplicate>>
Key(m) == [term |-> m.term, leader |-> m.leader, peer |-> m.peer,
           requestId |-> m.requestId]
Messages ==
  {[kind |-> k, term |-> 2, leader |-> "C", peer |-> p,
    requestId |-> r, matchedThrough |-> (IF k = "SUCCESS" THEN (IF r = 1 THEN 2 ELSE 4) ELSE 0), copyId |-> c] :
      k \in {"SUCCESS","FAILURE"}, p \in Peers, r \in 1..2,
      mt \in {1}, c \in 1..2}
Init ==
  /\ nextIndex = [p \in Peers |-> 5]
  /\ matchIndex = [p \in Peers |-> 0]
  /\ peerMatch = [p \in Peers |-> 4]
  /\ pending = Messages
  /\ handledKeys = {}
  /\ badCursorBound = FALSE
  /\ badMatchRegression = FALSE
  /\ badPeerIsolation = FALSE
  /\ badFailureDuplicate = FALSE
HandleSuccess(m) ==
  /\ m \in pending
  /\ m.kind = "SUCCESS"
  /\ m.matchedThrough <= peerMatch[m.peer]
  /\ LET p == m.peer
         nm == IF m.matchedThrough > matchIndex[p]
               THEN m.matchedThrough ELSE matchIndex[p]
         nn == IF m.matchedThrough + 1 > nextIndex[p]
               THEN m.matchedThrough + 1 ELSE nextIndex[p]
     IN
       /\ matchIndex' = [matchIndex EXCEPT ![p] = nm]
       /\ nextIndex' = [nextIndex EXCEPT ![p] = nn]
       /\ badMatchRegression' =
            IF nm < matchIndex[p] THEN TRUE ELSE badMatchRegression
       /\ badPeerIsolation' = badPeerIsolation
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<peerMatch, handledKeys, badCursorBound,
                 badFailureDuplicate>>
HandleFailure(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ m.requestId = 2
  /\ (IF DeduplicateFailures THEN Key(m) \notin handledKeys ELSE TRUE)
  /\ LET p == m.peer
         nn == IF ClampFailures
               THEN (IF nextIndex[p] - 1 > matchIndex[p] + 1
                     THEN nextIndex[p] - 1 ELSE matchIndex[p] + 1)
               ELSE nextIndex[p] - 1
     IN
       /\ nextIndex' = [nextIndex EXCEPT ![p] = nn]
       /\ matchIndex' = matchIndex
       /\ handledKeys' = handledKeys \cup {Key(m)}
       /\ badFailureDuplicate' =
            IF Key(m) \notin handledKeys
            THEN badFailureDuplicate ELSE TRUE
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<peerMatch, badCursorBound, badMatchRegression,
                 badPeerIsolation>>
DiscardStaleFailure(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ m.requestId # 2
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<nextIndex, matchIndex, peerMatch, handledKeys,
                 badCursorBound, badMatchRegression, badPeerIsolation,
                 badFailureDuplicate>>
DiscardDuplicateFailure(m) ==
  /\ m \in pending
  /\ m.kind = "FAILURE"
  /\ m.requestId = 2
  /\ DeduplicateFailures
  /\ Key(m) \in handledKeys
  /\ pending' = pending \ {m}
  /\ UNCHANGED <<nextIndex, matchIndex, peerMatch, handledKeys,
                 badCursorBound, badMatchRegression, badPeerIsolation,
                 badFailureDuplicate>>
Next ==
  \/ \E m \in pending : HandleSuccess(m)
  \/ \E m \in pending : HandleFailure(m)
  \/ \E m \in pending : DiscardStaleFailure(m)
  \/ \E m \in pending : DiscardDuplicateFailure(m)
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
TypeOK ==
  /\ nextIndex \in [Peers -> 1..6]
  /\ matchIndex \in [Peers -> 0..4]
  /\ peerMatch \in [Peers -> 0..4]
  /\ pending \subseteq Messages
  /\ handledKeys \subseteq
       [term : {2}, leader : {"C"}, peer : Peers, requestId : 1..2]
  /\ badCursorBound \in BOOLEAN
  /\ badMatchRegression \in BOOLEAN
  /\ badPeerIsolation \in BOOLEAN
  /\ badFailureDuplicate \in BOOLEAN
CursorLowerBound == \A p \in Peers : nextIndex[p] >= matchIndex[p] + 1
NoMatchRegression == badMatchRegression = FALSE
NoPeerIsolationViolation == badPeerIsolation = FALSE
NoFailureDuplicateEffect == badFailureDuplicate = FALSE
CursorBoundMonitor ==
  badCursorBound = FALSE
EventuallyAllLatestFailuresHandled ==
  <> (\A p \in Peers : [term |-> 2, leader |-> "C", peer |-> p,
                        requestId |-> 2] \in handledKeys)
EventuallyPendingDrained == <> (pending = {})
====