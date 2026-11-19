---- MODULE WitnessV2AppendEntriesRequestIdReuseEpoch ----
EXTENDS Naturals, FiniteSets, TLC
CONSTANT UseTermGuard
VARIABLES currentTerm, role, lastRequestId, nextIndex, matchIndex, messages, handled
vars == <<currentTerm, role, lastRequestId, nextIndex, matchIndex, messages, handled>>
Leader == "LEADER"
Follower == "FOLLOWER"
Init ==
  /\ currentTerm = 2
  /\ role = Leader
  /\ lastRequestId = 1
  /\ nextIndex = 2
  /\ matchIndex = 0
  /\ messages = {[kind |-> "APPEND_RESP", term |-> 1,
                   requestId |-> 1, success |-> FALSE]}
  /\ handled = FALSE
HandleResponse(m) ==
  /\ m \in messages
  /\ IF UseTermGuard THEN m.term = currentTerm ELSE TRUE
  /\ role = Leader
  /\ IF m.success
        THEN /\ nextIndex' = IF nextIndex > matchIndex + 1 THEN nextIndex ELSE matchIndex + 1
             /\ UNCHANGED matchIndex
        ELSE /\ m.requestId = lastRequestId
             /\ nextIndex' = IF nextIndex > 1 THEN nextIndex - 1 ELSE 1
             /\ UNCHANGED matchIndex
  /\ messages' = messages \ {m}
  /\ handled' = TRUE
  /\ UNCHANGED <<currentTerm, role, lastRequestId>>
Next == \E m \in messages : HandleResponse(m)
Spec == Init /\ [][Next]_vars
TypeOK ==
  /\ UseTermGuard \in BOOLEAN
  /\ currentTerm \in 0..2
  /\ role \in {Leader, Follower}
  /\ lastRequestId \in 0..1
  /\ nextIndex \in 1..3
  /\ matchIndex \in 0..2
  /\ messages \subseteq [kind : {"APPEND_RESP"}, term : 0..2,
                            requestId : 1..1, success : BOOLEAN]
  /\ handled \in BOOLEAN
StaleResponseHasNoEffect == nextIndex = 2
====
