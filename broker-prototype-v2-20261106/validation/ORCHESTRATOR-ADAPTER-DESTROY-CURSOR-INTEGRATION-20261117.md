# Orchestrator Adapter Destroy Cursor Integration — 2026-11-17

## Scope

Implemented only in the Raya MCP Working Copy:
`D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`

This slice connects the structured Adapter Destroy result to the existing generation-handle Registry and cleanup cursor. It does not invoke Submitter or Worker lifecycle operations.

## Implementation

Added `RAYA_ORCHESTRATOR_CLEANUP_STEP_ADAPTER_DESTROY` (cursor step 0) and `raya_orchestrator_destroy_adapter_owned`.

The facade:
1. Retrieves Context only through the exact cleanup owner after Registry admission is CLOSING and all Registry API leases have drained.
2. Reads the Adapter Destroy cursor and refuses to execute if the step is IN_PROGRESS, UNKNOWN, or BLOCKED.
3. Begins a cursor attempt before calling Adapter Destroy.
4. On structured Adapter SUCCESS, records COMMITTED + SUCCESS.
5. On BUSY or INVALID, records BLOCKED + UNCERTAIN. This is deliberate: Adapter admission may already have been irreversibly closed, so the outcome is not a no-effect retry.
6. Requires explicit `raya_orch_cursor_reconcile` based on fresh evidence before another attempt. The facade does not automatically retry a blocked operation.
7. On an already committed successful step, returns the terminal success result without calling Adapter Destroy again.

The facade never calls Submitter Shutdown/Join/Destroy or Worker lifecycle APIs. Adapter Destroy itself does not stop or destroy either component.

## Tests and verification

### Orchestrator facade integration
- MSVC x64 compile with `/W4 /WX`: PASS.
- 10 consecutive runs: PASS.
- Each run: 42 assertions, 0 failures.
- Aggregate: 420 assertions.
- Added test covers:
  - exact owner and drained Registry lease gate;
  - Adapter active-work BUSY result and irreversible admission-close effect;
  - BLOCKED + UNCERTAIN cursor recording;
  - rejection of blind second execution;
  - explicit evidence-based reconciliation after active count drains;
  - successful Adapter destruction and cursor COMMITTED state;
  - idempotent reporting of committed success without a second destroy.

### Regression suites
- Registry regression: 50 assertions, 0 failures.
- Adapter API suite: 751 assertions across 66 cases per run, 10 consecutive runs, all PASS (7,510 assertions).
- Combined assertions across the three executed suites: 7,980.
- All builds used MSVC x64 `/W4 /WX`.
- The Orchestrator build script now links the Adapter and its required component sources so the integration test exercises the real Adapter implementation, not stubs.

### Artifact/process inspection
- Build-script-owned EXE/OBJ artifacts remaining: 0.
- No `cl`, `link`, `java`, or `tlc` processes remained at final inspection.
- Unrelated pre-existing artifacts were not deleted.

## Changed files and SHA-256

- `raya-orchestrator-v1.h` — `A8286BD836D10435D3F24D5F667713D311F242B7545E7ED7760289157D5D09E7`
- `raya-orchestrator-v1.c` — `1902A68EACF03E7CED9F46C5B90B5786ACEF2B02F847CFCAC16C7F1A0BBF2A08`
- `raya-orchestrator-v1-test.c` — `F30CB4FD82C54D48AEBCDA430120412CF67EA67B216D1F7C395A063FAF6792E0`
- `build-raya-orchestrator-v1-test.cmd` — `A8505C9A391964CF8FE1134E3F9FA335355A86252EA06AF9086DC6569D57D767`

## Remaining constraints

- Submitter direct-caller quiescence is still an external precondition and has not been proven by this slice.
- No Submitter lifecycle operation is wired into the Orchestrator cursor yet.
- Worker teardown, startup rollback, chained failure matrix, and durable cross-process recovery remain open.
- Cursor and owner state are in-memory; no crash-durable recovery claim is made.
- Phase 1 remains NOT ACCEPTED; Production remains NO-GO.
- No Production/source-project files, README, IranPriceScout, or independent port 8765 tunnel were touched.
- No commit or reset was performed.

## Environment

Final observed free space:
- C: 11,349,905,408 bytes
- D: 2,111,688,704 bytes
