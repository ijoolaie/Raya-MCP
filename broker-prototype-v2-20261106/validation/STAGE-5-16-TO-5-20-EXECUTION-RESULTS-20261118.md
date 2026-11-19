# Stage 5.16–5.20 Execution Results — 2026-11-18

## Scope and result

Executed the feasible parts of Stage 5.16–5.20 in the Working Copy. Outcomes are separated by evidence level; blocked work is not reported as complete.

## Files changed

- `managed-pipe-io-v2-native-test.c`
- `integrated-submit-adapter-v2-api-test.c`
- This report.

No production source/header, build script, README, model, configuration, or production artifact was changed. No commit/reset was performed.

## 5.16 — Multiple independent Adapter-associated retained submissions

**Status: BLOCKED / NOT COMPLETE.**

Inspected the existing ACK-failure injection path in `integrated-submit-adapter-v2-api-test.c`.

- The fault is injected once at the Submitter ACK signal boundary.
- The test verifies one real Adapter submission becomes retained.
- The Submitter is degraded after that failure, so the existing seam does not establish that a second independent Adapter submission can be admitted and retained through the same live Adapter/Submitter.
- No second record was fabricated or inserted to claim independent submission coverage.
- A later test-only attempt armed ACK failure on both independent Adapter/Submitter pairs and admitted one real READ through each before completion. Adapter A reached ACK failure; Adapter B remained at one issuer lease with zero Worker handle-close calls, so the shared Worker sequence still produced only one retained record.

A deterministic, ownership-correct second-retention injection seam or a separately supported independent Adapter/Submitter arrangement is still required.

## 5.17 — Actual DIRECT managed-pipe retained path

**Status: COMPLETE for the focused runtime identity assertions.**

Extended the existing `test_close_failure_retention` in `managed-pipe-io-v2-native-test.c`. The test submits a real direct managed-pipe READ and injects the existing second-close failure. Before Worker teardown it now asserts:

- The actual retained operation is initialized and has valid identity.
- Worker epoch is nonzero and matches the Worker instance.
- Worker operation ID is nonzero.
- Inventory kind is DIRECT.
- Adapter association is absent and both Adapter IDs are zero.
- First-retain cause is `RAYA_RECOVERY_CAUSE_HANDLE_CLOSE`.
- First-retain stage is `RAYA_WORKER_RETAIN_STAGE_DIRECT`.

This validates the actual direct I/O retention path's in-memory identity and first-retain metadata. It does not claim a production enumeration API or a durable snapshot/recovery API.

## 5.18 — First-retain cause/stage immutability

**Status: PARTIALLY COMPLETE.**

- The actual DIRECT close-failure runtime test verifies the first cause/stage assigned by the real path.
- The API test now republishes the same test-owned operation through the common Worker publication helper with a different cause/stage and asserts duplicate publication is rejected and the original cause/stage remain unchanged.
- The helper checks duplicate list membership before mutating evidence, so repeated publication cannot rewrite the original evidence in this tested case.

Added a concurrent same-operation duplicate-publication race test: two Windows threads contend under the Worker-lock contract, exactly one publication succeeds, and the winner's first cause/stage remain intact.
Not established: two distinct runtime operations concurrently retained through separate real I/O paths, or all distinct runtime retain branches. Those require separate focused tests.

## 5.19 — Snapshot consistency

**Status: PARTIALLY COMPLETE; previous coverage retained.**

Existing evidence remains:
- Test-only stable-identity validator covers duplicate identities, epoch consistency, DIRECT and ADAPTER_ASSOCIATED inventory shapes, and Adapter/Worker stable-pair joins.
- Existing mixed runtime snapshot contains one actual Adapter-associated retained operation and one separately registered/test-published DIRECT operation in the same Worker list.

This pass adds the actual DIRECT I/O retention identity assertions. It does not add two independent Adapter-associated runtime submissions; that remains blocked under 5.16. The mixed test's DIRECT entry is not claimed to be a second real direct I/O submission.

## 5.20 — Recovery/Reconciliation contract

**Status: CONTRACT BASELINE EXISTS; PRODUCTION API CONTRACT/IMPLEMENTATION NOT COMPLETE.**

Re-read:
- `WORKER-RETAINED-IDENTITY-CONTRACT-FREEZE-20261118.md`
- `WORKER-RETAINED-IDENTITY-IMPLEMENTATION-READINESS-20261118.md`

The frozen design distinguishes Worker identity from Adapter identity, defines DIRECT versus ADAPTER_ASSOCIATED inventory, optional Adapter association, immutable first-retain evidence, and test-only copied snapshots. It explicitly excludes production enumeration, release, reconciliation, retry, and replay.

The actual production enumeration/reconciliation/release API contract still needs a separately approved and reviewed specification, including authorization, state transitions, ownership, concurrency, and fail-closed behavior. No production API was added in this pass.

## Validation

### Standard Adapter API regression
- Build: PASS with Visual Studio 2026 BuildTools 18.7.3, `/W4 /WX`.
- 10 consecutive runs PASS.
- 811 assertions per run, 66 cases per run.
- 8,110 assertion executions.

### Adapter test-hook regression
- Explicit `/DRAYA_ADAPTER_TESTING` build: PASS with `/W4 /WX`.
- 10 consecutive runs PASS.
- 1,004 assertions per run, 68 cases per run.
- 10,040 assertion executions.

### Direct managed-pipe native regression
- Build: PASS with `/W4 /WX`.
- 10 consecutive runs PASS.
- 97 assertions per run, zero failures.
- 970 assertion executions.

### Artifact and disk audit
- All declared EXE/OBJ outputs for the two script builds and the test-hook build were absent after cleanup.
- Test-hook outputs were removed by exact declared filenames only.
- No unrelated files were deleted.
- No build script was modified.
- Free space after validation: C: 14,117,011,456 bytes; D: 4,810,502,144 bytes.

## Remaining work

1. 5.16: create a deterministic, non-fabricated way to retain two independent Adapter-associated submissions and validate both stable identity pairs in one runtime snapshot.
2. 5.18: distinct-operation concurrent retention coverage and remaining runtime retain branches are still outstanding. Real Adapter-associated first-retain immutability is now covered for the existing ACK-quarantine path; same-operation duplicate race remains covered.
3. 5.19: extend actual runtime snapshot coverage after 5.16 is solved; keep synthetic validator coverage distinct from runtime evidence.
4. 5.20: specify and review the production Recovery/Reconciliation API contract. No release/retry/replay behavior is authorized by the current test-only identity layer.
5. Only after the above, proceed to positive recovery tests and Stage 5 acceptance.

## Acceptance

- 5.16: BLOCKED / NOT COMPLETE.
- 5.17: focused actual DIRECT retained-path identity assertions PASS.
- 5.18: PARTIAL.
- 5.19: PARTIAL.
- 5.20: existing identity contract baseline; production Recovery/Reconciliation API contract NOT COMPLETE.
- Stage 5 implementation/recovery acceptance: BLOCKED / NOT ACCEPTED.
- Phase 1: NOT ACCEPTED.
- Production: NO-GO.

## Addendum — 2026-11-19 real DIRECT first-retain immutability check

Extended the existing real test_close_failure_retention runtime path in managed-pipe-io-v2-native-test.c. After the actual managed-pipe READ reaches Worker retention through the injected second-handle-close failure, the test now attempts a second publication of that same retained operation under the Worker lock using a different cause and POST_TERMINAL stage.

Observed result: duplicate publication is rejected; retained-list head and count remain unchanged; the original HANDLE_CLOSE / DIRECT first-retain cause and stage remain unchanged. This is a runtime-created retained operation, not a fabricated test record.

Verification:
- build-managed-pipe-io-v2-native-test.cmd, Visual Studio 2026 BuildTools 18.7.3 x64, /W4 /WX: PASS.
- 10 consecutive executions: PASS.
- 101 assertions per execution; 1,010 assertion executions total.
- The first invocation was made from the repository root, where the relative-source build script cannot resolve its inputs; it failed before compilation and created no artifacts. The prescribed rerun from broker-prototype-v2-20261106 passed and its script removed all declared EXE/OBJ outputs.

This closes one real-runtime immutability gap only. Stage 5.18 remains PARTIAL: the same-operation duplicate-publication race is now covered, but distinct-operation runtime retain races and other runtime retain branches are not yet covered. Stage 5.16 remains BLOCKED; this test does not manufacture a second Adapter-associated retained submission. No production source/header, build script, model, configuration, or recovery behavior changed. No retained resource was released, retried, replayed, or deleted. Phase 1 remains NOT ACCEPTED; Production remains NO-GO.

## Addendum — 2026-11-19 concurrent first-publication race

Added `test_concurrent_first_retain_publication` to `managed-pipe-io-v2-native-test.c`. Two independent Windows threads are released by one start event and concurrently attempt to publish the same initialized, identity-bearing DIRECT operation while following the required Worker-lock contract. The contenders use different cause/stage pairs. The test verifies both threads complete, exactly one publication succeeds, the other is rejected as a duplicate, the Worker list contains exactly the single operation, and the stored first-retain cause/stage match the winning contender. The test removes only its own stack-backed synthetic record under the Worker lock before normal Worker shutdown; it does not touch a production retained resource.

Verification:
- `build-managed-pipe-io-v2-native-test.cmd`, Visual Studio 2026 BuildTools 18.7.3 x64, `/W4 /WX`: PASS.
- 10 consecutive executions: PASS.
- 120 assertions per execution; 1,200 assertion executions total.
- The build script removed its declared EXE/OBJ outputs.

This adds deterministic concurrent duplicate-publication coverage for the shared Worker helper. It does not establish two distinct runtime operations racing from separate real I/O retain paths, nor does it cover all retain branches. Stage 5.18 remains PARTIAL. Stage 5.16 remains BLOCKED; this test is intentionally a same-operation duplicate race and is not evidence of two independent Adapter-associated retained submissions. Stage 5.19 and 5.20 remain as stated above. No production source/header, build script, model, configuration, or recovery behavior changed. No retained resource was released, retried, replayed, or deleted. Phase 1 remains NOT ACCEPTED; Production remains NO-GO.

## Addendum — 2026-11-19 dual-Adapter / shared-Worker retention attempt

A focused test-only experiment changed the existing shared-Worker scenario to arm ACK-signal failure independently for both Adapters, submit one real READ through each Adapter before completing either pipe, and then complete both operations. The experiment did not establish two retained records. Observed diagnostic at the failing assertion: Adapter A reached its two Worker handle-close calls and ACK failure injection (`close=2`, `injected=1`, `issuer_leases=0`); Adapter B had no Worker handle-close calls and no ACK failure injection (`close=0`, `injected=0`) while its Submitter still held one issuer lease. The test stopped at this assertion. It therefore provides no evidence of a second Adapter-associated retained operation.

The experimental test source was restored exactly; no production source/header or build script was changed, no generated artifact remained, and the Working Copy was clean after restoration. This result narrows the current test limitation: the first Adapter's ACK-failure path prevents this attempted shared-Worker sequence from reaching the second Adapter's close/ACK path. It does not prove that every possible supported arrangement is impossible, but a different deterministic, ownership-correct runtime arrangement or an explicitly reviewed test seam is still needed. No synthetic retained record was used. Stage 5.16 remains BLOCKED / NOT COMPLETE; no release, retry, replay, or cleanup behavior was introduced. Phase 1 remains NOT ACCEPTED; Production remains NO-GO.

## Addendum - 2026-11-19 real Adapter-associated first-retain immutability

Extended the existing test_shared_worker_two_adapter_success_then_ack_quarantine scenario in integrated-submit-adapter-v2-api-test.c. After the real Adapter B READ reaches Worker retention through the injected Submitter ACK failure, the test captures the actual retained operation and its original first-retain cause/stage. Under the Worker lock it attempts duplicate publication of that same operation with a different recovery cause and POST_TERMINAL stage.

The test verifies duplicate publication is rejected, the Worker retained-list head/count remain unchanged, and the original first-retain cause/stage remain unchanged. The retained operation is produced by the real Adapter/Worker execution path; no synthetic retained record is introduced.

Verification:
- build-integrated-submit-adapter-v2-api-test.cmd, Visual Studio 2026 BuildTools 18.7.3 x64, /W4 /WX: PASS.
- 10 consecutive executions: PASS.
- 981 assertions per execution across 66 cases; 9,810 assertion executions total.
- The first build attempt exposed a signed/unsigned comparison warning treated as an error; the test-local variable type was corrected. The successful build completed without warnings/errors.
- git diff --check: PASS.

This closes one real Adapter-associated first-retain immutability gap for the ACK-quarantine path only. It does not establish two independent Adapter-associated retained submissions on one shared Worker, distinct-operation concurrent runtime retention, or full runtime Snapshot/Recovery coverage. Stage 5.16 remains BLOCKED; Stage 5.18 remains PARTIAL; Stage 5.19 remains PARTIAL; Stage 5.20 remains incomplete. No Production source/header, build script, recovery behavior, or cleanup semantics changed. No retained resource was released, retried, replayed, or deleted. Phase 1 remains NOT ACCEPTED; Production remains NO-GO.
## Addendum - 2026-11-19 shared-Worker fail-closed sequencing analysis

A focused source-path review explains the observed dual-Adapter attempt without broadening the test result into a claim of universal impossibility. In `integrated-submit-adapter-v2.c`, the first newly retained Adapter envelope publishes its associated Worker operation (when ownership has reached the applicable terminal boundary) and then calls `raya_reaper_mark_degraded_cause` on the shared Worker lifecycle. In `reaper-managed-worker-v2.c`, `managed_worker_main` checks the Worker lifecycle under `w->lock` at the beginning of each loop and returns `ERROR_INVALID_STATE` immediately when it is `RAYA_REAPER_DEGRADED`; it also checks for DEGRADED after a finite test wait. Therefore, after the first Adapter ACK quarantine degrades the shared Worker, the Worker thread will not process another queued operation on that Worker. This is consistent with the observed second Adapter remaining at one issuer lease and zero Worker close calls.

This is a code-path explanation of the current architecture and the observed ordering, not proof that all conceivable two-record states are impossible. The current supported Worker loop is fail-closed on the first degraded lifecycle, so manufacturing a second retained record or bypassing the degraded gate would invalidate the ownership/lifecycle evidence. No such bypass is approved. A legitimate route to two records would require a separately reviewed architecture/contract change that permits independent operation completion after one operation is quarantined, or another existing supported path that does not degrade the shared Worker on the first retain. Neither is evidenced in the current implementation. Accordingly, 5.16 remains BLOCKED pending owner-level design decision or new supported evidence; no Production behavior was changed.

Verification after the source-path review:
- `build-integrated-submit-adapter-v2-api-test.cmd`, Visual Studio 2026 BuildTools 18.7.3 x64, `/W4 /WX`: PASS.
- 10 consecutive executions, 981 assertions per execution across 66 cases; 9,810 assertion executions total.
- Build script completed successfully and removed its declared EXE/OBJ outputs.
- `git diff --check`: PASS.

Stage 5.16 remains BLOCKED. Stage 5.18 remains PARTIAL; Stage 5.19 remains PARTIAL; Stage 5.20 remains incomplete. No Production source/header, build script, model, configuration, recovery behavior, or cleanup semantics changed. No retained resource was released, retried, replayed, or deleted. Phase 1 remains NOT ACCEPTED; Production remains NO-GO.
## Addendum - 2026-11-19 snapshot evidence boundary reconciliation

A cross-report review reconciled the earlier successful `STAGE-5-SHARED-WORKER-DUAL-ADAPTER-RUNTIME-RESULTS-20261118.md` with the later dual-submit-before-completion experiment. They exercise different orderings and are not contradictory: the established successful ordering completes Adapter A fully before submitting Adapter B, then retains B after B's ACK failure. The later experiment submitted both operations before completing either; it failed because the first ACK quarantine degraded the shared Worker before the second operation reached Worker completion. Both findings remain valid. Neither establishes two independent Adapter-associated retained records.

The existing 5.19 evidence has three distinct levels that must not be conflated:
1. The test-hook mixed Worker snapshot captures one actual Adapter-associated retained operation plus a separately registered/published test-owned DIRECT record. The DIRECT record is not a real direct I/O completion.
2. The controlled shared-Worker runtime test captures a test-hook snapshot of the actual retained Adapter B operation and validates its Adapter/Worker stable identity pair and terminal ownership fields.
3. The production copied v1 inventory APIs have focused contract tests for quiescence, capacity, version, record shape, identity, and non-mutation, but the current reports do not establish a production copied-inventory snapshot of a real DIRECT I/O retained operation or a combined production cross-plane snapshot.

Therefore 5.19 remains PARTIAL. The next safe evidence target is to exercise the production Worker copied-inventory API against a real DIRECT retained operation after its Worker is joined and quiescent, if the current API's documented lifetime/quiescence preconditions permit that exact runtime. Do not inject a synthetic DIRECT record, mutate the retained list to manufacture coverage, or treat the test-hook snapshot as production inventory evidence. A combined cross-plane production snapshot is not currently implemented and is not inferred from the separate Adapter and Worker APIs. The two-Adapter retained multiplicity requirement remains blocked independently under 5.16.

No source, production behavior, resource ownership, cleanup, retry, or replay behavior changed in this reconciliation. Stage 5.16 remains BLOCKED; 5.19 remains PARTIAL; Stage 5 and Phase 1 remain NOT ACCEPTED; Production remains NO-GO.
## Addendum - 2026-11-19 production Worker inventory on real Adapter runtime

Extended `test_shared_worker_two_adapter_success_then_ack_quarantine` after the actual Adapter B ACK-quarantine path reaches Worker join. The test now calls the production `raya_managed_worker_copy_retained_inventory_v1` API against the joined, quiescent Worker and copies its one actual retained Adapter-associated operation into caller-owned v1 storage. It verifies successful status, required count, struct size/version, identity validity, inventory kind, Worker epoch/operation ID, Adapter identity pair, and original first-retain cause/stage. The existing test-hook cross-plane snapshot assertions remain separate and unchanged.

Verification:
- `build-integrated-submit-adapter-v2-api-test.cmd`, Visual Studio 2026 BuildTools 18.7.3 x64, `/W4 /WX`: PASS.
- 10 consecutive executions: PASS.
- 994 assertions per execution across 66 cases; 9,940 assertion executions total.
- Build script completed successfully and removed its declared EXE/OBJ outputs.
- `git diff --check`: PASS.

This is now production copied Worker-inventory evidence for one real Adapter-associated retained runtime record. It is not production Adapter+Worker atomic cross-plane snapshot evidence, does not cover a real DIRECT I/O retained record through this API, and does not establish two retained Adapter submissions. Stage 5.19 remains PARTIAL; Stage 5.16 remains BLOCKED; Stage 5.18 remains PARTIAL; Stage 5.20 remains incomplete. No Production source/header or lifecycle/recovery behavior changed. No retained resource was released, retried, replayed, or deleted. Phase 1 remains NOT ACCEPTED; Production remains NO-GO.

## 2026-11-19 — Production copied Worker inventory on a real DIRECT retained I/O operation

Extended `managed-pipe-io-v2-native-test.c` in the existing `test_close_failure_retention` scenario. The scenario submits a real managed-pipe READ, reaches the actual DIRECT retained path through the configured second-handle-close failure, and verifies the original first-retain evidence. After `raya_managed_worker_join` succeeds and the Worker reports `joined == TRUE` and `thread == NULL`, the test calls the production `raya_managed_worker_copy_retained_inventory_v1` API using caller-owned storage.

The copied v1 record is checked for exactly one entry, struct size/version, valid identity, DIRECT inventory kind, matching Worker epoch, nonzero Worker operation ID, absent Adapter identity/pair, `HANDLE_CLOSE` first-retain cause, and DIRECT first-retain stage. No synthetic record, registration helper, or test hook is used for this retained operation. The API is exercised against the actual retained operation created by the managed-pipe READ runtime path.

Verification: `build-managed-pipe-io-v2-native-test.cmd` using Visual Studio 2026 BuildTools 18.7.3 x64 and `/W4 /WX`; 10 consecutive executions, 132 assertions per execution (1,320 total), all passed. The build script cleaned its declared artifacts. `git diff --check` passed. No Production source/header or recovery behavior changed.

This closes the specific evidence gap for a production copied Worker-inventory snapshot of a real DIRECT retained I/O operation after Join/Quiescence. It does not implement a combined atomic Adapter+Worker snapshot, prove two independent Adapter-associated retained submissions on one shared Worker, authorize reconciliation/release/retry/replay, or change any Gate E/F or acceptance status. Stage 5 remains incomplete/not accepted; Phase 1 remains not accepted; Production remains NO-GO.

## Addendum - 2026-11-19 Stage 5.26 paired-snapshot lifecycle blocker

The existing success-A / ACK-failure-B test already proves the distinct supported ordering and captures B after Worker join through the production Worker inventory and test-only Adapter/Worker snapshot. It also proves A completed successfully and has zero retained/active envelopes. A paired post-quiescence Adapter snapshot for A is not supported by the current lifecycle API: the Adapter inventory API requires admission closed, but `raya_integrated_adapter_destroy_ex` is the only production admission-close operation and destroys/zeros an Adapter with no active or retained records. It cannot leave A initialized and closed for a later snapshot; the snapshot also requires the shared Worker joined. No close-only lifecycle API exists. Do not fabricate A retention, call inventory on a destroyed object, mutate counters, or introduce an unreviewed test seam. A new close-only contract would need separate review. This is a diagnostic limitation only; it does not alter 5.16's two-retained-submission blocker or authorize lifecycle/recovery changes.

## Addendum - 2026-11-19 production Adapter snapshots for both sides of success-A / ACK-failure-B

Extended `test_shared_worker_two_adapter_success_then_ack_quarantine` using existing test-injected Adapter API callbacks. After Adapter A's real READ succeeds, the test starts one additional real Adapter API entrant whose injected `create_event` callback is held. `raya_integrated_adapter_destroy_ex` is invoked while that entrant is active and returns BUSY with admission closed and one entrant. The callback is released and the attempted submission rejects before Issue; the entrant drains, leaving Adapter A initialized with closed admission and zero active/retained records. Adapter A is kept alive until after the shared Worker joins.

After both Submitters are shutdown/joined and their API admission gates are explicitly closed with zero entrants, and after the shared Worker is joined, the test calls the production Adapter v1 inventory API on both Adapters. Adapter A returns a valid zero-record snapshot. Adapter B returns exactly one copied record with its instance/operation identity, original first-retain cause, and SUBMITTER_ACK stage. The existing production Worker v1 snapshot still returns the matching real Adapter-associated Worker record. This demonstrates separate post-quiescence production Adapter inventories (A=0, B=1) plus the corresponding Worker inventory; it does not create a second retained Adapter submission and does not promise an atomic cross-plane view.

Verification: `build-integrated-submit-adapter-v2-api-test.cmd`, Visual Studio 2026 BuildTools 18.7.3 x64 `/W4 /WX`; 10 consecutive executions, 1,022 assertions per execution across 66 cases (10,220 total), all passed. The build script cleaned its declared artifacts. `git diff --check` passed. No Production source/header or lifecycle behavior changed. No retained resource was released, retried, replayed, or deleted.

This closes the Stage 5.26 paired Adapter snapshot evidence gap for the supported success-A / ACK-failure-B ordering. Stage 5.16 remains BLOCKED because only B is retained; A's inventory is empty. Stage 5.19 now has production copied Adapter snapshots for both Adapter namespaces (one empty, one retained), a matching real Adapter-associated Worker snapshot, and a separate real DIRECT Worker snapshot. A combined atomic cross-plane API remains explicitly outside the frozen v1 contract. Stage 5.18 remains PARTIAL and Stage 5.20 remains incomplete because no evidence-specific reconciliation action is approved. Stage 5 / Phase 1 remain NOT ACCEPTED; Production remains NO-GO.
