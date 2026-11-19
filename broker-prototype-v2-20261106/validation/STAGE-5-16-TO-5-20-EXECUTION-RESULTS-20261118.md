# Stage 5.16–5.20 Execution Results — 2026-11-18

## Scope and result

Executed the feasible parts of Stage 5.16–5.20 in the Working Copy. Outcomes are separated by evidence level; blocked work is not reported as complete.

## Files changed

- `managed-pipe-io-v2-native-test.c`
- `integrated-submit-adapter-v2-api-test.c`
- This report.

No production source/header, build script, README, model, configuration, or production artifact was changed. No commit/reset was performed.

## 5.16 — Multiple independent Adapter-associated retained submissions

**Current status: PARTIAL / NOT COMPLETE.** The earlier blocked assessment below is superseded by the 2026-11-19 mixed-stage runtime evidence addendum.

Current proven runtime result: two real Adapter-associated retained records from distinct Adapter instances and distinct Worker operation IDs coexist on one shared Worker. Adapter B reaches terminal READ completion and is retained after its Submitter ACK fails (`SUBMITTER_ACK` / Worker `POST_TERMINAL`). After that Worker is degraded, Adapter A's second public submit registers identity but Worker admission rejects; Adapter-local close failure during pre-issue cleanup retains A (`HANDLE_CLOSE_FAILURE` / Worker `ADAPTER_ASSOCIATED`). Production Adapter v1 inventories return A=1 and B=1; production Worker v1 inventory copies both records and validates both identity pairs.

This proves two real Adapter API submissions and two independent Adapter-associated retained records. It does **not** prove two OS-issued operations or two independent post-terminal ACK failures. The stricter dual-ACK scenario remains incomplete; the shared Worker fail-closed DEGRADED sequencing is documented in the dated addenda. No lifecycle bypass, fabricated record, new hook, or Production behavior change is authorized by this evidence.
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

**Current status: PARTIAL.** Production copied v1 inventories have now been exercised after their documented quiescence conditions against real retained runtime records:
- Worker v1 on a real Adapter-associated ACK-quarantined operation.
- Worker v1 on a real DIRECT READ retained by finalizer close failure.
- Adapter v1 for both Adapter namespaces in the mixed-stage shared-Worker test (A=1, B=1), with the Worker v1 inventory matching both stable identity pairs.
- Worker v1 on two real DIRECT retained records from the concurrent degradation / handoff-not-transferred test.

The v1 consistency model is two independent copied per-plane inventories after each plane's own quiescence preconditions; it does not specify an atomic cross-plane snapshot. The test-only validator and runtime evidence remain distinguished. The remaining 5.19 dependency is the stricter 5.16 scenario with two independently OS-issued Adapter-associated operations each reaching post-terminal ACK failure; the mixed-stage result must not be overstated as that scenario.
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

1. 5.16: the mixed-stage runtime case proves two real Adapter-associated retained records and both identity pairs. Still outstanding: two independently OS-issued Adapter-associated operations each reaching its own post-terminal ACK-failure path. Do not bypass the shared Worker's DEGRADED gate or fabricate a second completion.
2. 5.18: remains partial. The mixed-stage Adapter admission-rejected / Adapter-local close-failure branch is now covered; distinct-operation concurrent DIRECT retention and the remaining externally inducible runtime retain branches require separate evidence. Invariant-guard primitive contracts are covered by focused component tests; do not manufacture invalid internal state to simulate their runtime failures.
3. 5.19: production v1 Adapter and Worker inventories are exercised on the real mixed-stage two-record case. Remaining dependency is the stricter 5.16 dual-ACK runtime scenario; no atomic cross-plane API is required by the frozen v1 contract.
4. 5.20: no safe reconciliation action is approved. Any production recovery/reconciliation API requires a separately approved contract covering authorization, ownership, state transitions, concurrency, and fail-closed behavior. No release/retry/replay behavior is authorized.
5. Positive recovery tests and Stage 5 acceptance remain blocked until the required design/owner decisions and acceptance evidence exist.
## Acceptance

- 5.16: PARTIAL / NOT COMPLETE — two real mixed-stage Adapter-associated retained records are proven; dual independent post-terminal ACK failures are not.
- 5.17: focused actual DIRECT retained-path identity assertions PASS.
- 5.18: PARTIAL.
- 5.19: PARTIAL — production per-plane v1 inventories verified on real Adapter and DIRECT retained records, including both mixed-stage Adapter records.
- 5.20: identity/inventory contract baseline exists; production Recovery/Reconciliation API contract NOT COMPLETE; no reconciliation action approved.
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

## Addendum - 2026-11-19 bounded source-to-runtime retention coverage review (Stage 5.18)

Reviewed the production retention branches in `managed-pipe-io-v2.c` and `integrated-submit-adapter-v2.c` against the existing native runtime tests. This was a source/test traceability review only; no production or test code changed.

| Production retention branch | Existing evidence | Coverage boundary |
|---|---|---|
| DIRECT finalizer close failure after real pipe READ (`finish_caller_terminal`) | `test_close_failure_retention`; real READ, injected second-handle close failure; production Worker v1 inventory copied after join | Covered for HANDLE_CLOSE / DIRECT. Does not cover every ownership-invariant branch. |
| DIRECT finalizer/close failure during unissued cleanup (`discard_unissued`) | No dedicated runtime test located for this exact path | Gap. Existing submission/create/duplicate failure tests do not establish a close failure while discarding an unissued DIRECT operation. |
| DIRECT owner/resource-state guard, integrated caller-terminal failure, finalizer commit, owner release, unreserve, and caller-finish failure | Helper/state-contract coverage exists; no distinct real-runtime fault injection located for each branch | Partial only. These are ownership-invariant fail-closed branches, not evidence of runtime recovery or cleanup. |
| DIRECT failed handoff after uncertain/pending I/O (`handoff_pending`) | Timeout/handoff native tests cover normal transfer; no test located that forces the production handoff to return NOT_TRANSFERRED after uncertain I/O and then validates the resulting retained record | Gap in exact failure-branch runtime evidence. Do not simulate by editing Worker lifecycle/list state. |
| Adapter OS-accepted I/O followed by handoff-not-transferred | `test_handoff_failure_after_os_acceptance_retains` | Covered for the injected degraded/handoff failure ordering; real OS-accepted READ remains retained, callback suppressed, issuer lease and reservation remain. |
| Adapter ACK failure after Worker terminal release | `test_shared_worker_two_adapter_success_then_ack_quarantine`; also the two ACK signal failure variants | Covered for real Adapter/Worker runtime ACK quarantine and post-terminal ownership state. |
| Adapter not-issued finalizer close failure | `test_finalizer_close_failure_retains` | Covered for real Adapter runtime HANDLE_CLOSE retention, with post-join test-hook snapshot evidence. This is not the production copied-v1 inventory path. |
| Adapter not-issued ownership-invariant failures (owner transition, integrated terminal, finalizer commit/release, unreserve, worker finish) | No distinct real-runtime fault-injection test located for each branch | Gap/partial. Existing broad ownership contract tests do not prove each callback branch under runtime execution. |
| Adapter transferred-but-untracked quarantine after Worker wake failure | `test_worker_wake_failure_transferred_untracked` | Test-only hook path. It verifies fail-closed state and Adapter envelope retention while the Worker operation remains queued/untracked; not a production callback path or general recovery proof. |
| First-retain duplicate/concurrent publication | `test_concurrent_first_retain_publication`; actual Adapter-associated duplicate publication scenario | Covered at helper same-operation race and real Adapter-associated immutability levels; does not cover two distinct real operations concurrently retained. |

Review conclusion: existing evidence is strong for the concrete real-runtime HANDLE_CLOSE DIRECT branch, Adapter not-issued HANDLE_CLOSE branch, Adapter ACK-quarantine branch, and the specifically injected OS-accepted/handoff-failure and wake-failure scenarios. It does not cover every production ownership-invariant retain site or all cleanup/handoff failure combinations. In particular, the ownership-invariant cases after `raya_reaper_integrated_caller_terminal`, finalizer commit/release, `raya_reaper_integrated_unreserve`, `raya_reaper_caller_finish`, and `raya_pipe_owner_caller_collect` remain source-reviewed but not independently fault-injected as distinct real-runtime branches.

No new test was added because the remaining branches require precise fault injection at ownership transitions whose failure semantics are not established by the current public/test seams. Adding generic mutation hooks or forcing internal state would risk manufacturing invalid states rather than validating a supported runtime failure. The next safe step is a narrow review of whether any existing injected API seam can naturally produce one of those exact failures without changing production semantics; otherwise record the specific owner/design dependency rather than inventing a recovery test.

Stage 5.18 remains PARTIAL. Stage 5.16 remains BLOCKED; Stage 5.19 remains PARTIAL; Stage 5.20 remains incomplete. No Production source/header, build script, model, configuration, lifecycle, recovery, cleanup, retry, or replay behavior changed. No retained resource was released or modified. Phase 1 remains NOT ACCEPTED; Production remains NO-GO.
## Addendum - 2026-11-19 refinement: invariant-contract tests versus runtime fault injection (Stage 5.18)

A second, narrower source review refined the prior coverage matrix. Some ownership-invariant primitives were incorrectly summarized as having no test coverage at all. Their rejection contracts are tested in focused component tests, but those tests do not drive the corresponding failure through the real Adapter or DIRECT runtime callback.

| Primitive / runtime branch | Existing focused contract evidence | Real runtime evidence | Correct classification |
|---|---|---|---|
| `raya_reaper_integrated_caller_terminal` | `reaper-integrated-caller-unreserve-test.c` validates the legal caller-terminal transition; core tests cover invalid caller-terminal state/lease rejection | No supported runtime seam forces the transition to fail after a valid reservation and terminal OS completion | Contract covered; runtime fault branch not independently injectable |
| `raya_reaper_integrated_unreserve` | `reaper-integrated-caller-unreserve-test.c` verifies rejection before caller-terminal and rejection of duplicate unreserve without mutating reservation/count state | No real Adapter/DIRECT runtime fault injection for unreserve failure | Contract covered; runtime fault branch not independently injectable |
| `raya_reaper_caller_finish` | `reaper-core-v2-test.c` verifies successful finish and rejection when caller lease count is already zero | Runtime cleanup invokes it on an admitted lease; no supported seam creates a zero-lease condition at that exact point without corrupting accounting | Contract covered; runtime fault branch not independently injectable |
| `raya_pipe_finalizer_commit_release` | `pipe-io-finalizer-v2-test.c` verifies valid commit, duplicate commit rejection, and rejection before both handles are closed; `reaper-two-phase-release-test.c` adds two-phase release coverage | Real runtime HANDLE_CLOSE failure is covered; no runtime seam makes commit fail after a valid successful close sequence | Contract covered; runtime fault branch not independently injectable |
| `raya_pipe_owner_release` | `pipe-io-owner-v2-test.c` exercises legal and invalid owner-state transitions and duplicate release rejection | Runtime Adapter/DIRECT owner-release invariant failure is not independently fault-injected | Contract covered; runtime fault branch not independently injectable |
| `raya_pipe_owner_caller_submission_failed` / caller collection | Owner state-machine tests exercise accepted/rejected transitions and uncertainty classification | Real OS submit/read completion and selected failure paths are covered, but not arbitrary internal state corruption | State contract covered; runtime branch coverage remains specific to exercised OS outcomes |
| `raya_pipe_finalizer_close_handles` | `pipe-io-finalizer-v2-test.c` covers first-close and second-close failure, uncertain state, and no repeated close attempt | DIRECT and Adapter runtime HANDLE_CLOSE retention paths are separately covered | Contract and selected real runtime branches covered |

The implementation review explains why a generic runtime fault hook is not justified: `caller_terminal`, `unreserve`, `caller_finish`, `commit_release`, and owner release reject invalid state/counter/lease combinations. In the valid runtime sequence those predicates should succeed. Forcing their failure would require altering internal ownership state or adding a callback that substitutes for core accounting transitions; neither is an existing supported external failure source. Such a test would demonstrate behavior under manufactured invariant corruption, not a naturally reachable production failure. The existing focused component tests remain the appropriate evidence for their rejection/non-mutation contracts.

Consequently, revise the prior matrix wording: these primitive contracts are not untested; what remains absent is distinct end-to-end runtime fault injection for each invariant guard. Do not add hooks or mutate internal state to claim such coverage. The remaining legitimate runtime evidence work is limited to concrete externally inducible failures (for example, the already-supported close callback failures) and any additional OS/handoff outcome that can be produced through existing documented seams without violating ownership invariants.

No code changed. This addendum corrects evidence classification only. Stage 5.18 remains PARTIAL because its runtime evidence is branch-specific and does not establish all possible production failure paths. Stage 5.16 remains BLOCKED; Stage 5.19 remains PARTIAL; Stage 5.20 remains incomplete. No lifecycle, ownership, recovery, cleanup, retry, or replay behavior changed. Phase 1 remains NOT ACCEPTED; Production remains NO-GO.
## Addendum - 2026-11-19 real DIRECT handoff-not-transferred runtime evidence (Stage 5.18)

Added `test_handoff_not_transferred_after_concurrent_degradation` to `managed-pipe-io-v2-native-test.c`. The test uses only existing supported seams: the per-worker close callback and the per-submission cancellation callback. It creates two real connected named-pipe operations on separate caller threads. The pending operation reaches its bounded wait/cancel path and blocks inside the supplied cancellation callback. The other operation completes a real READ locally; its configured close callback fails the first finalizer close and signals the pending caller. That first operation is retained with `HANDLE_CLOSE`, causing the shared Worker to enter `DEGRADED`. The pending caller then completes its cancellation/drain path and attempts handoff against the already-degraded Worker. The production integrated handoff rejects transfer, and `handoff_pending` retains the still-owned operation with `OWNERSHIP_INVARIANT` rather than dropping or freeing it.

Assertions verify both submissions return RETAINED, Worker lifecycle is DEGRADED with both causes, caller-active is zero, two reservations and two retained records remain, Worker join reaps the degraded thread with its expected non-success exit, and the production Worker retained-inventory v1 API copies both identity-bearing DIRECT records with their distinct first-retain causes/stages. Completion callback remains suppressed. No retained resource is released or altered by the test.

Build/verification: `build-managed-pipe-io-v2-native-test.cmd`, Visual Studio 2026 BuildTools 18.7.3 x64; 10 consecutive executions, each 176 assertions and zero failures (1,760 assertions total). `git diff --check` passed. The first test invocation exposed a named-pipe test-fixture name collision when two pipes were created in the same tick; the fixture now separates pipe creation and the subsequent existing test setup by 10 ms. No production source/header or lifecycle behavior changed.

`discard_unissued` was also reviewed. Its caller is the post-registration `raya_managed_worker_reserve` failure. The integrated reserve predicate rejects once lifecycle has moved from ACCEPTING (including a concurrent transition to STOPPING). That race is conceptually reachable, but the direct managed-pipe path has no existing barrier/callback between admission/registration and reserve; `CreateEventW`, `DuplicateHandle`, and the direct `ReadFile`/`WriteFile` are not injected through a supported Direct API seam. Forcing the timing with stress loops or mutating the worker lifecycle would not be deterministic evidence of this branch. Therefore no `discard_unissued` runtime test was fabricated and this exact branch remains uncovered by a deterministic supported-seam test.

Coverage conclusion: the `handoff_pending` NOT_TRANSFERRED branch now has real DIRECT runtime evidence for the concrete concurrent-degradation ordering. This does not prove all possible causes of NOT_TRANSFERRED, nor does it cover `discard_unissued`. Stage 5.18 remains PARTIAL. Stage 5.16 remains BLOCKED; Stage 5.19 remains PARTIAL; Stage 5.20 remains incomplete. No recovery/reconciliation action is approved. Phase 1 remains NOT ACCEPTED; Production remains NO-GO.
## Addendum - 2026-11-19 Adapter reserve-failure / unissued-discard seam assessment (Stage 5.18)

Reviewed the complete `adapter_submit_impl` ordering and the Adapter/Submitter injected API contracts for the reserve-failure cleanup branch. The relevant sequence is: create envelope/buffer and operation ID; `create_event`; `duplicate_handle`; initialize operation; register Worker identity; `raya_managed_worker_admit`; `raya_submitter_admit`; then `raya_managed_worker_reserve`. If reserve fails, the Adapter aborts Submitter admission, marks caller submission failed, closes both owned handles through the injected Adapter `close_handle`, commits finalizer release, releases the owner, finishes the Worker caller lease, and frees the envelope. If any ownership/cleanup transition fails, it retains the envelope.

The existing Adapter API seam exposes `create_event`, `duplicate_handle`, `read_file`, `write_file`, `get_last_error`, and `close_handle`. `create_event` and `duplicate_handle` execute before Worker registration/admission; `read_file`/`write_file` execute later on the Submitter issue path, after reserve and enqueue; `close_handle` is reached in cleanup after reserve failure or other pre-issue failure. The Submitter API seam controls its own event/thread/wait/close operations, but does not provide a callback between Worker admission and Worker reserve. Consequently, failing an existing callback cannot directly and deterministically cause `raya_managed_worker_reserve` to reject in the target window.

A concurrent external transition of the shared Worker lifecycle after `raya_managed_worker_admit` and before `raya_managed_worker_reserve` is a theoretical way for the reserve predicate to reject. The current API offers no deterministic barrier in that interval. The existing callbacks either run before the interval or after reserve; scheduling stress would be probabilistic and would not establish a stable regression test. Mutating Worker lifecycle/counters or adding a new production/test-only callback at that exact point would manufacture a transition or expand the seam specifically to reach the branch. Neither is justified under the current Stage 5 evidence rules.

This is a bounded seam assessment, not a proof that the race is impossible. No test was added and no Production code, header, API contract, lifecycle, or cleanup behavior changed. The Adapter reserve-failure cleanup branch remains without deterministic runtime evidence through existing supported seams. The distinct Adapter not-issued close-failure branch and Adapter OS-accepted handoff-failure branch remain covered by their existing tests; they must not be treated as coverage of this reserve-failure branch.

Combined conclusion for `discard_unissued` / reserve-failure cleanup: DIRECT has no deterministic existing seam between admission/registration and reserve; Adapter has a theoretically reachable concurrent lifecycle race but no deterministic existing seam in that interval. The DIRECT `handoff_pending` concrete concurrent-degradation branch is now covered by the preceding runtime test; this does not close either discard/reserve-failure gap. Stage 5.18 remains PARTIAL. Stage 5.16 remains BLOCKED; Stage 5.19 remains PARTIAL; Stage 5.20 remains incomplete. Phase 1 remains NOT ACCEPTED; Production remains NO-GO.
## Addendum - 2026-11-19 Stage 5.19 current snapshot-evidence reconciliation

Reconciled the original 5.19 status text and its subsequent dated addenda against the frozen production retained-inventory v1 contract and the current runtime tests. Earlier statements in the opening 5.19 section that describe only test-hook snapshots or say that production enumeration is absent are historical and superseded by the following verified evidence:

1. Production Worker v1 copied inventory was exercised after join/quiescence on a real Adapter-associated ACK-quarantined runtime operation. The test validates the copied Worker identity, inventory kind, Adapter identity pair, first-retain cause and stage.
2. Production Worker v1 copied inventory was separately exercised after join/quiescence on a real DIRECT managed-pipe READ retained by finalizer close failure. The test validates DIRECT shape, Worker identity, and first-retain cause/stage.
3. Production Adapter v1 copied inventory was exercised post-quiescence on both sides of the controlled shared-Worker test: Adapter A returned a valid zero-record snapshot after successful completion; Adapter B returned its actual ACK-retained envelope with stable Adapter identity and first-retain evidence. The matching real Adapter-associated Worker record was also copied through Worker v1.
4. Production Worker v1 inventory was exercised on the concrete concurrent DIRECT degradation/handoff-not-transferred test after Worker join; it copied both actual DIRECT retained records with distinct first-retain causes/stages.
5. Focused v1 API tests cover quiescence rejection, bounded capacity/no partial output, version/record shape, identity namespaces, partial identity-less records, and snapshot non-mutation. These contract tests remain distinct from the real runtime snapshots above.

The frozen v1 design intentionally specifies two independent copied inventories, not one combined atomic Adapter/Worker/Submitter snapshot. Sequential per-plane snapshots after their respective quiescence preconditions are the supported consistency model; absence of an atomic cross-plane API is therefore not a defect or remaining 5.19 acceptance criterion. The test-only cross-plane diagnostic remains non-atomic and is not promoted to a production API.

**Current 5.19 disposition: PARTIAL, with the supported production v1 snapshot paths exercised on real Adapter-associated and real DIRECT retained runtime records.** The remaining dependent runtime evidence is the two-independent-Adapter-associated-retained-submissions-on-one-shared-Worker scenario, which is still blocked under 5.16. The successful-A / retained-B experiment proves two Adapter namespaces and separate production snapshots but has only one retained Adapter-associated submission; it must not be counted as two. No further 5.19 code or API change is justified until 5.16's ownership-correct runtime scenario is resolved. This conclusion does not require an atomic cross-plane API or any mutation/recovery capability.

No source, header, test, build script, ABI, lifecycle, ownership, cleanup, recovery, retry, callback, or replay behavior changed in this addendum. Existing test results are cited as already recorded evidence; no new build or test execution is claimed. Stage 5.16 remains BLOCKED; Stage 5.17 remains focused-complete; Stage 5.18 remains PARTIAL; Stage 5.19 remains PARTIAL for the explicit 5.16-dependent runtime scenario; Stage 5.20 remains incomplete. Gate D remains complete with no action approved; Gate E/Gate F remain blocked. Phase 1 remains NOT ACCEPTED; Production remains NO-GO.
## Addendum - 2026-11-19 mixed-stage two-Adapter retained runtime evidence

Extended `test_shared_worker_two_adapter_success_then_ack_quarantine` using only existing public Adapter submit calls and existing injected callback seams. No production source/header, ABI, build script, lifecycle behavior, or recovery behavior changed.

### Runtime sequence and exact retained classes

1. Adapter A / Submitter A submits operation ID 1 through the shared Worker. The real named-pipe READ completes, Worker finalization succeeds, Submitter ACK succeeds, and the completion callback runs once. Adapter A has no retained envelope at this point.
2. Adapter B / Submitter B submits operation ID 1 through the same Worker. Its real named-pipe READ completes and Worker terminal release succeeds. The existing exact-event ACK fault is injected for Submitter B. The Adapter release callback retains B's envelope as `SUBMITTER_ACK`; the corresponding Worker record is `ADAPTER_ASSOCIATED` with `POST_TERMINAL` stage and `OWNERSHIP_INVARIANT` first cause. The B completion callback is suppressed and the shared Worker becomes `DEGRADED`.
3. After that degradation, a second real public submit call is made through Adapter A (Adapter operation ID 2). The Worker identity registration succeeds on the still-initialized shared Worker object, then Worker admission rejects because the lifecycle is `DEGRADED`. The existing Adapter-local `close_handle` callback is configured to fail during the resulting pre-issue finalizer cleanup. The real Adapter path retains A's envelope with `HANDLE_CLOSE_FAILURE`; its Worker inventory record has the matching Adapter identity pair, `ADAPTER_ASSOCIATED` stage, and `HANDLE_CLOSE` first cause. No Submitter admission, OS I/O issue, or completion callback occurs for this second A operation.

This is a deterministic, non-fabricated way to produce two independent Adapter-associated retained records on one shared Worker: A instance / operation 2 and B instance / operation 1. Their Worker epoch is shared while their Worker operation IDs are distinct. The two records have different, explicitly asserted retain causes and stages. This proves two retained records from two real Adapter API submissions, but it is **not** evidence of two OS-issued operations or two post-terminal ACK failures. The strict dual-independent-ACK-failure path described in the dispatcher-foundation plan remains unproven and is not reclassified as complete.

### Snapshot evidence

After both Adapter admission gates are closed, both Submitters are quiesced, and the shared Worker is joined:

- Production Adapter v1 inventory returns one record for Adapter A (operation 2, `HANDLE_CLOSE_FAILURE`) and one for Adapter B (operation 1, `SUBMITTER_ACK`).
- Production Worker v1 inventory returns both actual records, with distinct Worker operation IDs and exact matching Adapter identity pairs.
- The test-only per-Adapter snapshots are taken separately for A and B. Their Adapter records are combined only in caller-owned test storage and validated against the copied shared Worker list; no atomic cross-plane snapshot is claimed.
- The A retained duplicated handles remain open and tracked, as required by quarantine. The test verifies their handles remain valid. No retained resource is released, retried, replayed, or deleted.

### Validation

- Standard prescribed `build-integrated-submit-adapter-v2-api-test.cmd`: `/W4 /WX` build PASS; 10 consecutive runs PASS; 1,042 assertions across 66 cases per run (10,420 assertion executions).
- Explicit `/DRAYA_ADAPTER_TESTING` build: `/W4 /WX` build PASS; one execution PASS; 1,258 assertions across 68 cases. The combined test-only validator accepted both Adapter snapshots against the shared Worker inventory.
- `git diff --check`: PASS.
- The prescribed script removed its declared outputs; the explicit hook build's exact EXE/OBJ outputs were removed afterward. The output audit found no declared build artifacts left.

### Updated disposition

- 5.16: PARTIAL / NOT COMPLETE. Two independent Adapter-associated retained records and their identity pairs are now proven through real runtime paths. The stricter planned scenario in which both independent operations are OS-issued and each reaches its own post-terminal ACK-failure path remains unproven; this addendum does not claim it.
- 5.17: focused DIRECT retained-path identity coverage remains complete.
- 5.18: PARTIAL. The Adapter Worker-admission-rejected / Adapter-local close-failure runtime branch is now covered; other distinct runtime retain branches and DIRECT `discard_unissued` remain outstanding.
- 5.19: PARTIAL. Production Adapter v1 snapshots now capture both real Adapter namespaces and production Worker v1 inventory captures both real records. The supported v1 per-plane consistency model is demonstrated; the remaining acceptance dependency is the stricter 5.16 dual-ACK runtime scenario. No atomic cross-plane API is required or claimed.
- 5.20: incomplete; no reconciliation action is approved and no recovery/release/retry/replay API is authorized.
- Gate D remains complete with no action approved; Gate E and Gate F remain blocked.
- Phase 1 remains NOT ACCEPTED; Production remains NO-GO.