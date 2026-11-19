# Worker Retained Publication and Snapshot Metadata — 2026-11-18

## Scope
Implemented the next bounded Worker retained-identity slice in the Working Copy only:
`D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`.

## Changes
- `reaper-managed-worker-v2.h`
  - Added Worker first-retain stage enum: UNKNOWN, DIRECT, ADAPTER_ASSOCIATED, PARTIAL_RECORD, POST_TERMINAL.
  - Added immutable first-retain cause/stage fields to each managed operation.
  - Declared a Worker-lock-held retained publication helper.
  - Extended the test-only Worker retained snapshot record additively. The legacy pointer diagnostic remains; a separately named address diagnostic and copied identity metadata are now exposed.
- `reaper-managed-worker-v2.c`
  - Added `raya_managed_worker_publish_retained_locked`.
  - It rejects duplicate pointer membership, validates complete identity tuples when present, accepts explicitly identity-less partial records only when all identity fields are zero/unspecified, stamps first-retain evidence once, and links/counts exactly once.
  - The helper does not acquire locks, allocate Worker IDs, close handles, free memory, invoke callbacks, release leases, or call Adapter code.
- `managed-pipe-io-v2.c`
  - Routed direct Worker retained publication through the helper.
  - Initialized-but-identity-less records are classified as PARTIAL_RECORD.
- `integrated-submit-adapter-v2.c`
  - Routed the Adapter-associated Worker-list insertion through the same helper.
  - Uses POST_TERMINAL for post-terminal publication, ADAPTER_ASSOCIATED for identified Adapter operations, and PARTIAL_RECORD for identity-less records.
  - Adapter envelope retention remains a separate inventory and is unchanged.
- `integrated-submit-adapter-v2.h`
  - Extended test-only Worker snapshot metadata while retaining the legacy pointer field for compatibility.

## Validation
- `build-managed-pipe-io-v2-native-test.cmd`: PASS, 10 runs × 88 assertions = 880 assertion executions.
- `build-integrated-submit-adapter-v2-api-test.cmd`: PASS, 10 runs × 751 assertions = 7,510 assertion executions.
- After the final direct-stage classification adjustment, the native test was rerun: PASS, 10 × 88.
- Compiler used the existing Visual Studio 2026 Build Tools environment; no installation/download performed.
- All declared EXE/OBJ outputs from both scripts were absent after cleanup. No manual deletion performed.
- C: 14,127,259,648 bytes free; D: 4,811,100,160 bytes free at the pre-build check.
- No new identity-specific assertions have yet been added; existing suites validate compilation and regression behavior, not the full new metadata contract.

## Remaining gaps
- Add identity-specific tests for DIRECT and ADAPTER_ASSOCIATED tuples, multiple Workers/operations, partial records, duplicate publication, and first-retain immutability.
- Update cross-plane validators to compare stable Worker identity where valid while preserving pointer diagnostics only for membership/address diagnostics.
- Add safe test seam coverage for sequence exhaustion if feasible.
- No production enumeration, reconciliation, release, retry, replay, or recipient receipt API was added.

## Boundaries
No README, build script, TLA+/configuration, or Production files changed. No Commit/Reset. No manual artifact deletion. Phase 1 remains NOT ACCEPTED; Production remains NO-GO.
