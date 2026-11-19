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
