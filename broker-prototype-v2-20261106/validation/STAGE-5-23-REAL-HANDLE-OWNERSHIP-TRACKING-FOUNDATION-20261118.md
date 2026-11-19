# STAGE-5-23-REAL-HANDLE-OWNERSHIP-TRACKING-FOUNDATION-20261118

## Changes
Only `integrated-submit-adapter-v2-api-test.c` changed.

- Added synchronized per-`api_state` tracking for the two handles created along the Adapter operation path:
  - completion event returned by `test_create_event`
  - duplicated pipe handle returned by `test_duplicate`
- Tracking uses the same exclusive SRW lock as the Worker-close registry. If the bounded two-slot ownership table cannot accept a handle, the newly created/duplicated handle is closed and the callback fails; it is not silently left untracked.
- Updated `test_shared_worker_close` to hold the registry lock through exact-handle lookup and `CloseHandle`, then clear that exact slot only after a successful close. This prevents an unregister/reuse window and avoids stale closed HANDLE values remaining in the table.
- The close dispatcher continues to fail closed for unknown handles. It increments the matched state's close count and arms only that state's ACK fault after two successfully closed owned handles.
- Strengthened the dispatcher test to register its handles through the same tracking helper and assert that each slot is cleared after its corresponding successful close.

## Validation
- Build/test: `build-integrated-submit-adapter-v2-api-test.cmd`
- Toolchain: Visual Studio 2026 BuildTools 18.7.3, x64, `/W4 /WX`.
- Result: PASS, 10 consecutive executions.
- Each execution: 841 assertions across 66 cases.
- Total: 8,410 assertion executions.
- All 15 EXE/OBJ outputs declared by the build script were absent after cleanup.
- No manual artifact deletion was performed.
- Fourteen older root-level EXE/OBJ artifacts were inspected and preserved unchanged.
- Free space after validation: C: 13,227,134,976 bytes; D: 4,810,424,320 bytes.

## Scope and limitations
This is the real-handle ownership-tracking and close-dispatcher foundation, with a unit test of registration, exact-handle routing, successful close, and slot clearing. The ordinary API suite passed.

It does **not** yet prove that two independently started Adapters/Submitters can safely share one live Worker, does not exercise two real named-pipe operations through the shared Worker callback, and does not establish a full callback-state lifetime/unregistration proof under concurrent Worker shutdown. Those runtime checks remain open. The test-hook build was not run in this pass.

No production source/header, build script, README, TLA+ model, configuration, or production integration changed. No retained resource was released, replayed, or fabricated.

## Remaining work
1. Add and run the real two-Adapter/one-Worker named-pipe READ integration test using the newly populated per-state handle ownership table.
2. Verify two independent ACK failures and two distinct Adapter-associated retained identities in the live snapshots, after quiescence and Worker termination.
3. Prove state registration/unregistration lifetime around the actual Worker thread; unregister only after join and no callback can remain.
4. If any lifecycle precondition cannot be demonstrated, keep the multi-Adapter runtime test blocked rather than infer safety from dispatcher unit tests.
5. Production enumeration/reconciliation remains a separate unimplemented contract; no release/replay API is added here.

## Stage status
- 5.16 multi-Adapter retained runtime: BLOCKED / NOT COMPLETE.
- 5.17 direct managed-pipe retained identity runtime: PASS in prior recorded execution.
- 5.18 first-retain immutability: PARTIAL.
- 5.19 snapshot consistency: PARTIAL.
- 5.20 production enumeration/reconciliation API: draft only; NOT IMPLEMENTED.
- Stage 5 implementation/recovery acceptance: BLOCKED / NOT ACCEPTED.
- Phase 1: NOT ACCEPTED.
- Production: NO-GO.
