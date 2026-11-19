---- MODULE WitnessV2OldSuccessAfterNewFailure ----
EXTENDS Naturals, TLC
CONSTANTS PeerPrefixStable, AllowRollback
VARIABLES nextIndex, matchIndex, peerMatch, pending, handled,
          badMatchBeyondPeer, badCursorBound
vars == <<nextIndex, matchIndex, peerMatch, pending, handled,
          badMatchBeyondPeer, badCursorBound>>
Init ==
  /\ nextIndex = 5
  /\ matchIndex = 1
  /\ peerMatch = 3
  /\ pending = {"NEW_FAILURE","OLD_SUCCESS"}
  /\ handled = {}
  /\ badMatchBeyondPeer = FALSE
  /\ badCursorBound = FALSE
HandleNewFailure ==
  /\ "NEW_FAILURE" \in pending
  /\ nextIndex' = IF nextIndex - 1 > matchIndex + 1
                   THEN nextIndex - 1 ELSE matchIndex + 1
  /\ pending' = pending \ {"NEW_FAILURE"}
  /\ handled' = handled \cup {"NEW_FAILURE"}
  /\ UNCHANGED <<matchIndex, peerMatch, badMatchBeyondPeer,
                  badCursorBound>>
HandleOldSuccess ==
  /\ "OLD_SUCCESS" \in pending
  /\ "NEW_FAILURE" \notin pending
  /\ (IF PeerPrefixStable THEN 3 <= peerMatch ELSE TRUE)
  /\ matchIndex' = IF 3 > matchIndex THEN 3 ELSE matchIndex
  /\ nextIndex' = IF 4 > nextIndex THEN 4 ELSE nextIndex
  /\ pending' = pending \ {"OLD_SUCCESS"}
  /\ handled' = handled \cup {"OLD_SUCCESS"}
  /\ badMatchBeyondPeer' =
       IF matchIndex' > peerMatch THEN TRUE ELSE badMatchBeyondPeer
  /\ UNCHANGED <<peerMatch, badCursorBound>>
RollbackPeer ==
  /\ AllowRollback
  /\ "NEW_FAILURE" \in handled
  /\ "OLD_SUCCESS" \in pending
  /\ peerMatch = 3
  /\ peerMatch' = 2
  /\ UNCHANGED <<nextIndex, matchIndex, pending, handled,
                  badMatchBeyondPeer, badCursorBound>>
Next ==
  \/ HandleNewFailure
  \/ HandleOldSuccess
  \/ RollbackPeer
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
TypeOK ==
  /\ nextIndex \in 1..6
  /\ matchIndex \in 0..4
  /\ peerMatch \in 0..4
  /\ pending \subseteq {"NEW_FAILURE","OLD_SUCCESS"}
  /\ handled \subseteq {"NEW_FAILURE","OLD_SUCCESS"}
  /\ badMatchBeyondPeer \in BOOLEAN
  /\ badCursorBound \in BOOLEAN
CursorLowerBound == nextIndex >= matchIndex + 1
MatchWithinPeerPrefix == matchIndex <= peerMatch
NoMatchBeyondPeerMonitor == badMatchBeyondPeer = FALSE
EventuallyBothResponsesHandled == <> (pending = {})
====