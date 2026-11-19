# Worker Retained Identity — Epoch and Sequence Implementation (2026-11-18)

## Implemented
- Added process-local monotonic Worker epoch allocation using InterlockedIncrement64; zero/negative result fails closed.
- Worker stores worker_epoch and next_operation_id.
- Managed operation stores operation_initialized, identity_valid, Worker epoch/operation ID, inventory kind, and optional Adapter association pair.
- Successful raya_managed_operation_init marks operation_initialized only after finalizer initialization succeeds. Its failure memset leaves the marker false.
- Added raya_managed_worker_register_operation. It requires an initialized Worker, successfully initialized operation, valid inventory kind, correct Adapter-pair presence/absence, no prior identity, zero prior IDs, nonzero Worker epoch, and non-exhausted sequence. Allocation and stamping occur under Worker lock.
- Direct managed-pipe path registers DIRECT identity after successful operation init and before reservation/issue.
- Adapter path registers ADAPTER_ASSOCIATED identity after successful operation init and before Worker admission, with the exact Adapter instance/operation pair.
- Pre-init and failed-init partial records remain identity-less and retain existing quarantine behavior.

## Files changed
- reaper-managed-worker-v2.h
- reaper-managed-worker-v2.c
- managed-pipe-io-v2.c
- integrated-submit-adapter-v2.c

No tests, build scripts, README, model, configuration, or Production files were changed.

## Validation
- build-managed-pipe-io-v2-native-test.cmd: PASS, 10 runs x 88 assertions = 880 assertion executions.
- build-integrated-submit-adapter-v2-api-test.cmd: first invocation compiled successfully and passed 3 runs (751 assertions each), then failed on run 4 at existing timing-sensitive assertion line 296 (`!raya_integrated_adapter_destroy(&a)`). This is a nondeterministic active-I/O timing assertion; no code change was made to mask it.
- Second invocation: PASS, 10 runs x 751 assertions = 7,510 assertion executions.
- Both scripts' declared EXE/OBJ outputs absent after completion; cleanup performed by the scripts. No manual deletion.
- Disk after tests: C free 14,130,966,528 bytes; D free 4,811,108,352 bytes.

## Not yet implemented
- Unified Worker-lock retained publication helper and duplicate-list protection.
- Worker first-retain cause/stage metadata and immutable first-retain semantics for Worker list items.
- Test-only snapshot expansion with copied stable identity fields; current pointer-based diagnostic snapshot is unchanged.
- Identity-specific tests for distinct Workers, multiple operations, direct/Adapter association, invalid combinations, duplicate registration, sequence exhaustion, and partial-record compatibility.
- Validator migration from pointer-address cross-plane matching to explicit stable identity fields.

## Status
This is a bounded implementation slice, not acceptance of Stage 5. Identity registration is live in both successful operation paths, but retained inventory evidence is not yet complete. Stage 5 implementation/recovery acceptance remains BLOCKED / NOT ACCEPTED. Phase 1 NOT ACCEPTED; Production NO-GO.
