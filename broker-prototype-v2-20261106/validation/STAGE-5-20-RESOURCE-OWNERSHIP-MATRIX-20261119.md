# Stage 5.20 Source-Backed Resource Ownership and Failure-Class Matrix (2026-11-19)

## Purpose and decision boundary

This document translates delegated O-520-3 policy into a source-backed ownership map. It identifies which component controls each state/resource at the observed transition boundaries and what evidence is missing when a transition fails. It is **not** a reconciliation procedure, recovery API specification, or authorization to mutate retained state. Gate D remains authoritative: no evidence-specific reconciliation action is approved or proven safe.

Reviewed production sources (line numbers refer to this revision):

- `integrated-submit-adapter-v2.c`: Adapter envelope retention/free, Submitter callbacks, submit admission and cleanup (notably lines 139-204, 256-335, 400-548).
- `dedicated-io-submitter-v2.c`: queue/issue/publish/ACK lifecycle (notably lines 87-200, 560-590).
- `managed-pipe-io-v2.c`: DIRECT operation allocation, caller cleanup, handoff, terminal path and retention (notably lines 11-145 and the submit body following line 150).
- `reaper-managed-worker-v2.c`: Worker retained-list publication/inventory and Worker completion/finalization (notably lines 205-217, 220-280, 640-682).

## Ownership domains established by source

| Domain | Sole state/resource owner | Source-backed boundary | Explicit non-authority |
|---|---|---|---|
| Adapter envelope, Adapter retained-list membership/count, Adapter operation ID, client callback/context | Adapter | `retain_envelope` inserts the envelope under Adapter lock and records first cause/stage; `adapter_free` decrements active count and frees operation buffer/envelope; successful `adapter_worker_release` ACKs, invokes callback, then frees. | Worker retained-list membership or Worker terminal release does not prove Adapter envelope is reclaimable. |
| Worker operation lifecycle, Worker retained-list membership/count, Worker registry/reservation and Worker admission leases | Managed Worker / integrated Worker core | Worker publishes retained operations under Worker lock; Worker terminal path performs terminal hold, finalizer close/commit, owner release, integrated registry release, then calls release callback. | Adapter counts, Submitter state, or Worker v1 inventory alone do not authorize release. |
| Submitter request state, queue links/count, active producer/issue state, issuer lease, Issue/Publish effect and ACK transition | Dedicated Submitter | Submitter thread removes queued request, tracks ISSUE and PUBLISH effect separately, and moves request to ISSUED or UNCERTAIN; public admit/enqueue/abort/ack APIs enter Submitter API gate. | Submitter request state does not own or independently free the Adapter envelope or Worker operation. |
| Pipe owner/finalizer state and issuing/completion handles | Per-operation owner/finalizer state machine, driven by caller or Worker according to current operation ownership | Caller-side pre-issue cleanup or Worker terminal completion calls finalizer close/commit, then owner release. Worker path order is visible at `reaper-managed-worker-v2.c:656-681`; Adapter pre-issue cleanup at `integrated-submit-adapter-v2.c:308-335`; DIRECT caller terminal cleanup at `managed-pipe-io-v2.c:67-129`. | A retained-list entry, close attempt count, null handle, or component lifecycle flag alone is not a complete cross-plane effect ledger. No component may repeat close or infer release from counts. |

The Adapter's operation is embedded in its envelope; the Worker holds a reference to that operation while it owns it. After the Worker release callback boundary, the Worker promises not to touch the operation, but the Adapter envelope may remain retained if Submitter ACK fails (`integrated-submit-adapter-v2.c:193-204`). This is a real split lifetime, not shared ownership of the same free operation.

For DIRECT submissions, `managed_pipe_release` invokes the client callback and frees buffer/context/operation (`managed-pipe-io-v2.c:11-18`). The DIRECT caller owns the operation until successful Worker handoff; after handoff, Worker controls terminal progression and invokes the release callback only after its terminal release path (`managed-pipe-io-v2.c:137-145`, `reaper-managed-worker-v2.c:656-681`).

## Failure-class matrix

| Failure class / point | Owner at failure boundary | Source-observed committed or uncertain effects | Current disposition and missing proof |
|---|---|---|---|
| Adapter PRE_ISSUE_SETUP before Worker registration/admission | Adapter owns envelope and locally created buffer/handles; operation finalizer/owner tracks handle disposition | Event/duplicate-handle creation and operation initialization are staged before Worker registration (`integrated-submit-adapter-v2.c:400-435`). | QUARANTINE on failed close or invariant failure. Need exact per-handle close and owner/finalizer state evidence; no blind close retry or envelope free. |
| Adapter Worker admission rejected; caller-side cleanup | Adapter owns envelope; caller drives per-operation owner/finalizer; Worker owns Worker registry/admission state | Caller-submission-failed, finalizer close/commit, owner release, then envelope free are sequential; any failed step retains (`integrated-submit-adapter-v2.c:437-455`). | QUARANTINE on any failed transition. A completed earlier step is not rolled back; no generic cleanup restart. |
| Adapter Submitter admission rejected after Worker admission | Adapter owns envelope; Submitter owns admission result; caller drives owner/finalizer cleanup; Worker tracks admission lease | Abort/owner/finalizer/owner-release/Worker-finish/free occur as ordered steps (`integrated-submit-adapter-v2.c:456-472`). | QUARANTINE if any step fails. Missing exact cross-plane commit ledger prevents safe repair. |
| Adapter Worker reserve rejected after Submitter admission | Adapter owns envelope; Submitter owns admission until abort; Worker owns reserve/admission accounting; caller executes cleanup | Submitter abort, caller-submission-failed, finalizer close/commit, owner release, Worker finish, free (`integrated-submit-adapter-v2.c:474-491`). | QUARANTINE on any failed step. Existing seam review found no deterministic supported callback at the admission-to-reserve boundary; no synthetic race test. |
| Adapter enqueue rejected / backpressure | Adapter owns envelope; Submitter owns admission/queue state; Worker owns reservation | Submitter abort precedes caller-terminal transition, integrated caller-terminal, finalizer close/commit, owner release, unreserve, Worker finish, then free (`integrated-submit-adapter-v2.c:492-548`). | QUARANTINE on any failed transition. Earlier finalizer/owner commits may already be irreversible; do not retry remaining sequence generically. |
| Submitter Issue NOT_ACCEPTED | Submitter owns request transition; Adapter callback `adapter_not_issued` performs caller-side cleanup; Worker owns its reservation/admission records | Submitter marks NOT_ISSUED only after validating issue state/leases, then calls not-issued callback (`dedicated-io-submitter-v2.c:140-161`). Adapter callback performs owner/finalizer/unreserve/finish/free (`integrated-submit-adapter-v2.c:281-335`). | This is a determinate path only when Submitter's NOT_ISSUED contract is intact. If any callback/cleanup/accounting step fails, QUARANTINE. Never infer NOT_ISSUED from a stage label. |
| Submitter Issue uncertain or Publish uncertain | Submitter owns request/effect ledger; Adapter envelope remains allocated; Worker may or may not have accepted handoff depending on publish substep | Submitter records UNKNOWN and degrades on invalid Issue result or failed Publish (`dedicated-io-submitter-v2.c:163-190`). Adapter publish distinguishes not-transferred after OS acceptance and retains; transferred path marks Worker ownership (`integrated-submit-adapter-v2.c:256-278`). | QUARANTINE; NEVER REPLAY. Need authoritative issue and transfer effect evidence; local OS acceptance is not recipient commit. |
| Worker DIRECT or Adapter-associated completion uncertain / terminal hold | Worker owns operation lifecycle and retained Worker record; Adapter retains its envelope if associated | Worker observes completion/owner collection and terminal-holds before finalizer close (`reaper-managed-worker-v2.c:640-655`). | QUARANTINE. No finalizer close, owner release, registry release, callback, or I/O retry is implied by terminal hold. |
| Worker finalizer close or finalizer commit failure | Worker owns operation lifecycle; finalizer state machine controls close/commit facts; Adapter envelope remains alive for associated operation | Worker calls close then commit; failure returns before owner release, registry release, and callback (`reaper-managed-worker-v2.c:656-662`). | QUARANTINE; NO BLIND CLOSE RETRY. Need per-handle irreversible-effect proof and exact finalizer state; current v1 record does not provide this complete ledger. |
| Worker owner release failure after finalizer commit | Worker owns operation lifecycle; per-operation owner state tracks release | Finalizer may already be committed before owner release fails (`reaper-managed-worker-v2.c:656-668`). | QUARANTINE. Do not repeat finalizer or synthesize owner release; partial effects are irreversible. |
| Worker integrated registry release failure after finalizer and owner release | Worker owns registry/reservation state; operation's finalizer and owner commits already occurred | Registry release follows finalizer and owner release (`reaper-managed-worker-v2.c:664-675`). | QUARANTINE. Do not repeat earlier commits or manually unlink/decrement registry state. |
| Worker release callback / Adapter Submitter ACK failure (POST_TERMINAL) | Worker has completed its operation-side release boundary; Adapter owns remaining envelope; Submitter owns ACK/request state | Worker invokes release callback only after its terminal release sequence (`reaper-managed-worker-v2.c:678-681`). Adapter marks terminal release, ACKs; on ACK failure retains envelope and suppresses client callback/free (`integrated-submit-adapter-v2.c:193-204`). | QUARANTINE; NEVER REISSUE I/O. Worker operation handles/registry may already be released; Submitter ACK and client callback are separate. Need authenticated/idempotent ACK evidence and explicit envelope disposition before any future action. |
| Adapter/Worker ownership invariant, partial record, identity-less or conflicting evidence | No single action owner can be proven from available record | First-retain cause/stage and inventory identify a diagnostic classification, not every committed effect (`integrated-submit-adapter-v2.c:139-180`; Worker inventory validates identity/shape but is read-only at `reaper-managed-worker-v2.c:220-280`). | QUARANTINE; NO ID-BASED ACTION. Missing or conflicting ownership/effect evidence is itself disqualifying. |
| Orchestrator shutdown/cursor or cross-plane partial commit | Orchestrator owns its orchestration cursor/lifecycle; underlying Adapter/Worker/Submitter retain their own state | The source review and Gate D record establish no safe cursor mutation or cross-plane rollback. | Preserve cursor and component states; no cursor skip/reset, forced teardown, or fabricated empty count. |

## Invariants for any future action contract

1. Exactly one component owns each mutable state/resource at a given transition boundary. A reference or inventory correlation is not ownership transfer.
2. Ownership transfer must be explicit, validated, and ordered; after transfer, the previous owner must not free or mutate the transferred object.
3. Finalizer close, finalizer commit, pipe-owner release, integrated registry release/unreserve, Submitter ACK, client callback, and envelope free are separate effects. A later failure does not roll back earlier effects.
4. Submitter Issue and Publish effects are distinct. `ISSUE_ACCEPTED` / OS completion do not prove recipient-level WRITE commit.
5. Worker post-terminal release and Adapter post-terminal envelope release are distinct. Worker v1 and Adapter v1 inventories are independent and are not an atomic cross-plane snapshot.
6. Missing, stale, partial, conflicting, identity-less, or non-authenticated evidence means quarantine.
7. No ID-only release, list unlink, counter adjustment, repeated close, callback/ACK retry, I/O replay, or Orchestrator cursor mutation is permitted by this matrix.

## Remaining evidence gaps and bounded next work

- The production v1 inventories do not expose a complete immutable per-record effect ledger across Issue, Publish/handoff, completion, each handle close, finalizer commit, owner release, integrated release/unreserve, Worker finish, Submitter ACK, callback, and envelope free.
- There is no recipient-side authenticated receipt/status or stable idempotency protocol for uncertain WRITE.
- Existing supported seams do not deterministically reach the exact Adapter/ DIRECT reserve-rejection interval; no new seam is introduced by this documentation pass.
- The current Stage 5.20 safe scope remains source-backed clarification and negative quarantine/no-replay evidence only. Gate D remains complete with no action approved; positive recovery tests and mutation remain blocked.

## Verification and status

Documentation-only source review. No source/header/API/test/build/lifecycle/ownership/cleanup/recovery/retry/callback/replay behavior changed. No tests or builds were run. No retained resource was modified, released, retried, replayed, or deleted.

- O-520-3: DECIDED at policy level; source-backed ownership domains and failure boundaries mapped here. This does not approve an action.
- Gate D: COMPLETE; no reconciliation action approved.
- Gate E: BLOCKED; positive recovery actions/tests remain unauthorized and unsupported.
- Gate F: remains subject to remaining acceptance evidence and PR review; not passed by this document.
- Stage 5.20: INCOMPLETE. Stage 5 / Phase 1: NOT ACCEPTED. Production: NO-GO.

## Production-v1 field coverage versus the per-record effect ledger

This subsection resolves the next bounded question by comparing the actual public production record layouts with the underlying in-memory state and the test-only diagnostic surface. “Available internally” below means process-local mutable state observed by source inspection; it is not a durable, authenticated, immutable audit record.

| Effect / evidence item | Production Adapter v1 | Production Worker v1 | Other current source surface | Ledger conclusion |
|---|---|---|---|---|
| Stable per-process Adapter identity and operation ID | PRESENT (`adapter_instance_id`, `adapter_operation_id`) | PRESENT only for Adapter-associated records (`adapter_identity_valid` and pair) | Adapter counters are process-local | Correlation identity exists; it is not durable across restart. |
| Worker identity / operation identity | NOT PRESENT | PRESENT (`identity_valid`, `worker_epoch`, `worker_operation_id`) | Worker operation fields are mutable in-memory state | Worker correlation exists only in Worker v1. |
| First retain cause and stage | PRESENT | PRESENT | Retained nodes hold first-retain values | Classification evidence exists; it is not a complete effect history. |
| Submitter request ID and request state | NOT PRESENT | NOT PRESENT | `raya_submitter_request` has `request_id` and `state`; Submitter snapshot exposes only aggregate lifecycle/counts | Per-request state is not exposed by either Production retained inventory. |
| Issue and Publish effect state | NOT PRESENT | NOT PRESENT | Request has `issue_effect` and `publish_effect`; Submitter keeps first degraded request/effect diagnostics; Adapter test-only snapshot copies effects | Internal per-request states exist, but are absent from Production v1 and are not a durable receipt. |
| OS completion observation / transferred bytes / completion error | NOT PRESENT | NOT PRESENT | Worker operation has `transferred`, `completion_error`, collection state and retry observations; Adapter test-only snapshot copies selected completion values | Current Production v1 does not expose completion evidence. |
| Per-handle close disposition (issuing pipe vs completion event) | NOT PRESENT | NOT PRESENT | Finalizer has aggregate state, attempts and successful-close count; it stores both handles internally; Adapter test-only snapshot copies aggregate counters | No per-handle result/effect record in Production v1. Aggregate successful-close count cannot identify which handle's close committed. |
| Finalizer release commit | NOT PRESENT | NOT PRESENT | `raya_pipe_finalizer.release_commits`; Adapter test-only snapshot copies it | Internal state exists; not part of Production v1. |
| Owner state / release commit | NOT PRESENT | NOT PRESENT | `raya_pipe_owner.state` and `release_commits`; Adapter test-only snapshot copies these | Internal state exists; not part of Production v1. |
| Integrated reservation state and registry membership | NOT PRESENT | NOT PRESENT | `raya_integrated_op.reservation_state`, registry and reserved-slot counts; Adapter test-only snapshot copies selected state/counts | No per-record reservation/registry effect proof in Production v1. Aggregate counts do not prove a particular operation's release. |
| Worker caller-finish / admission lease transition | NOT PRESENT | NOT PRESENT | Worker/integrated counters and transition return values | No immutable per-operation finish result in Production v1. |
| Submitter ACK result / ACK terminality | NOT PRESENT | NOT PRESENT | Request state becomes ACKED or remains retained; `raya_submitter_acknowledge` can return failure when terminal ACK state is set but lifecycle signaling fails | Production inventory cannot distinguish ACK state from later signal failure or provide a verifiable ACK receipt. |
| Client callback invocation/result | NOT PRESENT | NOT PRESENT | Adapter release callback invokes callback after successful ACK; callback is `void` | No callback receipt/result exists; invocation cannot be reconstructed from v1 inventory. |
| Adapter envelope free / buffer reclamation | NOT PRESENT | NOT PRESENT | `adapter_free` mutates active count and frees memory; retained membership/count are observable | Freed objects are absent by definition; v1 is not a historical ledger. |
| Ownership transfer / handoff substeps | NOT PRESENT | NOT PRESENT | Handoff result and operation owner state are in-memory; test-only snapshot has worker-owned/terminal-released flags | No complete immutable per-step transfer record in Production v1. |
| Recipient-side WRITE receipt, deduplication, or status | NOT PRESENT | NOT PRESENT | No recipient protocol found in reviewed contract | Fundamentally absent; OS acceptance/completion is not recipient commit. |

### Surface boundary

- `raya_adapter_retained_record_v1` contains only `struct_size`, `version`, Adapter instance/operation IDs, first-retain cause, and first-retain stage.
- `raya_worker_retained_record_v1` contains only `struct_size`, `version`, Worker identity/kind, optional Adapter identity pair, and first-retain cause/stage.
- The richer `raya_adapter_test_retained_item` is guarded by `RAYA_ADAPTER_TESTING`; it is a test-only diagnostic snapshot, not a Production API or stable recovery contract. Its selected effect/counter fields must not be represented as available to a Production reconciler.
- `raya_submitter_snapshot` reports aggregate lifecycle/queue/producer/issue/issuer counts. It does not export a copied per-request ledger. The request's mutable effect fields are Submitter-owned after admission.
- Owner, finalizer, integrated reservation/registry and Worker operation fields are in-memory state. None of the reviewed Production v1 inventory records preserves a chronological sequence of effect attempts/results, actor, evidence reference, or durable commit boundary.

### Resulting gap classification

1. **Already available for read-only classification:** process-local identity pairs, inventory kind, and immutable first-retain cause/stage in the two independent Production v1 inventories.
2. **Available only as mutable in-process state or test-only diagnostics:** request/effect states, completion values, owner/finalizer counters, reservation/registry state, and selected Worker/Submitter aggregate counters. These are not a Production v1 per-record effect ledger.
3. **Not represented by the current implementation contract:** per-handle close outcomes, immutable chronological effect records, callback receipt, authenticated ACK evidence, durable identity/effect history across restart, and recipient-side WRITE receipt/status/idempotency.
4. **No safe retrofit is authorized in this step:** adding an effect ledger changes production API/implementation and requires a separate reviewed contract, ownership, durability, privacy, and test plan. This mapping does not authorize that implementation or any reconciliation action.

The exact evidence gap is therefore not simply “missing a combined snapshot”: it is missing immutable per-record effect outcomes and trustworthy external-effect evidence. Independent Adapter/Worker snapshots remain intentionally separate; combining their current fields would still not manufacture the absent effect history.

## Negative no-replay evidence at the Submitter boundary

The existing Submitter failure-matrix test now counts the injected callbacks for the two ambiguous-effect branches without changing production code or callback semantics:

- **Issue outcome uncertain:** the Issue callback is observed exactly once; Publish is observed zero times; `not_issued` is observed zero times; request state is `UNCERTAIN`; Issue effect is `UNKNOWN`; Publish remains `NOT_ATTEMPTED`; Submitter records `ISSUE_UNCERTAIN` and exits its worker thread. This prevents treating an uncertain Issue as NOT_ISSUED and provides a direct assertion against a second Issue or Publish invocation during this worker execution.
- **Publish outcome uncertain after accepted Issue:** Issue is observed exactly once; Publish exactly once; `not_issued` zero times; request state is `UNCERTAIN`; Issue effect is `COMMITTED`; Publish effect is `UNKNOWN`; Submitter records `PUBLISH_UNCERTAIN` and exits its worker thread. This prevents an automatic second Issue/Publish attempt in this path.

Validation used the existing `build-dedicated-io-submitter-v2-failure-test.cmd` script with VS 2026 BuildTools 18.7.3 x64 and `/W4 /WX`: 25 consecutive passes, 302 assertions per pass (7,550 assertion executions). The test uses the existing public Submitter issue/publish/not-issued callback seams. Production source/header/API and runtime behavior are unchanged.

This evidence is intentionally narrow: it proves no callback replay within these Submitter worker executions. It does not prove recipient-side WRITE commit, durable no-replay across process restart, or end-to-end Adapter/Worker recovery. Those remain unsupported and unauthorized. No new positive recovery/reconciliation action is implied.

### Admission remains closed after an uncertain Submitter effect

The same two negative runtime tests also verify the next-operation boundary after the Submitter worker has exited on an uncertain effect. In both Issue-uncertain and Publish-uncertain cases, lifecycle remains `RAYA_SUBMITTER_DEGRADED`; a fresh zero-initialized request is rejected by the existing public `raya_submitter_admit` API, retains `request_id == 0` and `BUILDING` state, and the previously counted Issue/Publish callback totals remain unchanged. Thus the test proves both (a) no second callback for the uncertain request and (b) no new Submitter admission after the uncertainty has degraded that Submitter instance. This is a Submitter-local, same-process negative guarantee only; it does not assert global Adapter/Worker admission state, queued-request disposition, recipient commit, or restart behavior.

The prescribed failure-matrix script passed 25 consecutive VS 2026 BuildTools 18.7.3 x64 `/W4 /WX` runs, now 310 assertions per run (7,750 total assertion executions). Test-only; no Production code or runtime semantics changed.

## Negative queued-work quarantine after uncertain Submitter Issue

The existing Issue callback seam now deterministically holds the first Issue while a second request is admitted and queued. The first callback then returns `RAYA_SUBMITTER_ISSUE_UNCERTAIN`. The runtime test observes all of the following after the Submitter worker exits:

- First request remains `UNCERTAIN`, Issue effect `UNKNOWN`, Publish `NOT_ATTEMPTED`.
- Second request remains `QUEUED`; Submitter queue count remains one. It is not issued by a second callback and is not converted to `NOT_ISSUED`.
- `active_issue` is zero, but the issuer lease remains one; the captured degraded snapshot records queue=1, active_issue=0, issuer_leases=1. The lease is intentionally retained under the uncertain Issue contract.
- Issue callback count is exactly one; Publish and NOT_ISSUED callback counts are zero.

The deterministic sequence uses only existing public admission/enqueue/snapshot APIs and the existing Issue callback seam; no internal state mutation, new fault hook, or Production change. The prescribed VS 2026 BuildTools 18.7.3 x64 `/W4 /WX` failure-matrix build passed 25 consecutive runs × 332 assertions = 8,300 assertion executions. This is negative same-process Submitter evidence only. The queued request's eventual disposition remains deliberately unresolved/quarantined; this test does not authorize draining, aborting, releasing, retrying, or replaying it, and provides no recipient-side WRITE or restart evidence.

## Negative queued-work quarantine after uncertain Publish

The existing Publish callback seam now deterministically holds request 1 after its Issue callback returns ACCEPTED and before Publish returns uncertain/failure. While Publish is held, request 2 is admitted and queued. The test then releases Publish to fail and observes the Submitter worker exit DEGRADED. Assertions establish: request 1 is UNCERTAIN with Issue COMMITTED and Publish UNKNOWN; request 2 remains QUEUED and is neither issued nor marked NOT_ISSUED; queue count remains one; `active_issue` returns to zero while `issuer_leases` remains one; Issue callback count is exactly one, Publish callback count exactly one, and NOT_ISSUED count zero. The captured degraded snapshot preserves queue=1, active_issue=0, issuer_leases=1. The test uses only existing public APIs and callbacks, with deterministic Win32 event synchronization; no new hook, internal state mutation, or Production change.

Prescribed VS 2026 BuildTools 18.7.3 x64 `/W4 /WX` failure-matrix build passed 25 consecutive runs × 356 assertions = 8,900 assertion executions. This is same-process Submitter evidence only. Queued work remains unresolved/quarantined; no drain, abort, release, retry, replay, restart recovery, or recipient-side WRITE claim is supported. Stage 5.20 remains INCOMPLETE; Gate D no action approved; Gate E blocked; Phase 1 NOT ACCEPTED; Production NO-GO.
## ACK notification failure: no second ACK transition or notification

The existing injected `changed_event` SetEvent failure test now re-invokes the public `raya_submitter_acknowledge` API after the first call has already moved the request to ACKED, consumed its issuer lease, and degraded the Submitter. The second call is rejected; request state remains ACKED; the injected SetEvent callback count is unchanged; queue, producer, active issue, and issuer lease counts remain zero in the degraded snapshot. This establishes that the ACK transition/notification is not re-applied by a second public ACK call in this same process. It does not establish that an external caller observed the ACK, that an ACK receipt is durable, or that recovery across process restart is possible.

Prescribed VS 2026 BuildTools 18.7.3 x64 `/W4 /WX` failure-matrix build passed 25 consecutive runs × 361 assertions = 9,025 assertion executions. Test-only; no Production source/header/API/runtime behavior changed. Stage 5.20 remains INCOMPLETE; Gate D no action approved; Gate E blocked; Phase 1 NOT ACCEPTED; Production NO-GO.
## NOT_ISSUED abort notification failure: repeated abort is rejected

The `test_abort_set_failure` case now repeats the public `raya_submitter_abort_admission` call after the first call has moved the request to `NOT_ISSUED` and the `changed_event` notification failed, degrading the Submitter. The second call is rejected; request state remains `NOT_ISSUED`; injected `SetEvent` callback count is unchanged; the degraded snapshot remains empty (queue, producers, active issue, issuer leases all zero). This is same-process evidence that the NOT_ISSUED transition/notification is not re-applied by a repeated abort call. It is not durable callback receipt or proof of external caller observation.

Prescribed VS 2026 BuildTools 18.7.3 x64 `/W4 /WX` failure-matrix build passed 25 consecutive runs × 366 assertions = 9,150 assertion executions. Test-only; no Production source/header/API/runtime behavior changed. Stage 5.20 remains INCOMPLETE; Gate D no action approved; Gate E blocked; Phase 1 NOT ACCEPTED; Production NO-GO.
## Worker-originated NOT_ISSUED callback: ownership and retry boundary

Source order in `submitter_main` confirms that `ISSUE_NOT_ACCEPTED` first releases the active-issue and issuer-lease counts and commits the request to `NOT_ISSUED`, then signals `changed_event`. If that notification fails, the Submitter is degraded; after releasing the lock, the worker invokes `not_issued` once and exits. The existing `test_not_accepted_notification_failure` deterministically verifies this callback count is one after the worker exits and the request/snapshot states are terminal/degraded as expected.

This callback is not the same operation as public `raya_submitter_abort_admission`, which only transitions an `ADMITTED` request while `active_producers>0`. There is no public callback-replay API, and the callback signature returns `void`; therefore no receipt/result establishes external observation or durable completion. Preserve the no-replay boundary: do not add a retry or new hook to manufacture a second callback. The remaining gap is callback-side effect/receipt evidence, not missing proof that the current worker invokes it once. Documentation only; no Production or test changes in this review. Stage 5.20 remains INCOMPLETE; Gate D no action approved; Gate E blocked; Phase 1 NOT ACCEPTED; Production NO-GO.

## Adapter NOT_ISSUED callback side-effect chain and testability boundary

The real `adapter_not_issued` callback is an operational cleanup routine, not merely a notification. In order it performs owner submission-failed transition, Worker caller-terminal accounting, finalizer close, finalizer release commit, owner release, Worker reservation unreserve, Worker caller-finish, user completion callback, and Adapter envelope free. Any intermediate ownership/finalizer/accounting failure retains the envelope and marks degradation; no retry is authorized.

Evidence remains split: integrated Adapter `test_immediate_read_failure` proves the real NOT_ISSUED cleanup and user callback when Submitter notification succeeds; Submitter `test_not_accepted_notification_failure` proves generic callback invocation once when changed-event notification fails. There is no combined end-to-end assertion. Existing injected Submitter `set_event` and ACK-state registration seam could target the NOT_ISSUED notification, but the resulting Submitter is DEGRADED and public Destroy does not accept its initial `destroy_progress` state. No safe same-process teardown exists through the public contract. Do not force internal destroy state, close handles directly, or add a retry. Combined Adapter+Submitter notification-failure evidence and external callback receipt remain gaps. This is a fixture/lifecycle-contract blocker, not evidence that the callback chain fails. Documentation only; no code/test/build changes. Stage 5.20 remains INCOMPLETE; Gate D no action approved; Gate E blocked; Phase 1 NOT ACCEPTED; Production NO-GO.

## 2026-11-20 serialized Worker terminal-dispatch constraint (Stage 5.16)

The current managed_worker_main dispatches one resolved operation per loop iteration. After that operation's finalizer/owner/registry release, it invokes one release callback and returns to the loop. If the Adapter release callback cannot ACK, retain_envelope marks the shared Worker DEGRADED; the next loop iteration exits before another operation can be claimed or terminally released. Thus one shared Worker cannot produce two independent Adapter post-terminal ACK-failure callbacks under the present fail-closed serialized contract. The existing dual-retention test's second record is a post-degradation admission-rejected/pre-issue cleanup retention, not a second OS-issued ACK failure. No test hook or production change is justified to bypass this boundary. Any revised target needs an explicit architecture and safety-contract decision.

## 2026-11-20 Stage 5.18 test-evidence level distinctions

The native WAIT_FAILED test directly calls `WaitForMultipleObjects` on a built wait plan, closes the disposable wake event, marks the integrated core DEGRADED, and verifies registry/outstanding/reservation preservation. It does not run the Managed Worker or publish a Worker retained-list record. The native wake-failure test transfers 70 real named-pipe operations, injects `SetEvent(INVALID_HANDLE_VALUE)`, directly degrades the integrated core, and explicitly starts no Worker; it proves queued-core quarantine, not Worker retained-list publication. By contrast, the Adapter `test_worker_wake_failure_transferred_untracked` runs a real Managed Worker and Adapter, observes Worker wake callback failure and join, and verifies Adapter-local retention while Worker retained inventory is empty and the transferred operation remains queued/outstanding/reserved. Keep these evidence claims separate.

These distinctions do not resolve the exact DIRECT `discard_unissued` or Adapter reserve-rejection cleanup gaps: both lack a deterministic existing seam between admission/registration and reserve. Nor do primitive invalid-transition tests establish runtime fault injection of ownership-invariant guards. No new hook, internal state mutation, or Production behavior change is approved. Stage 5.18 remains PARTIAL.

## 2026-11-20 consolidated acceptance cross-reference

The authoritative Stage 5 acceptance gap register is in `STAGE-5-16-TO-5-20-EXECUTION-RESULTS-20261118.md` under **2026-11-20 consolidated Stage 5 acceptance gap register**. It reconciles the latest 5.16 serialized Worker ACK boundary, 5.18 evidence-level distinctions, 5.19 dependency, and 5.20 ownership/effect-ledger audit. O-520-1..6 are DECIDED at policy level; this does not authorize positive recovery. Gate D remains no action approved, Gate E blocked, Gate F pending explicit acceptance/review; Stage 5/Phase 1 remain NOT ACCEPTED and Production remains NO-GO.

This cross-reference is documentation-only; no tests/builds or Production changes were made.

## Public lifecycle API audit: teardown is not reconciliation (2026-11-20)

A focused audit of the public Adapter, Worker, and Submitter lifecycle APIs resolves whether an existing supported API can safely reconcile retained/uncertain operations. It cannot. The APIs expose admission closure, diagnostic inventory/snapshots, orderly shutdown, join, and guarded destruction; none supplies a per-operation effect receipt or a retained-record release/reconciliation transition.

| Public API surface | Source-backed behavior | Why it is not a reconciliation primitive |
|---|---|---|
| Adapter `raya_integrated_adapter_copy_retained_inventory_v1` / retained count | Copies the Adapter's retained identity, first cause, and stage under the documented quiescence contract; count is diagnostic. | Read-only only. It does not inspect or prove each underlying effect, change ownership, or authorize freeing an envelope. |
| Adapter `raya_integrated_adapter_destroy_ex` (`integrated-submit-adapter-v2.c:580-634`) | Closes Adapter admission, reports BUSY while API entrants remain, then requires `active_count==0` and `retained_count==0` with a consistent retained-list head/count before destroying the lock and zeroing storage. | A retained envelope is a hard stop, not an item this API releases. It does not stop or join Worker/Submitter and cannot repair a non-empty retained list. |
| Worker `raya_managed_worker_copy_retained_inventory_v1` | Copies Worker retained identities and first cause/stage. | Read-only inventory; no unlink, release, unreserve, or counter adjustment API is exposed by this inventory operation. |
| Worker `raya_managed_worker_shutdown_ex` (`reaper-managed-worker-v2.c:706-747`) | Requests orderly shutdown from ACCEPTING; accepts STOPPING/STOPPED; a DEGRADED/FAILED lifecycle returns terminal failure with `RETRY_RECONCILE`. | The retry classification is not an implemented reconciliation action. In DEGRADED/FAILED, this API does not transition the retained work to a safe terminal disposition. |
| Worker `raya_managed_worker_destroy_ex` (`reaper-managed-worker-v2.c:807-840`) | Requires joined thread, STOPPED lifecycle, zero caller-active/outstanding/queued/reserved/registry state, and empty Worker retained list before closing wake event and destroying storage. | Retained or outstanding work blocks destruction. It is not cleared or released by Destroy. |
| Submitter `raya_submitter_snapshot` / `raya_submitter_admission_close` (`dedicated-io-submitter-v2.c:545-590`) | Snapshot returns aggregate lifecycle/queue/producer/issue/lease counts. Admission close sets the API-gate closed bit and reports active entrants. | Neither API exports per-request receipts nor disposes queued/uncertain requests or leases. Admission closure is not queue drain or reconciliation. |
| Submitter `raya_submitter_shutdown_ex` (`dedicated-io-submitter-v2.c:387-423`) | Signals orderly shutdown from RUNNING/STOPPING; a notification failure degrades the Submitter. | Does not resolve UNCERTAIN requests or release queued work/issuer leases. A DEGRADED state does not become a reconciled state by calling shutdown again. |
| Submitter `raya_submitter_destroy_ex` (`dedicated-io-submitter-v2.c:478-530`) | Requires joined thread and empty queue/producers/active issue/issuer leases. It accepts STOPPED with intact handles, or only the specific DEGRADED partial-destroy progress states 3/1; then closes the remaining lifecycle handles. | It cannot destroy while uncertain/queued/leased work remains. The allowed DEGRADED states describe partial handle teardown, not operational recovery. Other DEGRADED states (including the NOT_ISSUED notification-failure fixture at destroy_progress 0) are rejected. |

### Lifecycle conclusions

1. **Admission close is not stop, drain, or release.** It prevents new API entrants after the gate closes and reports entrants already inside; it does not settle queued work or retained operations.
2. **Shutdown is not recovery.** It signals an orderly stop only along supported lifecycle transitions. Worker DEGRADED/FAILED is terminal for this shutdown API; Submitter uncertainty remains quarantined.
3. **Join is not effect proof.** It establishes thread termination and handle-reaping status, not recipient WRITE commit, callback receipt, ACK observation, or per-handle close disposition.
4. **Destroy is guarded reclamation, not reconciliation.** Adapter/Worker/Submitter destroy preconditions explicitly reject retained, outstanding, queued, or leased work. A successful destroy is possible only after those conditions are already satisfied by legitimate prior transitions.
5. **`RETRY_RECONCILE` is a classification label, not a callable procedure.** No public API in the reviewed headers performs safe per-record reconciliation or provides the missing immutable effect/recipient receipt evidence.

**Disposition:** no existing public API closes Stage 5.20's positive-recovery gap. Keep Gate D as NO ACTION APPROVED and preserve process-lifetime quarantine. Do not call shutdown/destroy as a workaround, alter lifecycle/counters/list membership, or infer that a retry classification authorizes a mutation. Any future reconciliation API requires a separate owner-approved effect/receipt contract and review; it is not part of this Stage 5 pass.

Source review only; no code, headers, tests, build scripts, lifecycle behavior, or recovery behavior changed. No tests/builds were run.

## Adapter NOT_ISSUED + Submitter notification-failure composition review (2026-11-20)

The requested combined failure path was traced across production code and existing tests. The evidence is compositional, not end-to-end:

- `integrated-submit-adapter-v2.c:281-333` implements `adapter_not_issued`: it transitions the pipe owner to caller-submission-failed, records the integrated caller terminal state, closes the operation's handles, commits finalizer release, releases pipe ownership, unreserves the Worker slot, finishes the caller lease, invokes the user completion callback, and frees the Adapter envelope. Any failed ownership/finalizer/unreserve/finish step retains the envelope and degrades the Worker instead of continuing cleanup.
- `dedicated-io-submitter-v2-failure-test.c:test_not_accepted_notification_failure` (`:639-660`) proves the Submitter invokes its configured NOT_ISSUED callback once, the request remains `RAYA_SUBMITTER_REQ_NOT_ISSUED`, aggregate queue/producer/issue/lease counts reach zero, and a subsequent notification failure leaves the Submitter DEGRADED. Its callback is a counted test callback, not the Adapter callback.
- `integrated-submit-adapter-v2-api-test.c:test_immediate_read_failure` (`:362-387`) exercises the Adapter-level immediate issue-rejection/NOT_ISSUED path and proves callback delivery, both handle closes, zero Adapter active/retained count, and orderly component stop. `test_finalizer_close_failure_retains` (`:1140-1230`) separately proves that a close failure on the same NOT_ISSUED class is retained rather than freed. Neither case injects the Submitter notification failure.
- `raya_integrated_adapter_start_with_apis` (`integrated-submit-adapter-v2.c:340-360`) accepts a Submitter Win32 API table, and the adapter API test already supplies an injected Submitter SetEvent implementation. Thus a combined test can be built using the existing API injection boundary; however, the current injected SetEvent failure selector is specifically tied to the ACK failure scenario (`test_submitter_set_event`, `integrated-submit-adapter-v2-api-test.c:174-188`). No existing test currently composes the Adapter NOT_ISSUED callback with the later Submitter notification failure.

**Evidence conclusion:** source ordering supports that Adapter NOT_ISSUED cleanup and the user callback execute before the later notification failure is surfaced, but the combined runtime postconditions (Adapter active/retained counts, Worker outstanding/reserved/registry/caller-active state, callback count, Submitter DEGRADED state and zero queue/lease counts) are not yet asserted in one end-to-end test. Do not promote this path to complete evidence. This is a bounded test-evidence gap, not evidence of a production defect. No Production behavior or recovery contract should change to close it. Any follow-up test should use the existing injected API table and assert only legitimate observed state; it must not manually release retained resources or use internal state mutation.

Disposition for Stage 5.20: this composition remains PARTIAL evidence. Submitter notification failure after a NOT_ISSUED callback is not equivalent to successful reconciliation; Submitter DEGRADED lifecycle and destroy restrictions remain. Gate D remains NO ACTION APPROVED.

### Combined NOT_ISSUED notification-failure test feasibility decision (2026-11-20)

The existing injected Submitter API table can technically induce the notification failure after the real Adapter `adapter_not_issued` callback; no Production hook is required. The current SetEvent selector is ACK-specific, but a test-local selector could target the relevant changed-event call. The blocker is safe fixture teardown: the resulting Submitter is DEGRADED at destroy-progress 0, and the public Submitter destroy API rejects that state. The Adapter/Worker public shutdown/destroy contracts do not provide a safe way to dispose of this deliberately degraded fixture. Using raw handle closes, structure zeroing, forced cleanup, or process-exit-only cleanup would bypass the lifecycle contract and invalidate the test's safety claim.

Decision: do not add the test or expand the selector in this Stage 5 pass. Keep the combined runtime postconditions PARTIAL. The path is technically reachable; it is not proven unreachable. A future test requires an owner-reviewed fixture/lifecycle contract or an explicitly accepted process-isolated test strategy. This does not authorize Production recovery behavior or forced teardown. No tests/builds or code changes were made.
