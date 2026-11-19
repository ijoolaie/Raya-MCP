---- MODULE WitnessV2AppendEntriesRequestIdReuseEpochDiagnostic ----
EXTENDS WitnessV2AppendEntriesRequestIdReuseEpoch
Next == HandleWithoutTermGuard(CHOOSE m \in messages : TRUE)
Spec == Init /\ [][Next]_vars
====
