# STAGE-5-24-SHARED-WORKER-DUAL-ADAPTER-RUNTIME-EXPERIMENT-20261118

## Objective
Attempt a real runtime test with two independent Adapter/Submitter pairs sharing one Managed Worker, using real named-pipe READ operations and the exact-handle Worker close dispatcher.

## Runtime experiment result
The first attempt did **not** pass and is not accepted as proof of multi-Adapter shared-Worker support.

Observed during the experiment:
- Adapter A reached the shared Worker close callback twice; its two tracked handles were closed.
- Adapter A's ACK fault was injected and its Adapter retained count reached 1.
- Adapter B's Adapter retained count also reached 1, but its Worker close callback count remained 0, its ACK fault was not injected, and its two tracked handles remained present.
- The focused test therefore failed at the assertion that both Adapter states had two Worker close callbacks.

This is consistent with the first operation's ACK-failure/quarantine path degrading the shared Worker before Adapter B's operation can complete through the Worker. The observation does not, by itself, prove the exact internal causal ordering; it does establish that the attempted two-operation scenario did not produce two independent Adapter-associated Worker terminal paths.

The incomplete dual-Adapter test code and its diagnostic output were removed. No failed test was left in the normal suite. No retained resources were released or replayed.

## Post-experiment regression
- Build/test: `build-integrated-submit-adapter-v2-api-test.cmd`
- Visual Studio 2026 BuildTools 18.7.3, x64, `/W4 /WX`.
- PASS: 10 consecutive executions.
- Each execution: 841 assertions across 66 cases.
- Total: 8,410 assertion executions.
- All 15 declared EXE/OBJ build outputs absent after script cleanup.
- No manual artifact deletion.
- Free space after test: C: 13,212,086,272 bytes; D: 4,809,887,744 bytes.
- Fourteen older root-level EXE/OBJ artifacts remain preserved.

## Code disposition
No production source/header, build script, README, TLA+ model, or configuration change was made. The prior test-only exact-handle tracking and close-dispatcher foundation remains. The incomplete dual-Adapter runtime experiment was removed; the ordinary API suite is back to its passing baseline.

## Required next investigation
1. Trace exact Worker admission and quarantine ordering for Adapter B after Adapter A's ACK failure.
2. Determine whether both operations can be admitted and reach Worker terminal completion before either ACK failure is injected, without changing production semantics or weakening fault behavior.
3. If a deterministic scheduling seam is required, keep it test-only and avoid altering production lifecycle logic.
4. Re-run the two-Adapter test only after the scheduling and ownership conditions are justified from source.
5. Preserve the distinction between Adapter-only retained envelopes and Worker retained operations; a retained count of 1 in each Adapter is not proof of two Worker-associated retained operations.

## Stage status
- 5.16 multi-Adapter retained runtime: BLOCKED / NOT COMPLETE.
- 5.17 direct managed-pipe retained identity runtime: PASS in prior recorded execution.
- 5.18 first-retain immutability: PARTIAL.
- 5.19 snapshot consistency: PARTIAL.
- 5.20 production enumeration/reconciliation API: draft only; NOT IMPLEMENTED.
- Stage 5 implementation/recovery acceptance: BLOCKED / NOT ACCEPTED.
- Phase 1: NOT ACCEPTED.
- Production: NO-GO.
