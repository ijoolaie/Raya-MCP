---- MODULE WitnessV2AppendEntriesThreeResponseStaleTermGuard ----
EXTENDS Naturals, FiniteSets, TLC
CONSTANT UseTermGuard

Leader == "LEADER"
Follower == "FOLLOWER"

VARIABLES currentTerm, role, lastRequestId, nextIndex, matchIndex,
          messages, handled
vars == <<currentTerm, role, lastRequestId, nextIndex, matchIndex,
          messages, handled>>

Init ==
  /\ currentTerm = 1
  /\ role = Leader
  /\ lastRequestId = 3
  /\ nextIndex = 2
  /\ matchIndex = 0
  /\ messages = {[kind |-> "APPEND_RESP", term |-> 1,
                  requestId |-> 1, success |-> FALSE, matchIndex |-> 0]}
  /\ handled = FALSE

NewLeadershipTerm ==
  /\ currentTerm = 1
  /\ role = Leader
  /\ currentTerm' = 2
  /\ role' = Leader
  /\ lastRequestId' = 1
  /\ UNCHANGED <<nextIndex, matchIndex, messages, handled>>

HandleResponse(m) ==
  /\ m \in messages
  /\ m.kind = "APPEND_RESP"
  /\ role = Leader
  /\ IF UseTermGuard THEN m.term = currentTerm ELSE TRUE
  /\ IF m.success
       THEN
         /\ matchIndex' = IF m.matchIndex > matchIndex
                            THEN m.matchIndex ELSE matchIndex
         /\ nextIndex' = IF m.matchIndex + 1 > nextIndex
                           THEN m.matchIndex + 1 ELSE nextIndex
       ELSE
         /\ m.requestId = lastRequestId
         /\ nextIndex' = IF nextIndex > 1 THEN nextIndex - 1 ELSE 1
         /\ UNCHANGED matchIndex
  /\ messages' = messages \ {m}
  /\ handled' = TRUE
  /\ UNCHANGED <<currentTerm, role, lastRequestId>>

Next ==
  \/ NewLeadershipTerm
  \/ \E m \in messages : HandleResponse(m)

TypeOK ==
  /\ UseTermGuard \in BOOLEAN
  /\ currentTerm \in 1..2
  /\ role \in {Leader, Follower}
  /\ lastRequestId \in 0..3
  /\ nextIndex \in 1..3
  /\ matchIndex \in 0..2
  /\ messages \subseteq [kind : {"APPEND_RESP"}, term : 1..2,
                         requestId : 1..3, success : BOOLEAN,
                         matchIndex : 0..2]
  /\ handled \in BOOLEAN

StaleResponseCannotBeHandled ==
  currentTerm = 2 =>
    /\ messages # {}
    /\ handled = FALSE
    /\ nextIndex = 2

Spec == Init /\ [][Next]_vars
====
