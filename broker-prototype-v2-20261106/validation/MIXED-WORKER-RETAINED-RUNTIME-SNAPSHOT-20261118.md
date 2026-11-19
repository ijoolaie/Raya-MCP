# Mixed DIRECT / ADAPTER_ASSOCIATED Runtime Snapshot Validation — 2026-11-18

## Scope
Extended the existing Adapter retained-path fault test to place a separately initialized and identity-registered DIRECT operation into the same live Worker retained list as the Adapter-associated operation. After the existing lifecycle reaches Worker join and Adapter admission closure, the test-only snapshot API captures both entries from the actual Worker list.

The DIRECT record is removed from the list under the Worker lock after snapshot assertions, and its two test handles are closed. The original Adapter-associated quarantined operation is not released or modified.

## File changed
- `integrated-submit-adapter-v2-api-test.c`

No production source/header, build script, README, model, configuration, or Production artifact changed.

## Assertions added
- Create two valid test handles and initialize a real managed-operation record.
- Register the record as DIRECT using the Worker registration API.
- Publish it through the common Worker retained-list publication helper.
- Verify the Worker retained count becomes two alongside the existing Adapter-associated retained operation.
- Capture a two-entry Worker diagnostic snapshot through `raya_adapter_test_retained_snapshot`.
- Verify the snapshot contains both DIRECT and ADAPTER_ASSOCIATED records.
- Verify both records carry valid Worker identity in the same Worker epoch.
- Verify DIRECT has no Adapter association.
- Verify ADAPTER_ASSOCIATED has a complete Adapter association.
- Join Adapter and Worker membership using the stable Adapter instance/operation pair, not list position or pointer ordering.
- Preserve the Adapter record's existing terminal/finalizer/submitter assertions.
- Detach only the test-owned DIRECT record after snapshot validation; close its test handles. The pre-existing quarantined Adapter record remains retained.

## Validation
- Standard API test build: PASS, 10 consecutive runs, 801 assertions / 66 cases per run (8,010 assertion executions).
- Test-hook API build with `/DRAYA_ADAPTER_TESTING`: PASS, 10 consecutive runs, 994 assertions / 68 cases per run (9,940 assertion executions).
- Both compiler invocations used `/W4 /WX`; both completed successfully.
- API script declared outputs absent after cleanup.
- Test-hook build outputs explicitly removed after the 10-run suite.
- All declared EXE/OBJ outputs absent after validation.
- 14 pre-existing root EXE/OBJ artifacts preserved untouched.
- Final free space: C 14,114,615,296 bytes; D 4,810,514,432 bytes.

## Retained-list publication audit
- Worker retained-list pointer insertion is centralized in `raya_managed_worker_publish_retained_locked` (`reaper-managed-worker-v2.c`).
- Direct managed-pipe call sites route through that helper (`managed-pipe-io-v2.c`).
- Adapter-associated Worker publication routes through that helper (`integrated-submit-adapter-v2.c`).
- Adapter envelope retention has its own separate linked-list insertion sites in `integrated-submit-adapter-v2.c`; these are not Worker retained-list bypasses.
- No additional raw Worker retained-list insertion site was found by source search.

## Execution note
During test construction, an initial run exposed assumptions that the Adapter-associated item would always be Worker snapshot index zero. The test was corrected to locate it by stable Adapter identity pair. A separate single-entry test's pointer-diagnostic assertion was kept scoped to that single-entry case. Final standard and test-hook suites both passed all ten runs.

## Remaining work
- Add multi-operation runtime coverage for two or more Adapter-associated records from independent submissions, where deterministic retention can be induced without fabricating ownership state.
- Verify actual direct managed-pipe I/O retained-path identity and snapshot propagation, not only registration/publication of a test-owned DIRECT record.
- Continue first-retain cause/stage immutability checks through actual runtime publication paths.
- Positive recovery/reconciliation remains blocked until production enumeration and narrow typed reconciliation APIs exist. No release, retry, replay, or recovery claim is made.

## Acceptance
- Stage 5 review/reporting checklist 5.1–5.15 remains complete.
- Stage 5 implementation/recovery acceptance: BLOCKED / NOT ACCEPTED.
- Phase 1: NOT ACCEPTED.
- Production: NO-GO.
- No commit or reset performed.
