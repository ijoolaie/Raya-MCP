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
## Addendum — 2026-11-19 Stage 5.18 existing-seam revalidation

Re-read the exact production call ordering in `managed-pipe-io-v2.c` (`discard_unissued`, submit admission through reserve) and `integrated-submit-adapter-v2.c` (`adapter_submit_impl`, Worker admission through reserve), together with the public injected API declarations in `integrated-submit-adapter-v2.h`.

Findings:
- DIRECT: `discard_unissued` is entered only after `raya_managed_worker_reserve` returns false. The direct submission path has no injected `create_event`, `duplicate_handle`, or I/O callback; these are direct Win32 calls. Its existing close callback runs only after the reserve rejection has already happened, so it can inject cleanup-close failure but cannot deterministically cause the prerequisite reserve rejection. Concurrent shutdown/degradation in the narrow admit/register-to-reserve interval remains a timing race, not a supported deterministic seam.
- Adapter: `create_event` and `duplicate_handle` callbacks occur before Worker admission; `read_file` / `write_file` occur after reserve/enqueue; `close_handle` is reached on cleanup after a failure. None provides a barrier between successful `raya_managed_worker_admit` and `raya_managed_worker_reserve`. A concurrent lifecycle transition could theoretically reject reserve, but the existing API callbacks cannot deterministically coordinate that interval.
- Existing Adapter admission-rejected close-failure coverage is a different branch: it fails at Worker admission, not reserve. Existing DIRECT handoff-not-transferred coverage is also distinct from `discard_unissued`.

Decision: no additional runtime test can be added for either exact reserve-failure branch using only the currently supported deterministic seams. Do not add timing stress, mutate Worker lifecycle/accounting state, or introduce a new production/test hook merely to force the branch. A future deterministic test would require an explicitly approved seam/barrier at the admission-to-reserve boundary; adding one is outside this evidence-only pass and would require separate review. No source, header, API, test, build script, ownership, lifecycle, cleanup, or recovery behavior changed. No tests were rerun or newly claimed. Stage 5.18 remains PARTIAL; Stage 5.16 and 5.19 remain PARTIAL; Stage 5.20 remains incomplete; Stage 5 acceptance remains BLOCKED / NOT ACCEPTED; Phase 1 NOT ACCEPTED; Production NO-GO.
## Addendum - 2026-11-19 Stage 5.20 contract-boundary reconciliation

A fresh review of the 5.20 execution baseline against the frozen v1 inventory contract, `PRODUCTION-RETAINED-INVENTORY-IMPLEMENTATION-20261118.md`, and the Gate D decision separates the completed read-only prerequisites from the still-unapproved recovery contract:

- Stable process-local Worker and Adapter operation identities, inventory kinds, explicit optional Adapter association, and immutable first-retain cause/stage are implemented. Production Adapter-envelope and Worker-operation copied v1 inventories are implemented and verified on real retained runtime records after their documented quiescence conditions. These are observation-only interfaces; they do not mutate retained ownership.
- Gate D is complete as a decision: the reviewed evidence proves no safe evidence-specific reconciliation action. Retained resources therefore remain quarantined for the remainder of the current process lifetime. The decision is not an approval of quarantine as the Phase 1 product boundary; that requires explicit project-owner acceptance.
- The production reconciliation/action contract remains incomplete and unauthorized. The current v1 inventory omits the complete per-record irreversible-effect ledger needed to establish every Issue/handoff/completion/finalizer substep/owner-release/reservation/ACK/callback disposition, and it does not provide recipient-side receipt/status/idempotency evidence for accepted or outcome-unknown WRITE. Inventory identity, first-retain cause/stage, counts, or list membership cannot substitute for those facts.
- Consequently, no release/unlink/counter adjustment/free/manual close/close retry/callback or ACK retry/I/O resubmission/replay/cursor mutation is approved. No positive recovery test is safe to add. Negative quarantine tests remain the applicable evidence; Gate E remains blocked, and Gate F remains pending explicit owner acceptance plus PR review.

This reconciliation supersedes only historical statements in earlier 5.20 design/review documents that production stable identity or read-only enumeration is absent. It does not promote the v1 inventories into a recovery API, approve any reconciliation action, or change the frozen v1 ABI/consistency model.

No source, header, test, build script, ABI, lifecycle, ownership, cleanup, recovery, retry, callback, replay, or Orchestrator cursor behavior changed. No tests/builds were run for this documentation-only reconciliation; previously recorded verification remains as cited in the implementation report. No retained resource was released, modified, retried, replayed, or deleted. Stage 5.20 remains INCOMPLETE; Gate D remains COMPLETE with no action approved; Gate E and Gate F remain BLOCKED; Stage 5/Phase 1 remain NOT ACCEPTED; Production remains NO-GO.

## Addendum - 2026-11-19 Stage 5.20 owner-decision register

This register distinguishes decisions already frozen/implemented from decisions that cannot be inferred from source or test evidence. It records the minimum owner inputs blocking further 5.20 recovery-contract work; it does not choose product policy on the owner's behalf.

### Settled; do not reopen in this slice

- **Read-only inventory shape:** two independent copied inventories (Adapter envelope and Worker operation), caller-established quiescence, bounded caller-owned arrays, no cross-plane atomicity, no side effects. The v1 ABI and implementation are present; this is not a recovery/release surface.
- **Current uncertain-effect safety rule:** accepted or outcome-unknown I/O, uncertain WRITE, conflicting evidence, partial cross-plane commits, and uncertain close/ownership state remain quarantined. No replay, generic release, blind close retry, callback/ACK retry, or cursor mutation.
- **Gate D result:** no evidence-specific reconciliation action is currently proven safe or approved. This is a present technical disposition, not a permanent product decision that quarantine must be accepted forever.

### Explicit owner decisions still required

| ID | Decision required | Evidence boundary / consequence if unanswered | Required authority |
|---|---|---|---|
| O-520-1 | Accept fail-closed quarantine for the remainder of the process lifetime as an explicit Phase 1 limitation, or decline Phase 1 acceptance. | Gate F cannot pass; Stage 5/Phase 1 remain NOT ACCEPTED. This does not authorize release. | Project owner |
| O-520-2 | Define the intended recovery scope: in-process local cleanup only, restart-surviving recovery, or both. | Do not design a production reconciliation state machine or claim restart recovery until scope is explicit. Current process-local IDs and v1 inventories are not durable. | Project/product owner |
| O-520-3 | For each retained failure class, identify the sole resource/state owner and whether any action is ever permitted; explicitly list permanently quarantined classes. | No action API, ownership transfer, unlink, close, free, callback, ACK, or teardown transition can be specified from aggregate counts or first-retain metadata. | Component owners plus project owner |
| O-520-4 | Decide whether durable restart recovery is a product requirement and, if yes, approve the durable identity namespace, storage/ACL and single-writer model, durability boundary, retention/capacity/backup policy, and supported failure model. | The current WAL work is an in-memory codec/scanner, not an on-disk journal. No durable identity allocator or crash recovery exists; no journal implementation is authorized by this register. | Project owner and deployment/operations owner |
| O-520-5 | If recipient-level WRITE completion/replay is in scope, name the recipient/protocol owner and approve stable idempotency identity, authenticated receipt/status semantics, verifier trust, and positive/negative evidence rules. Otherwise explicitly keep uncertain WRITE permanently non-replayable. | Local ReadFile/WriteFile acceptance, Worker handoff, local ACK, and process-local inventory do not prove recipient commit. No recipient verifier/status protocol is implemented. | External recipient/protocol owner plus project owner |
| O-520-6 | Name the authorized operator/role, authorization boundary, audit requirements, and approval process for any future evidence-specific reconciliation action. | No caller authorization or privileged reconciliation action can be safely exposed. A generic release-by-ID remains prohibited. | Security/operations owner plus project owner |

### Work that can proceed without those decisions

Only source-backed, non-mutating contract clarification and negative tests that prove quarantine/no-replay behavior can proceed, provided they use existing supported seams and do not manufacture internal state. The already implemented v1 inventories may be used for observation under their exact quiescence contract. No production recovery behavior, new recovery API, WAL file I/O, durable identity, recipient protocol, positive release test, or Orchestrator recovery cursor work is authorized by this register.

### Current gate result

- O-520-1 through O-520-6: OPEN; no owner approval evidence was found in the reviewed project records.
- Gate D: COMPLETE; no reconciliation action approved.
- Gate E: BLOCKED; no safe positive recovery action/test can be specified.
- Gate F: BLOCKED pending O-520-1 and PR review.
- Stage 5.20: INCOMPLETE. Stage 5 and Phase 1: NOT ACCEPTED. Production: NO-GO.

Review was documentation-only. No source, header, ABI, test, build script, journal, lifecycle, ownership, cleanup, recovery, retry, callback, replay, or Orchestrator cursor behavior changed. No tests/builds were run. No retained resource was released, modified, retried, replayed, or deleted.
## Addendum - 2026-11-19 delegated owner decision directions for O-520-1 through O-520-6

The project owner explicitly delegated selection of the six Stage 5.20 decision answers in the conversation dated 2026-11-19, with the constraint that answers follow the repository's safety, ownership, evidence, and staged-delivery principles. The following decisions close the product-direction questions at policy level. They do not waive technical acceptance criteria, authorize a recovery mutation, or grant Production approval.

### Decisions

| ID | Decision | Scope and guardrails |
|---|---|---|
| O-520-1 | **ACCEPT** fail-closed quarantine for the remainder of the current process lifetime as an explicit Phase 1 limitation. | This accepts the limitation, not Phase 1 as a whole. It does not authorize generic release, manual close, retry, replay, or reconciliation. Phase 1 remains unaccepted until its other gates, evidence, review, and acceptance criteria pass. |
| O-520-2 | **BOTH**, as a staged architectural objective: bounded in-process recovery first; restart-surviving recovery only as a later capability after its durable contract is approved and implemented. | Do not conflate the two scopes. No restart recovery is implied by current process-local identities or copied v1 inventories. No current implementation may claim either recovery capability merely from observation APIs. |
| O-520-3 | **SINGLE, explicit ownership per resource and state; no shared implicit ownership.** Adapter owns its envelope and Adapter-local submission/admission state; Worker owns Worker operation lifecycle, retained Worker records, and Worker-side reservations; Submitter owns its queue/issue/ACK state until the documented transfer/terminal boundary. | The exact resource disposition for each failure class must be mapped to the actual source transition and recorded before any action contract is proposed. A component may release only resources for which it has current, proven ownership; cross-component release requires an explicit, validated ownership transfer. Aggregate counts, IDs, or first-retain metadata are not release authority. Unknown, partial, or conflicting ownership remains quarantined. |
| O-520-4 | **NO**: durable restart recovery is not a Phase 1 requirement. | Keep the current in-memory WAL codec/scanner as non-durable work; do not add journal file I/O or durable identity as part of this Stage 5.20 decision closure. If restart recovery is later promoted to a product requirement, it requires a separate approved design covering identity namespace, storage and ACL, single-writer/locking, durability boundary, crash consistency, retention/capacity, backup/restore, and failure model. |
| O-520-5 | **NO automatic replay of uncertain WRITE.** Treat it as permanently non-replayable under the current contract. | A future recipient-level idempotent protocol may be proposed only as a separately approved capability with stable idempotency identity, authenticated recipient receipt/status, verifier trust, and explicit evidence rules. This decision does not authorize implementing that protocol or replay now. |
| O-520-6 | **Least-privilege, explicitly authorized operational role with auditable, case-specific approval.** | Authorization must be deployment-configured and attributable to a named principal/role; every proposed action needs evidence, reason, and immutable audit record. Any future irreversible release/reconciliation action requires independent approval (two-person control) and a narrowly scoped action contract. No role is currently granted a reconciliation API or action because Gate D has approved none. |

### Consequences and implementation boundary

1. Phase 1 product policy accepts process-lifetime quarantine as a disclosed limitation; this closes the policy choice in O-520-1 only. It does not change the current Stage 5, Phase 1, or Production acceptance status.
2. The target architecture may account for both in-process and restart-surviving recovery, but implementation is staged. The current Stage 5.20 slice remains limited to source-backed contract clarification, observation, and negative quarantine/no-replay evidence.
3. O-520-3 is a binding ownership principle, not permission to infer a concrete release action. A failure-class-to-owner/resource matrix must be source-traced before any future reconciliation action can be considered.
4. O-520-4 explicitly defers durable restart recovery. No WAL file I/O, durable identity allocator, storage/ACL setup, or crash-recovery implementation is authorized by this decision.
5. O-520-5 preserves the current no-replay rule. No recipient protocol, positive replay test, or uncertain-WRITE resubmission is authorized.
6. O-520-6 defines the future authorization/audit standard but grants no current operational privilege and does not override Gate D's no-action result.

### Updated decision status

- O-520-1 through O-520-6: **DECIDED at policy level** by delegated project-owner instruction dated 2026-11-19.
- Gate D: COMPLETE; no evidence-specific reconciliation action approved.
- Gate E: BLOCKED; positive recovery action/tests remain unauthorized and unsupported by evidence.
- Gate F: no longer blocked on the O-520-1 policy choice alone; remains subject to all other acceptance evidence, project-owner review of the resulting record, and PR review. This addendum does not declare Gate F passed.
- Stage 5.20: remains INCOMPLETE until the remaining source-backed work and acceptance evidence are complete.
- Stage 5 / Phase 1: NOT ACCEPTED pending remaining gates and review. Production: NO-GO.

This is a documentation-only decision record. No source, header, ABI, test, build script, journal, lifecycle, ownership implementation, cleanup, recovery, retry, callback, replay, authorization API, or Orchestrator cursor behavior changed. No tests/builds were run. No retained resource was released, modified, retried, replayed, or deleted.

## Addendum - 2026-11-19 O-520-3 source-backed ownership matrix

Pushed the detailed failure-class ownership map in `validation/STAGE-5-20-RESOURCE-OWNERSHIP-MATRIX-20261119.md`. It traces the Adapter envelope, Worker operation/registry/reservation, Submitter request/Issue/Publish/ACK, and per-operation owner/finalizer domains to the current production source, then maps pre-issue setup, admission rejection, reserve rejection, enqueue/backpressure, NOT_ISSUED, uncertain Issue/Publish, Worker terminal hold, finalizer/owner/registry failures, post-terminal ACK failure, and identity/invariant failures. The matrix distinguishes separate irreversible effects and records the missing per-record cross-plane effect ledger and recipient WRITE evidence. It is documentation-only; no action is approved and no runtime behavior changed.

This addendum also supersedes the earlier O-520 register's statement that all six decisions were OPEN: O-520-1 through O-520-6 are now DECIDED at policy level by the delegated decision record above. The remaining Gate E blocker is technical/evidentiary, not an outstanding owner-policy answer. Stage 5.20 remains INCOMPLETE; Gate D complete/no action approved; Gate E blocked; Gate F not passed; Phase 1 NOT ACCEPTED; Production NO-GO.

## 2026-11-19 Stage 5.20 production-v1 effect-ledger field audit

Extended `STAGE-5-20-RESOURCE-OWNERSHIP-MATRIX-20261119.md` with a field-by-field comparison of the exact Production Adapter/Worker v1 record layouts, Submitter request/snapshot state, Worker operation state, owner/finalizer/integrated reservation state, and the `RAYA_ADAPTER_TESTING`-only diagnostic snapshot. Confirmed that Production v1 exposes only process-local identity/correlation plus first-retain cause/stage; it does not expose per-request Issue/Publish state, completion values, per-handle close outcomes, owner/finalizer commits, per-operation registry/reservation release, Worker finish, ACK result, callback receipt, or envelope-free history. Some selected details exist only as mutable in-process fields or test-only diagnostics; aggregate Submitter counts are not per-record proof. No recipient-side authenticated WRITE receipt/status/idempotency protocol exists in the reviewed contract. This distinguishes missing effect history from the intentionally separate per-plane snapshots and does not propose a combined snapshot as a substitute. Documentation only; no source/header/API/test/build/lifecycle/recovery behavior changed; no tests/builds run. Gate D remains no action approved; Gate E blocked; Stage 5.20 incomplete; Phase 1 NOT ACCEPTED; Production NO-GO.

## 2026-11-19 Stage 5.20 negative Submitter no-replay callback assertions

Extended `dedicated-io-submitter-v2-failure-test.c` using only its existing Issue/Publish/NOT_ISSUED callback seams. The Issue-uncertain case now asserts Issue exactly once, Publish zero times, and NOT_ISSUED zero times. The Publish-uncertain-after-accepted-Issue case asserts Issue exactly once, Publish exactly once, and NOT_ISSUED zero times. Both retain their existing `UNCERTAIN` request/effect/degraded-state assertions. This directly guards against an automatic second Issue/Publish invocation in these two Submitter worker executions and against misclassifying either case as NOT_ISSUED. Prescribed `build-dedicated-io-submitter-v2-failure-test.cmd` passed 25 consecutive VS 2026 BuildTools 18.7.3 x64 `/W4 /WX` runs, 302 assertions per run (7,550 total assertion executions). Test-only change; no Production source/header/API or recovery behavior changed. This is not recipient-side WRITE evidence, durable restart evidence, or end-to-end reconciliation proof. Stage 5.20 remains INCOMPLETE; Gate D no action approved; Gate E remains blocked for positive recovery; Phase 1 NOT ACCEPTED; Production NO-GO.

## 2026-11-19 Stage 5.20 degraded Submitter admission-closure assertions

Extended the two existing uncertain-effect failure tests to assert the post-degradation admission boundary: after the Submitter worker exits with Issue-uncertain or Publish-uncertain, lifecycle remains `RAYA_SUBMITTER_DEGRADED`; a fresh request is rejected by the existing public `raya_submitter_admit` API without assigning a request ID or changing its BUILDING state; existing Issue/Publish callback totals remain unchanged. Combined with the preceding callback counters, this proves no callback replay for the uncertain request and no new admission into that degraded Submitter instance. Prescribed VS 2026 BuildTools 18.7.3 x64 `/W4 /WX` script passed 25 consecutive runs, 310 assertions/run (7,750 total). Test-only; no Production code, API, or runtime behavior changed. Scope is limited to same-process Submitter-local admission; no claim about Adapter/Worker global admission, recipient-side WRITE commit, queued work, or restart. Stage 5.20 INCOMPLETE; Gate D no action approved; Gate E positive recovery blocked; Phase 1 NOT ACCEPTED; Production NO-GO.

## 2026-11-19 Stage 5.20 queued-request quarantine after uncertain Issue

Added `test_queued_request_remains_quarantined_after_uncertain_issue` to `dedicated-io-submitter-v2-failure-test.c`, using only the existing public Submitter API and Issue/Publish/NOT_ISSUED callbacks. The Issue callback is deterministically held after request 1 enters Issue; request 2 is then admitted and enqueued while the first Issue is active. Releasing the callback with `RAYA_SUBMITTER_ISSUE_UNCERTAIN` makes the Submitter worker exit DEGRADED. The test verifies request 1 is UNCERTAIN with Issue UNKNOWN and Publish NOT_ATTEMPTED; request 2 remains QUEUED and the queue count remains one; active_issue returns to zero while the issuer lease remains one; the captured degraded snapshot preserves queue=1, active_issue=0, issuer_leases=1; Issue callback ran once, Publish zero times, and NOT_ISSUED zero times. This proves queued work is retained/quarantined rather than silently issued or relabeled NOT_ISSUED after an uncertain Issue. It does not prove a safe drain/disposition, restart behavior, or recipient-side WRITE outcome.

Prescribed `build-dedicated-io-submitter-v2-failure-test.cmd`: VS 2026 BuildTools 18.7.3 x64 `/W4 /WX`, 25 consecutive passes, 332 assertions/pass (8,300 total assertion executions). One initial assertion expectation was corrected to match the source-backed lease contract: uncertain Issue releases `active_issue` but intentionally retains `issuer_leases`; no Production source/header/API or runtime behavior changed. `git diff --check` passed. Stage 5.20 remains INCOMPLETE; Gate D no action approved; Gate E positive recovery remains blocked; Phase 1 NOT ACCEPTED; Production NO-GO.

## 2026-11-19 Stage 5.20 queued-request quarantine after uncertain Publish

Added `test_queued_request_remains_quarantined_after_uncertain_publish` using the existing public Submitter admission/enqueue/snapshot APIs and existing Issue/Publish/NOT_ISSUED callbacks. The first Issue returns ACCEPTED; its Publish callback is deterministically held while a second request is admitted and enqueued. During the hold, request 1 has Issue COMMITTED and Publish IN_PROGRESS, while request 2 remains QUEUED and the snapshot reports RUNNING, queue=1, active_issue=1, issuer_leases=1. Releasing Publish with an uncertain/failure result makes the worker exit DEGRADED. The test verifies request 1 remains UNCERTAIN (Issue COMMITTED, Publish UNKNOWN), request 2 remains QUEUED and is not issued or converted to NOT_ISSUED, queue=1, active_issue=0, issuer_leases=1, Issue callback exactly once, Publish callback exactly once, and NOT_ISSUED zero times. The captured degraded snapshot preserves these counts. Prescribed `build-dedicated-io-submitter-v2-failure-test.cmd`: VS 2026 BuildTools 18.7.3 x64 `/W4 /WX`, 25 consecutive passes, 356 assertions/pass (8,900 total assertion executions). Test-only; no Production source/header/API/runtime behavior changed. This proves same-process Submitter quarantine after uncertain Publish only; no drain/disposition, restart, or recipient-side WRITE outcome is established or authorized. Stage 5.20 remains INCOMPLETE; Gate D no action approved; Gate E positive recovery blocked; Phase 1 NOT ACCEPTED; Production NO-GO.
## 2026-11-19 Stage 5.20 ACK notification failure is not re-applied

Extended the existing `test_acknowledge_set_failure` using only the public `raya_submitter_acknowledge` API. The first ACK consumes the issuer lease and transitions the request to ACKED, then the injected `changed_event` notification failure degrades the Submitter. The test now calls acknowledge again and verifies it is rejected, the request remains ACKED, the Win32 `set_event` callback count does not increase, and the degraded snapshot remains empty of queue/producer/active/lease counts. Prescribed VS 2026 BuildTools 18.7.3 x64 `/W4 /WX` build passed 25 consecutive runs × 361 assertions = 9,025 assertion executions. Test-only; no Production source/header/API/runtime behavior changed. This proves the ACK transition and notification are not re-applied through the public API after this failure; it does not establish durable ACK receipt, caller-side observation, or cross-process recovery. Stage 5.20 remains INCOMPLETE; Gate D no action approved; Gate E blocked; Phase 1 NOT ACCEPTED; Production NO-GO.
## 2026-11-19 Stage 5.20 NOT_ISSUED abort notification no-repeat assertion

Extended `test_abort_set_failure` using only the public `raya_submitter_abort_admission` API. After the first abort transitions the admitted request to `NOT_ISSUED` and the injected `changed_event` notification fails (degrading the Submitter), a second abort call is rejected. The request remains `NOT_ISSUED`, the injected `SetEvent` callback count does not increase, and the degraded snapshot remains queue=0, producers=0, active_issue=0, issuer_leases=0. Prescribed VS 2026 BuildTools 18.7.3 x64 `/W4 /WX` failure-matrix build passed 25 consecutive runs × 366 assertions = 9,150 assertion executions. Test-only; no Production source/header/API/runtime behavior changed. This proves same-process repeated abort does not reapply the transition/notification after this failure; it does not establish durable callback receipt or external observation. Stage 5.20 remains INCOMPLETE; Gate D no action approved; Gate E blocked; Phase 1 NOT ACCEPTED; Production NO-GO.
## 2026-11-19 Submitter worker-originated NOT_ISSUED callback boundary

Re-read the production `submitter_main` NOT_ACCEPTED branch and the existing `test_not_accepted_notification_failure`. The production order is: validate the issuing request/lease; decrement `active_issue` and `issuer_leases`; set `issue_effect=NOT_ATTEMPTED` and request state `NOT_ISSUED`; attempt `changed_event` notification; if that notification fails, mark the Submitter DEGRADED, leave the lock, invoke the configured `not_issued` callback once, and return from the worker. The existing deterministic test injects failure at the first worker-side changed-event SetEvent (after the two successful enqueue notifications), waits for worker termination, and verifies DEGRADED, empty queue/producer/active/lease snapshot, request `NOT_ISSUED`, and callback count exactly one.

This worker-originated callback path is distinct from producer-side `raya_submitter_abort_admission`. The latter's implementation accepts only `REQ_ADMITTED` with a positive active-producer count; once a request is `NOT_ISSUED`, a second abort is rejected and cannot re-invoke the worker callback. No public API replays or retries the `not_issued` callback, and the callback type returns `void`, so the Submitter has no callback receipt/result to expose. The callback is invoked after notification failure, but its external side effect/observation is not durably acknowledged by this contract. No new seam, retry, callback replay, internal-state mutation, or Production behavior change is justified. Decision: existing test is sufficient for exactly-once invocation within this worker execution; external callback receipt/observation remains an explicit contract/evidence gap and is not to be simulated by a new retry path. Documentation only; no tests/builds rerun or newly claimed. Stage 5.20 remains INCOMPLETE; Gate D no action approved; Gate E blocked; Phase 1 NOT ACCEPTED; Production NO-GO.

## 2026-11-19 Adapter-side NOT_ISSUED effect chain and combined-test boundary

Follow-up source trace of the production Adapter callback `adapter_not_issued` (integrated-submit-adapter-v2.c) establishes the synchronous side-effect chain after Submitter marks a request NOT_ISSUED: transition pipe owner via `caller_submission_failed`; record Worker integrated caller-terminal; run the finalizer close for the Adapter-owned duplicate pipe/event handles; commit finalizer release; release pipe owner; unreserve the Worker integrated reservation; finish the Worker caller lease; invoke the user completion callback; free the Adapter envelope. Any ownership/finalizer/unreserve/finish failure instead retains the envelope and records a degradation cause; no automatic retry is present.

Existing evidence is split across two tests: `test_immediate_read_failure` in `integrated-submit-adapter-v2-api-test.c` exercises the real Adapter NOT_ISSUED cleanup/callback/free path when Submitter notification succeeds; `test_not_accepted_notification_failure` in `dedicated-io-submitter-v2-failure-test.c` exercises exactly-once generic callback invocation when Submitter `changed_event` notification fails. Neither test combines the real Adapter callback chain with that notification failure, so their evidence must not be conflated.

The integrated Adapter test harness already has an injected Submitter `set_event` callback and a registration seam that can target the NOT_ISSUED changed-event signal after issuer lease decrement. However, that combined case would intentionally leave the Submitter DEGRADED after its worker exits. Public Submitter Destroy accepts STOPPED or specific partial-destroy degraded states (`destroy_progress` 3/1), while the notification-failure transition leaves `destroy_progress` at its initial state; there is no public cleanup transition for this degraded instance. A same-process combined test therefore cannot safely tear down its Submitter using the public contract and would need an isolated process-lifetime fixture, not a synthetic state edit or direct handle cleanup. Decision: do not add a same-process combined test or teardown workaround in this pass. Keep the exact evidence boundary explicit: Adapter cleanup success and Submitter notification-failure callback-once are separately proven; their combined end-to-end interaction and externally durable callback receipt remain unproven. No source/test/API/build/Production behavior changed; documentation only. Stage 5.20 remains INCOMPLETE; Gate D no action approved; Gate E blocked; Phase 1 NOT ACCEPTED; Production NO-GO.

## 2026-11-20 Stage 5.16 serialized Worker ACK-failure boundary

A fresh source trace of the existing two-Adapter runtime case and the Worker completion loop sharpens the remaining 5.16 blocker:

- reaper-managed-worker-v2.c, managed_worker_main: each loop checks w->integrated.core.lifecycle == RAYA_REAPER_DEGRADED under the Worker lock and exits immediately when true (lines 499-503).
- The same loop resolves and claims one wait result, collects that operation, terminal-holds it, closes/commits its finalizer, releases its owner and integrated registry entry, then invokes exactly that operation's release callback before returning to the loop (lines 557-585 and 637-683). There is no second completion/release dispatch within that iteration.
- integrated-submit-adapter-v2.c, adapter_worker_release: a failed raya_submitter_acknowledge calls retain_envelope and returns (lines 193-205). retain_envelope publishes the retained record, marks the shared Worker DEGRADED, finishes the caller lease, and signals the Worker (lines 139-180).
- Consequently, after the first Adapter-associated post-terminal ACK failure, the Worker cannot process another operation's terminal completion/release callback: its next loop entry exits at the lifecycle guard. A second operation may already have been registered, reserved, enqueued, or OS-issued, but this implementation cannot establish a second independent post-terminal ACK failure on that same Worker.
- The existing test_shared_worker_two_adapter_success_then_ack_quarantine does not contradict this: Adapter B reaches the post-terminal ACK failure; Adapter A is then submitted after Worker degradation and is retained on the admission-rejected/pre-issue cleanup path. It proves two real Adapter-associated retained records, not two independently OS-issued post-terminal ACK failures.

Decision for this implementation slice: do not add a timing test, mutate lifecycle state, suppress the DEGRADED exit, continue dispatch after a failed ACK, or add a test-only bypass. Each would either fail to prove the requested runtime condition or change the production safety contract. The strict two-post-terminal-ACK target is structurally unreachable under the current single-worker fail-closed contract. Satisfying it requires a separately designed and reviewed architecture/acceptance change (for example, a different isolation boundary), not additional test scaffolding in Stage 5. This source review does not waive 5.16; it remains PARTIAL and blocks dependent 5.19 acceptance.

Documentation/source-trace only. No source, header, test, build, ABI, lifecycle, retry, recovery, or Production behavior changed. No tests/builds were run. git diff --check is required before commit. Stage 5.16 PARTIAL; Stage 5.19 PARTIAL; Stage 5.18 PARTIAL; Stage 5.20 INCOMPLETE; Gate D no action approved; Gate E blocked; Phase 1 NOT ACCEPTED; Production NO-GO.

## 2026-11-20 Stage 5.18 externally inducible failure evidence boundary audit

A focused re-read of the existing native WAIT_FAILED and wake-signal tests distinguishes three different evidence levels that must not be conflated:

1. `reaper-native-waitfailed-retention-test.c` constructs a wait plan over a synthetic integrated operation, closes the disposable wake handle, directly calls `WaitForMultipleObjects`, verifies `WAIT_FAILED/ERROR_INVALID_HANDLE`, then directly marks the integrated core DEGRADED and checks registry/outstanding/reservation remain. It does not run `managed_worker_main`, does not exercise the Worker-owned retained-list publication path, and does not produce a production Worker retained-inventory record. It is valid WaitPlan/Core fail-closed evidence, not end-to-end Managed Worker retention evidence.
2. `reaper-native-wakefailure-retention-test.c` creates 70 real named-pipe operations, transfers them to the integrated queue, then calls `SetEvent(INVALID_HANDLE_VALUE)` and directly marks the integrated core DEGRADED. Its own comment states no worker is started. It verifies queued/outstanding/reservation preservation and no promotion. This is real OS-issued/transferred operation and core-queue quarantine evidence, but not Managed Worker callback/retained-list evidence.
3. `integrated-submit-adapter-v2-api-test.c:test_worker_wake_failure_transferred_untracked` uses a real Adapter and Managed Worker thread plus the existing Worker wake callback seam. The callback fails after Adapter quarantine publication; the Worker exits/join is observed; Adapter retains its envelope while Worker retained inventory remains empty, and the operation remains queued/outstanding with a held reservation and valid handles. This is a distinct, valid Adapter-local transferred/untracked wake-failure branch, not proof of Worker retained-list publication.

The already-recorded real runtime branches remain distinct: DIRECT finalizer HANDLE_CLOSE retention with production Worker v1 inventory; Adapter NOT_ISSUED finalizer HANDLE_CLOSE retention; Adapter post-terminal ACK retention; DIRECT concurrent-degradation `handoff_pending` retention; Adapter admission-rejected/pre-issue close-failure retention; and the transferred/untracked Adapter wake-failure branch above. Primitive ownership/finalizer rejection tests establish invalid-transition non-mutation contracts, not runtime fault injection of those guards.

Remaining Stage 5.18 gaps after this reconciliation:
- DIRECT `discard_unissued` following reserve rejection has no deterministic supported seam between admission/registration and reserve.
- Adapter reserve-rejection cleanup has no deterministic supported seam between Worker admission and reserve; the existing admission-rejected close-failure test exercises an earlier, different branch.
- Runtime injection of invariant guards (`caller_terminal`, finalizer commit/release, owner release, unreserve, caller-finish) would require manufactured internal state corruption or a new seam that substitutes for core ownership transitions; neither is justified by the current contract.
- The native WaitPlan/Core tests do not close the Managed Worker retained-inventory gap for their respective wait-failure scenarios. No new claim is made that those lower-level tests prove a Worker retained record.

Decision: no additional test is added in this evidence-only pass. Existing seams either exercise a different branch or cannot deterministically reach the prerequisite reserve/invariant failure. No timing stress, internal state mutation, direct resource cleanup, new test hook, or Production behavior change is justified. Stage 5.18 remains PARTIAL. No tests/builds were run; this is a source/test-evidence classification only. Stage 5.16 PARTIAL; Stage 5.19 PARTIAL (dependent on 5.16); Stage 5.20 INCOMPLETE; Gate D no action approved; Gate E blocked; Phase 1 NOT ACCEPTED; Production NO-GO.

## 2026-11-20 consolidated Stage 5 acceptance gap register

This register consolidates the current source/test evidence and supersedes earlier status summaries only where later dated addenda provide newer evidence. It does not relax the original acceptance target or authorize a Production behavior change.

| Area | Current evidence-backed status | Exact remaining acceptance gap | Safe closure condition / current boundary |
|---|---|---|---|
| Stage 5.16 — shared-Worker dual post-terminal ACK failures | PARTIAL. Two real Adapter-associated retained records and their identity pairs coexist on one shared Worker; one is a real post-terminal ACK failure and the other is admission-rejected/pre-issue close-failure retention. | Two independent operations each OS-issued and each reaching its own post-terminal ACK-failure path on the same shared Worker remain unproven. Source shows the first ACK failure degrades the Worker and the next Worker loop exits before processing another operation. | Requires an explicit architecture/acceptance decision that preserves fail-closed semantics while defining how independent terminal ACK work is dispatched. Do not bypass DEGRADED exit or weaken quarantine merely to satisfy a test. Existing multi-Submitter fault dispatcher is test-harness foundation, not proof of the target scenario. |
| Stage 5.17 — DIRECT retained identity | COMPLETE for the focused actual DIRECT retained-path identity assertions. | No additional gap is recorded within this bounded acceptance item. | Preserve the exact scope; do not generalize it to all DIRECT failure branches. |
| Stage 5.18 — failure-class runtime evidence | PARTIAL. Runtime evidence exists for DIRECT HANDLE_CLOSE, DIRECT handoff-not-transferred/handoff_pending, Adapter NOT_ISSUED cleanup, Adapter post-terminal ACK retention, Adapter admission-rejected/pre-issue close-failure, and Adapter transferred/untracked wake failure. Native WAIT_FAILED and core wake-failure tests prove Core/queue quarantine, not Worker retained-list publication. | Exact DIRECT discard_unissued after reserve rejection and Adapter reserve-rejection cleanup lack deterministic existing seams. Ownership-invariant primitive tests do not inject each invariant guard at runtime. Adapter+Submitter NOT_ISSUED notification-failure end-to-end evidence remains split because public teardown cannot safely complete the degraded fixture. | Close only gaps that can be reached through existing supported deterministic seams and public lifecycle contracts. No timing stress, internal state mutation, forced teardown, or new hook in this pass. If a new seam is proposed, it needs separate contract/security review and must not alter Production semantics. |
| Stage 5.19 — retained inventory/snapshot evidence | PARTIAL. Production Worker v1 inventory and per-Adapter v1 inventories are proven on real retained records, including the two-record mixed-stage case. The contract is two independent copied per-plane inventories under documented quiescence, not an atomic cross-plane snapshot. | The remaining dependency is Stage 5.16's stricter dual-ACK runtime scenario; no separate atomic snapshot defect is identified. | Close only when 5.16 is accepted or its target is explicitly revised through project architecture/acceptance review. Do not add cross-plane atomicity as an invented requirement. |
| Stage 5.20 — ownership/effect/recovery boundary | INCOMPLETE. O-520-1..6 are DECIDED at policy level; source-backed ownership matrix and Production v1 field audit exist; same-process negative no-replay/admission/quarantine assertions exist. | Production v1 inventories do not preserve immutable per-record chronological effect outcomes, per-handle close receipts, authenticated ACK/callback receipts, or recipient-side WRITE status/idempotency. Some richer fields are mutable in-process or test-only. There is no public safe reconciliation contract. | Gate D remains no action approved. Preserve process-lifetime quarantine; no generic release/unlink/free/close retry, callback/ACK retry, uncertain I/O replay, cursor mutation, or positive recovery test. Any future effect-ledger/recovery API requires a separately reviewed contract; durable restart recovery is not Phase 1 scope. |
| Gate F / Phase 1 acceptance | NOT PASSED. PR #6 remains Draft/open; no explicit PR review/acceptance evidence is recorded in this pass. | Project-owner acceptance of the complete Stage 5/Phase 1 evidence and PR review remain outstanding. Policy acceptance of process-lifetime quarantine as a limitation is not acceptance of Phase 1 as a whole. | Do not self-approve or merge. Gate F passes only after explicit owner acceptance and required review against the completed evidence. |

### Consolidated disposition

- Stage 5.16: PARTIAL; architecture/dispatch boundary blocks the strict dual-ACK target.
- Stage 5.17: COMPLETE within its focused scope.
- Stage 5.18: PARTIAL; exact deterministic seam and combined degraded-fixture gaps remain.
- Stage 5.19: PARTIAL; depends on 5.16 strict target.
- Stage 5.20: INCOMPLETE; policy decisions are closed, but the current evidence cannot support a positive recovery action and no such action is approved.
- Gate D: COMPLETE / NO ACTION APPROVED. Gate E: BLOCKED. Gate F: PENDING acceptance/review.
- Stage 5: NOT ACCEPTED. Phase 1: NOT ACCEPTED. Production: NO-GO.

This is a documentation-only consolidation. No source, header, ABI, test, build script, lifecycle, ownership, cleanup, recovery, retry, callback, replay, or Orchestrator cursor behavior changed. No tests/builds were run. No retained resource was released, modified, retried, replayed, or deleted.

## 2026-11-20 public lifecycle API audit (Stage 5.20)

Audited the public Adapter/Worker/Submitter inventory, admission-close, shutdown, join, and destroy APIs against the retained/effect gaps. No existing public API safely reconciles retained or uncertain work: Adapter destroy closes admission but requires active=0 and retained=0; Worker shutdown rejects DEGRADED/FAILED with `RETRY_RECONCILE` (a classification only), and Worker destroy requires STOPPED plus empty outstanding/queued/reserved/registry/retained state; Submitter admission-close only closes the API gate, shutdown does not settle uncertain/queued work, and destroy requires empty queue/producers/active issue/issuer leases, accepting only narrowly defined partial handle-destroy states. Snapshot/inventory and Join provide diagnostic/quiescence evidence, not per-effect, recipient, ACK-observation, or callback receipts. This confirms there is no hidden public teardown/reconciliation route around Gate D. Documentation/source review only; no source/header/test/build/lifecycle/recovery changes and no tests/builds run. Stage 5.20 remains INCOMPLETE; Gate D no action approved; Gate E blocked; Gate F pending owner acceptance/review; Stage 5 and Phase 1 NOT ACCEPTED; Production NO-GO.

## 2026-11-20 Adapter NOT_ISSUED + notification-failure composition review

Source trace confirms `adapter_not_issued` performs caller-terminal transition, finalizer close/commit, owner release, Worker unreserve, caller-finish, user callback, and envelope free before the Submitter's later notification failure is reported. Existing evidence is split: the Submitter standalone `test_not_accepted_notification_failure` proves its configured callback runs once, request remains NOT_ISSUED, aggregate counts reach zero, and Submitter becomes DEGRADED; Adapter API tests separately prove NOT_ISSUED cleanup and retained-state behavior without this notification failure. The current test harness has an injected Submitter API table, but its SetEvent failure selector is ACK-specific; no single end-to-end test asserts both sides' postconditions. Therefore the combined path remains a bounded test-evidence gap (not a proven defect) and Stage 5.20 remains INCOMPLETE. No Production changes, recovery behavior changes, internal state mutation, or new hooks were made; this was source/test review only and no tests/builds were run. Gate D remains no action approved; Gate E blocked; Gate F pending owner acceptance/review; Stage 5/Phase 1 NOT ACCEPTED; Production NO-GO.

## 2026-11-20 combined NOT_ISSUED notification-failure test feasibility decision

A follow-up feasibility review used the existing Adapter API test fixture and Submitter lifecycle implementation to decide whether the previously identified combined scenario should be added now. The Adapter test harness can inject the Submitter Win32 API table through `raya_integrated_adapter_start_with_apis`; the current `test_submitter_set_event` selector is ACK-specific and could be extended locally in the test file to target the notification after the Adapter's NOT_ISSUED callback. That is technically reachable without a Production hook.

The decisive constraint is teardown, not injection. The worker-originated NOT_ISSUED callback runs `adapter_not_issued` first; the subsequent changed-event notification failure leaves the Submitter DEGRADED at its initial destroy-progress state. The public `raya_submitter_destroy_ex` contract rejects this state (only narrowly defined partial handle-destroy progress states are accepted), while Adapter/Worker shutdown and destroy preconditions do not provide a way to dispose of a deliberately degraded fixture. The existing `stop_components` helper therefore cannot safely complete. Reproducing the failure and then bypassing public lifecycle APIs, manually closing fixture-owned handles, zeroing structures, or relying on process exit would turn the test into forced teardown and would not establish a valid lifecycle outcome.

Decision: do not add the combined test or broaden the injection selector in this Stage 5 pass. The current evidence remains source-composed and the end-to-end postcondition gap remains PARTIAL. This is not a claim that the combined runtime path is unreachable; it is reachable, but the current public lifecycle contract cannot safely reclaim the degraded test fixture. Closing the gap requires an owner-reviewed test-fixture/lifecycle contract or an explicitly accepted process-isolated test strategy; it does not justify a Production recovery API, forced teardown, or weakening fail-closed behavior. No source, header, test, build, ABI, Production lifecycle, or recovery behavior changed; no tests/builds were run. Stage 5.20 remains INCOMPLETE; Gate D no action approved; Gate E blocked; Gate F pending explicit acceptance/review; Stage 5/Phase 1 NOT ACCEPTED; Production NO-GO.

## 2026-11-20 Stage 5.18 deterministic reserve-rejection closure decision

Revalidated the exact production ordering for the two remaining reserve-rejection branches. In the DIRECT path (`managed-pipe-io-v2.c`), the operation is registered and then `raya_managed_worker_reserve` is called; `discard_unissued` is reached only when that reserve call rejects. There is no supported injected callback/barrier between registration and reserve. The configured close callback runs only after rejection, so it can test cleanup failure but cannot cause or deterministically synchronize the reserve rejection itself.

In the Adapter path (`integrated-submit-adapter-v2.c`), create-event and duplicate-handle callbacks run before Worker admission; read/write callbacks run after successful reserve/enqueue; close callbacks run during cleanup after a failure. The public injected API table therefore has no deterministic callback between successful admission/registration and reserve. A concurrent shutdown/degradation race could theoretically change the reserve result, but no supported barrier establishes the required ordering; timing stress would be probabilistic and is not accepted as evidence.

Decision: classify both exact reserve-rejection runtime branches as uncovered under the current supported test seams. Do not add a new hook, production callback, scheduler/timing stress, internal state mutation, or forced cleanup. A future deterministic test requires a separately reviewed test-only synchronization contract that exposes this exact boundary without changing Production semantics, plus observable postcondition assertions. This decision does not claim the branches are impossible or prove a Production defect. Stage 5.18 remains PARTIAL. No code/test/build changes; no tests/builds run. Stage 5.16 PARTIAL; Stage 5.17 COMPLETE (bounded scope); Stage 5.19 PARTIAL; Stage 5.20 INCOMPLETE; Gate D no action approved; Gate E blocked; Gate F pending explicit owner acceptance/review; Stage 5/Phase 1 NOT ACCEPTED; Production NO-GO.

## 2026-11-20 Stage 5.16 / 5.19 architecture acceptance decision required

A source-backed review of `validation/STAGE-5-25-SHARED-WORKER-ACK-QUARANTINE-ORDERING-TRACE-20261118.md`, the current Worker/Adapter call ordering, and the consolidated acceptance gap register confirms that the strict Stage 5.16 target is an acceptance criterion, not merely an optional stress scenario: two independent OS-issued Adapter operations must each reach their own post-terminal Submitter ACK failure/retention path on the same shared Worker. The current implementation serializes `op->release` synchronously on the single Worker thread. The first ACK failure retains its envelope, marks the shared Worker DEGRADED, and returns to a loop that exits before processing another completion. A barrier inside the first ACK callback cannot make that same Worker process a second completion while the callback is blocked.

The existing mixed-stage test remains valid but does not satisfy this criterion: it proves two Adapter-associated retained records, where the second is rejected before issue and retained during pre-issue cleanup. It does not prove a second OS-issued post-terminal ACK failure. Stage 5.19 depends on the strict 5.16 criterion and therefore remains PARTIAL; its two independent copied per-plane inventory APIs are otherwise exercised on real retained records. No atomic cross-plane snapshot requirement is added.

**Explicit architecture/acceptance decision required from the project owner:**
1. Preserve the strict dual-post-terminal-ACK criterion and authorize a separately designed architecture change that provides independent ACK-failure isolation without weakening fail-closed quarantine; or
2. Formally revise the Stage 5.16 acceptance criterion to a different, precisely testable multi-Adapter isolation contract, with corresponding Stage 5.19 dependency and evidence updates.

Neither option is selected by this execution pass. The prior delegated Stage 5.20 policy answers do not authorize an architecture change or waive Stage 5.16/5.19 technical acceptance. Until this decision is recorded through project architecture/acceptance review, do not add a barrier, suppress DEGRADED exit, continue dispatch after uncertain ACK, or relabel the existing mixed-stage test as dual-ACK coverage. No source, header, ABI, test, build, lifecycle, ownership, cleanup, recovery, retry, callback, or replay behavior changed. No tests/builds were run. Stage 5.16 and 5.19 remain PARTIAL; Stage 5.18 remains PARTIAL; Stage 5.20 remains INCOMPLETE; Gate D remains NO ACTION APPROVED; Gate E remains BLOCKED; Gate F remains pending explicit owner acceptance/review; Stage 5/Phase 1 remain NOT ACCEPTED; Production remains NO-GO.

## 2026-11-20 owner decision: preserve strict Stage 5.16 criterion

The project owner selected preservation of the strict dual-post-terminal-ACK acceptance criterion. Two distinct Adapter/Submitter operations must each be independently OS-issued and each reach its own post-terminal ACK failure/retention path on the same shared Worker. Stage 5.19 remains dependent on this exact proof. A separately reviewed architecture-design work item is authorized; no architecture candidate is selected and no implementation, Production behavior change, test hook, recovery action, or relaxation of fail-closed semantics is authorized by this decision. The design gate and required ownership/lifecycle questions are recorded in validation/STAGE-5-16-STRICT-DUAL-ACK-ARCHITECTURE-DESIGN-GATE-20261120.md. Stage 5.16/5.19 remain PARTIAL; Stage 5.18 PARTIAL; Stage 5.20 INCOMPLETE; Gate D NO ACTION APPROVED; Gate E BLOCKED; Gate F pending review/acceptance; Stage 5/Phase 1 NOT ACCEPTED; Production NO-GO. Documentation only; no tests/builds run.

## 2026-11-20 Stage 5.16 strict dual-ACK implementation and acceptance result

The owner selected preservation of the strict Stage 5.16 criterion. Candidate A was implemented in the broker-prototype branch and the original criterion was not relaxed.

Implementation summary:
- Added `raya_submitter_acknowledge_ex` with typed outcomes. The compatibility Boolean `raya_submitter_acknowledge` remains and returns true only for SUCCESS.
- Only ACK_COMMITTED_NOTIFICATION_FAILED (request ACKED, issuer lease released, notification failed, Submitter DEGRADED) enters Adapter-scoped post-terminal quarantine without degrading the shared Worker.
- Adapter and Worker retained records are published exactly once; callback/free and ACK retry are suppressed. All other ACK failures, retained-publication failures, and Worker/operation lifecycle failures preserve Worker DEGRADED fail-closed behavior.
- Added retained-only cause `RAYA_RECOVERY_CAUSE_SUBMITTER_ACK_NOTIFICATION` (bit 6), excluded from the Worker recovery-policy evaluator's accepted degradation mask; the policy test verifies it remains BLOCKED if used as a Worker degradation cause.
- Added a dedicated `--strict-dual-ack` test mode and updated the API test build script to run that mode as its own process before the regular suite. This keeps intentionally retained fixture storage valid until process exit; the regular suite does not reuse its stack storage.

Strict runtime test proves two real named-pipe READ operations from two distinct Adapter/Submitter pairs are both OS-issued and simultaneously outstanding/registered on the same Worker (`ReadFile` call count 1 each, Worker outstanding 2, Registry count 2) before either completion is released. Each then reaches its own terminal/finalizer/owner/Integrated release and independent post-terminal ACK-notification failure. The test verifies two Adapter retained records, two matching Worker POST_TERMINAL records, two identity pairs, both Submitters DEGRADED with ACKED requests and zero issuer leases, no callback delivery, no second OS Issue after Submitter degradation, shared Worker remains ACCEPTING, orderly Worker shutdown/join, and Destroy remains blocked by retained inventory. Production Adapter v1 and Worker v1 inventory copies match both identity pairs. No retained record is unlinked or freed.

Validation:
- Standard API build `/W4 /WX`: 10 isolated strict runs, 107 assertions each; 10 regular-suite runs, 863 assertions across 66 cases each. PASS.
- `RAYA_ADAPTER_TESTING` API build `/W4 /WX`: 10 isolated strict runs, 120 assertions each; 10 regular-suite runs, 1,048 assertions across 68 cases each. PASS.
- Recovery policy: 16 assertions, 0 failures.
- Dedicated Submitter native core: 18 assertions per run, 25 consecutive runs PASS.
- Build scripts removed their declared EXE/OBJ outputs; `git diff --check` PASS.

### Revised acceptance register

| Area | Updated evidence-backed status | Remaining gap |
|---|---|---|
| Stage 5.16 | **COMPLETE** for the unchanged strict dual-post-terminal-ACK criterion in the broker-prototype branch. | No Stage 5.16 strict runtime gap remains in this bounded criterion. Production wiring is not authorized. |
| Stage 5.19 | **COMPLETE** within the frozen v1 contract: independent per-plane copied inventories after each plane's quiescence preconditions. The strict scenario proves two Worker v1 records and one Adapter v1 record for each Adapter, with matching identity pairs. | No atomic cross-plane snapshot is required or claimed. |
| Stage 5.18 | PARTIAL. | DIRECT/Adapter reserve-rejection and combined Adapter+Submitter NOT_ISSUED notification-failure branches have runtime evidence. Existing wake-failure runtime test confirms a transferred-but-unprocessed operation remains Adapter-retained and Worker-queued/reserved but is absent from Worker retained inventory. Managed Worker-level native WAIT_FAILED runtime evidence is also absent; it requires a dedicated test-only wait seam. Invariant-guard runtime fault injection remains intentionally excluded as manufactured internal corruption. |
| Stage 5.20 | INCOMPLETE. | Production per-record effect/receipt evidence and safe reconciliation contract remain absent; no positive action is approved. |
| Gate D / E / F | D COMPLETE / NO ACTION APPROVED; E BLOCKED; F pending explicit owner acceptance and PR review. | Positive recovery remains unauthorized; full Phase 1 acceptance is not met. |

Stage 5/Phase 1 remain NOT ACCEPTED. Production remains NO-GO. The implementation is confined to the broker-prototype branch and does not authorize Production wiring, reconciliation/release, retry, replay, callback redelivery, or changes to the Phase 1 recovery policy.

## 2026-11-20 deterministic DIRECT and Adapter reserve-rejection runtime coverage (Stage 5.18)

Following explicit project-owner authorization, added a narrowly scoped RAYA_ADAPTER_TESTING-only callback at the shared raya_managed_worker_reserve boundary. The callback executes immediately before the real reserve predicate and outside the Worker lock. The focused test callback invokes the existing public Worker shutdown API, deterministically transitioning ACCEPTING to STOPPING after the caller has been admitted/registered but before reserve. The production reserve function then rejects for its existing lifecycle predicate; no counters, operation state, registry entries, or reservation state are mutated by the test hook.

Two end-to-end named-pipe runtime tests were added to integrated-submit-adapter-v2-api-test.c:
- DIRECT: successful admission/registration, deterministic shutdown at the pre-reserve boundary, actual reserve rejection, discard_unissued, successful finalizer close/commit, caller-finish, callback suppression, zero retained records, zero caller-active/outstanding/reserved/registry counts, Worker join and Destroy.
- Adapter: successful Worker and Submitter admission, deterministic shutdown at the same pre-reserve boundary, actual reserve rejection, Submitter admission abort, caller submission failure, both owned-handle closes, finalizer commit, owner release, Worker finish, Adapter/Submitter/Worker orderly teardown, callback suppression, and zero retained/active/reserved/outstanding/registry state.

The hook type, storage, setter, and invocation are all compiled only under RAYA_ADAPTER_TESTING; no Production ABI, lifecycle, ownership, cleanup, or recovery behavior is changed. The prescribed API build script now enables that existing test macro and includes managed-pipe-io-v2.c for the DIRECT test. The script's cleanup list includes the additional object file.

Validation:
- API test build /W4 /WX: 10 strict dual-ACK process runs × 120 assertions and 10 full API-suite runs × 1,081 assertions across 68 cases; all PASS.
- Existing non-test-macro Managed Pipe native build /W4 /WX: 10 runs × 176 assertions; all PASS. This confirms the production compilation path excludes the test-only hook cleanly.
- Both build scripts completed their cleanup; declared EXE/OBJ outputs were absent after execution.
- git diff --check PASS.

Stage 5.18 is now COMPLETE for the two exact reserve-rejection cleanup branches (DIRECT discard_unissued and Adapter reserve-rejection cleanup). Stage 5.18 overall remains PARTIAL because runtime injection of each ownership-invariant guard and the combined Adapter+Submitter NOT_ISSUED notification-failure fixture remain unresolved. Stage 5.16 remains COMPLETE for the strict dual-post-terminal-ACK criterion; Stage 5.19 remains COMPLETE within the frozen independent per-plane v1 inventory contract; Stage 5.20 remains INCOMPLETE. Gate D remains NO ACTION APPROVED; Gate E BLOCKED; Gate F pending full Stage 5/Phase 1 owner acceptance and PR review. Stage 5/Phase 1 remain NOT ACCEPTED; Production remains NO-GO. No retained record was released, unlinked, replayed, reconciled, or freed by a recovery action.

## 2026-11-20 owner-approved process-isolated Adapter NOT_ISSUED notification-failure runtime test

The project owner explicitly approved a process-isolated test strategy for the combined Adapter + Submitter NOT_ISSUED notification-failure case, subject to build-artifact cleanup after testing. This supersedes the earlier feasibility decision that rejected relying on process exit without owner approval.

Added test_adapter_not_issued_notification_failure to integrated-submit-adapter-v2-api-test.c, selected by --not-issued-notification. The test uses only the existing injected Adapter/Submitter API tables; no Production source, header, ABI, callback, lifecycle, or recovery hook was added. The existing SetEvent test selector gained a separate NOT_ISSUED-specific arm: the injected ReadFile failure arms the selector, and the Submitter changed-event failure is injected only after the real issue callback has returned NOT_ACCEPTED and the request has transitioned to NOT_ISSUED. The Submitter's actual not_issued callback runs before the failed notification.

Assertions prove:
- Exactly one real ReadFile attempt and one targeted changed-event notification failure.
- Submitter lifecycle is DEGRADED for NOTIFICATION, request ID is 1, issue/publish effects are both NOT_ATTEMPTED, and queue/producer/active-issue/issuer-lease counts are all zero.
- Adapter callback is delivered exactly once; both Adapter-owned handles are closed; Adapter active/retained counts are zero.
- Worker remains ACCEPTING with caller-active, outstanding, reserved, registry, and retained counts all zero.
- Submitter thread has exited. Adapter is destroyed through its public API; Worker is shut down, joined, and destroyed through public APIs.
- No internal structure is zeroed, no Submitter-owned handle is manually closed, no destroy/recovery contract is bypassed, and no retained record is freed or reconciled.

The deliberately DEGRADED Submitter has no supported public destroy/recovery contract. Its thread is observed exited; the dedicated test process then terminates, allowing the OS to reclaim only that fixture's remaining process-local Submitter handles/storage. This is the explicitly approved isolation boundary, not a Production teardown/recovery behavior. Each invocation is a separate process.

The prescribed build-integrated-submit-adapter-v2-api-test.cmd now runs the new process-isolated mode once per each of its 10 iterations, alongside the existing strict dual-ACK process and full regression suite. Validation with Visual Studio 2026 BuildTools 18.7.3 x64 and /W4 /WX:
- NOT_ISSUED notification isolation: 10 process runs × 25 assertions = 250 assertions, all PASS.
- Strict dual-ACK isolation: 10 process runs × 120 assertions, all PASS.
- Full API regression: 10 runs × 1,081 assertions across 68 cases, all PASS.
- Build script cleanup completed; no .exe or .obj artifacts remain.
- git diff --check PASS.

This closes the previously split end-to-end Adapter+Submitter NOT_ISSUED notification-failure evidence gap. Stage 5.18 remains PARTIAL: the remaining bounded evidence gap is Worker-level retained-inventory publication for the native WAIT_FAILED/Core wake-failure scenarios; Core/queue quarantine tests do not by themselves prove Worker retained-list publication. Runtime fault injection for invariant guards remains intentionally excluded because it requires manufactured internal ownership/counter corruption; the primitive rejection/non-mutation contracts are covered by focused component tests. No Production behavior, positive recovery, retry, replay, retained-record release, or reconciliation was introduced. Stage 5.16 and 5.19 remain COMPLETE within their recorded strict/frozen criteria; Stage 5.20 remains INCOMPLETE; Gate D NO ACTION APPROVED; Gate E BLOCKED; Gate F pending full owner acceptance and PR review; Stage 5/Phase 1 NOT ACCEPTED; Production NO-GO.

## 2026-11-20 Stage 5.18 Worker retained-inventory gap reclassification

Re-read the checked-in MANAGED-WORKER-WAKE-FAILURE-ADAPTER-INTEGRATION-20261116.md and the exact test_worker_wake_failure_transferred_untracked implementation (already part of the RAYA_ADAPTER_TESTING API suite). This is not merely missing Worker inventory assertions: the test explicitly proves the transferred-but-unprocessed operation is Adapter-retained (adapter_count=1) while Worker retained inventory is empty (worker_count=0, w.retained_count=0, retained_head=NULL). At the same time it remains the Worker Core queue head in QUEUED state with queued/outstanding/reserved=1/1/1, registry count 0, owner REAPER_QUEUED, Submitter RUNNING with one issuer lease, Adapter active_count 1, and no user callback. The Worker thread is joined after the failed wake is externally kicked only for test observation. The operation/envelope/buffer/handles are deliberately left intact until process exit.

Source trace explains the boundary: retain_envelope publishes to Worker retained inventory only when Worker ownership was not established or Worker terminal release already occurred. In the wake-failure handoff case, Worker ownership has transferred and the operation remains in Core queue, so the Adapter is retained but the Worker retained list is not populated. The Worker queue and reservation still represent the operation in memory, but the v1 Worker retained-inventory API does not expose that queue-owned uncertain work. This is a confirmed transferred-but-unprocessed cross-plane inventory gap, not evidence that the operation was freed or unowned.

The separate native Core WAIT_FAILED tests exercise Core state and queue/registry invariants, not the Managed Worker thread's actual WaitForMultipleObjects failure branch. The Managed Worker calls Win32 WaitForMultipleObjects directly; current test-only seams cover wake signaling and lifecycle join APIs, not the wait call. Deterministically forcing WAIT_FAILED without closing/invalidation of a live handle (undefined/racy while a thread is waiting) therefore requires a narrowly scoped RAYA_ADAPTER_TESTING wait-result injection seam or a separately designed native fixture. No such seam was added in this pass.

Disposition: Stage 5.18 remains PARTIAL for (1) the confirmed transferred-but-unprocessed Adapter/Worker inventory gap on wake-signal failure and (2) missing Managed Worker-level native WAIT_FAILED runtime evidence. This review does not authorize or implement a Production retention/ownership semantic change, Worker inventory contract change, new test hook, cleanup, or recovery. The next bounded decision is whether to authorize a test-only wait-result seam for Managed Worker WAIT_FAILED coverage; the confirmed wake-failure gap requires a separate design decision before any fix. No retained record was released, unlinked, reconciled, or replayed. No tests/builds were run in this source/documentation-only pass; git diff --check PASS.
