# STAGE-5-22-WORKER-CLOSE-DISPATCHER-FOUNDATION-20261118

## Changes
Only `integrated-submit-adapter-v2-api-test.c` changed.

- Added a test-only registry for up to four Worker-close callback states.
- Registry registration/unregistration is protected by a statically initialized SRW lock.
- Added `test_shared_worker_close`, which routes an exact HANDLE to the state whose explicit two-handle ownership table contains it.
- The callback increments the matched state's close count and arms only that state's Submitter ACK fault after its two expected closes.
- Unknown handles fail closed with `ERROR_NOT_FOUND`; no fallback to callback order or first registered state.
- Added a dispatcher isolation test that interleaves closes from two independent states and verifies each state's ACK fault is armed only after its own two handles are observed.
- Added the test to the API suite.

## Validation
- Build: `build-integrated-submit-adapter-v2-api-test.cmd`
- Toolchain: Visual Studio 2026 BuildTools 18.7.3, x64, `/W4 /WX`.
- Result: PASS, 10 consecutive executions.
- Each execution: 834 assertions across 66 cases.
- Total: 8,340 assertion executions.
- All 15 EXE/OBJ outputs declared by the build script were absent after script cleanup.
- No manual deletion was performed.
- Free space before build: C: 13,229,998,080 bytes; D: 4,810,432,512 bytes.
- Free space after build: C: 13,229,801,472 bytes; D: 4,810,432,512 bytes.

## Scope and limitations
This is a dispatcher foundation/unit test only. It does not yet capture real operation-created event and duplicated-pipe handles into each state's ownership table, configure a real shared Worker with this dispatcher, or run two independent Adapters against one Worker. The test does not prove callback/unregistration race safety: the required lifetime rule remains that a state must not be unregistered or leave scope until the Worker thread is joined and no callback can still execute.

No production source/header, build script, README, model, configuration, or production integration changed. No retained operation was fabricated and no quarantine was released.

## Remaining work
1. Populate each state's exact owned-handle table from the real Adapter create-event and duplicate-handle callbacks, with synchronized publication.
2. Configure the shared Worker once with the dispatcher and register both Adapter states before submissions.
3. Submit real named-pipe READ operations through two independent Adapter/Submitter pairs sharing one Worker; inject and observe two independent ACK failures.
4. After Worker thread termination, collect both Adapter snapshots and the common Worker snapshot, checking two distinct Adapter identities and two ADAPTER_ASSOCIATED Worker identities.
5. Prove callback-state lifetime and registration/unregistration synchronization under the actual Worker lifecycle.
6. Preserve quarantined resources; do not add test-only release or replay paths.

## Stage status
- 5.16 multi-Adapter retained runtime: BLOCKED / NOT COMPLETE.
- 5.17 direct managed-pipe retained identity runtime: PASS in prior recorded execution.
- 5.18 first-retain immutability: PARTIAL.
- 5.19 snapshot consistency: PARTIAL.
- 5.20 production enumeration/reconciliation API: draft only; NOT IMPLEMENTED.
- Stage 5 implementation/recovery acceptance: BLOCKED / NOT ACCEPTED.
- Phase 1: NOT ACCEPTED.
- Production: NO-GO.
