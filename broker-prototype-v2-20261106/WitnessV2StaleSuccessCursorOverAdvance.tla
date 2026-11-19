---- MODULE WitnessV2StaleSuccessCursorOverAdvance ----
EXTENDS Naturals, FiniteSets, TLC
CONSTANTS PeerLogStable
VARIABLES nextIndex, matchIndex, peerMatch, oldSuccessPending, retryAttempted, retrySucceeded
vars == <<nextIndex, matchIndex, peerMatch, oldSuccessPending, retryAttempted, retrySucceeded>>
OldSuccess == [requestId |-> 1, matchedThrough |-> 4]
Init ==
  /\ nextIndex = 4
  /\ matchIndex = 0
  /\ peerMatch = 4
  /\ oldSuccessPending = TRUE
  /\ retryAttempted = FALSE
  /\ retrySucceeded = FALSE
DeliverOldSuccess ==
  /\ oldSuccessPending
  /\ matchIndex' = IF OldSuccess.matchedThrough > matchIndex THEN OldSuccess.matchedThrough ELSE matchIndex
  /\ nextIndex' = IF OldSuccess.matchedThrough + 1 > nextIndex THEN OldSuccess.matchedThrough + 1 ELSE nextIndex
  /\ oldSuccessPending' = FALSE
  /\ UNCHANGED <<peerMatch, retryAttempted, retrySucceeded>>
PeerLogChange ==
  /\ ~PeerLogStable
  /\ peerMatch = 4
  /\ peerMatch' = 2
  /\ UNCHANGED <<nextIndex, matchIndex, oldSuccessPending, retryAttempted, retrySucceeded>>
RetryAppend ==
  /\ ~retryAttempted
  /\ nextIndex > 1
  /\ retryAttempted' = TRUE
  /\ IF nextIndex - 1 <= peerMatch
        THEN retrySucceeded' = TRUE
        ELSE retrySucceeded' = FALSE
  /\ UNCHANGED <<nextIndex, matchIndex, peerMatch, oldSuccessPending>>
Next ==
  \/ DeliverOldSuccess
  \/ PeerLogChange
  \/ RetryAppend
Spec == Init /\ [][Next]_vars /\ WF_vars(RetryAppend)
TypeOK ==
  /\ nextIndex \in 1..7
  /\ matchIndex \in 0..6
  /\ peerMatch \in 0..6
  /\ oldSuccessPending \in BOOLEAN
  /\ retryAttempted \in BOOLEAN
  /\ retrySucceeded \in BOOLEAN
CursorLowerBound == nextIndex >= matchIndex + 1
EventuallyRetrySucceeded == <> (retrySucceeded = TRUE)
====
