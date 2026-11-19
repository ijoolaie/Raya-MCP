# Worker Identity Registration Contract Tests — 2026-11-18

## Scope
Added real registration-path tests for Worker operation identities. The tests now exercise the public initializer and registration API rather than only constructing identity fields manually.

## File changed
- `integrated-submit-adapter-v2-api-test.c`

No production source/header, build script, README, model, configuration, or Production artifact changed.

## Added coverage
- Initialize two real operation records using distinct valid event handles.
- Register one as DIRECT with no Adapter association.
- Register one as ADAPTER_ASSOCIATED with a nonzero Adapter instance/operation pair.
- Verify both receive the active Worker's epoch.
- Verify Worker operation IDs are distinct and monotonically allocated.
- Verify inventory kind and Adapter association fields are correctly stamped.
- Reject duplicate registration of an already identity-bearing operation.
- Reject DIRECT registration when Adapter identity arguments are supplied.
- Close all four test handles after Worker shutdown/join/destroy.

## Validation
- Before build: C free 14,122,229,760 bytes; D free 4,810,526,720 bytes.
- API test: PASS, 10 consecutive runs, 785 assertions / 66 cases per run (7,850 assertion executions).
- Native managed-pipe test: PASS, 10 consecutive runs, 88 assertions per run (880 assertion executions).
- Both build scripts exited 0.
- All declared EXE/OBJ outputs from both scripts absent after their cleanup.
- 14 pre-existing root EXE/OBJ artifacts remain preserved and untouched.
- After validation: C free 14,120,013,824 bytes; D free 4,810,526,720 bytes.

## Execution note
An initial API-test compile exposed a test-fixture cleanup insertion outside its intended function scope. The test source was corrected, then both full 10-run suites completed successfully. No production code was affected by that correction.

## Remaining work
- Exercise actual runtime snapshots containing multiple retained operations and mixed DIRECT / ADAPTER_ASSOCIATED membership; current registration tests validate assignment but do not create a multi-entry live retained snapshot.
- Continue verifying identity propagation through each retain insertion path and snapshot-copy path.
- Positive recovery/reconciliation remains unavailable without production enumeration and typed reconciliation APIs; do not infer recovery acceptance from these tests.

## Acceptance
- Stage 5 review/reporting checklist 5.1–5.15 remains complete.
- Stage 5 implementation/recovery acceptance: BLOCKED / NOT ACCEPTED.
- Phase 1: NOT ACCEPTED.
- Production: NO-GO.
- No commit or reset performed.
