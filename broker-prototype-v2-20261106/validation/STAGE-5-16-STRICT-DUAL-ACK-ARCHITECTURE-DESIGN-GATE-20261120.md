# Stage 5.16 Strict Dual-ACK Architecture Design Gate - 2026-11-20

## Owner decision

The project owner selected **preserve the strict Stage 5.16 acceptance criterion**. The criterion is not weakened, replaced by mixed-stage retention, or treated as optional stress coverage.

Acceptance still requires all of the following on one shared Managed Worker:
- two distinct Adapter/Submitter instances;
- two distinct operations independently accepted/issued by the operating system;
- each operation independently reaches its own terminal completion and post-terminal Submitter ACK failure;
- each failure is retained with its own stable Adapter/Worker identity and first-cause evidence;
- no fabricated operation, synthetic retained record, lifecycle mutation, forced cleanup, replay, or bypass of fail-closed ownership rules.

Stage 5.19 remains dependent on this exact Stage 5.16 proof.

## What this decision authorizes

It authorizes a separately reviewed architecture change to satisfy the unchanged criterion while preserving fail-closed isolation. Candidate A is selected by the later architecture review decision in this document for implementation in the broker-prototype branch. It does not authorize Production changes or weakening any lifecycle/recovery contract.

## Source-backed constraint

The current Worker performs its release callback synchronously on its single completion thread. The Adapter ACK-failure path retains the envelope and degrades the shared Worker before returning. The Worker exits at the next loop entry when it observes DEGRADED. Therefore the current synchronous callback topology cannot process a second completion on that Worker after the first ACK failure.

A timing barrier inside the first ACK callback is not a valid solution: it blocks the only Worker thread that must process the second completion. The existing mixed-stage test remains useful evidence for two Adapter-associated retained records, but its second operation is rejected before OS issue and does not satisfy this criterion.

## Initial design questions (disposition recorded in later review sections)

1. Can terminal completion notification/ACK delivery be separated from the Worker completion loop without transferring or duplicating ownership of the operation envelope?
2. If ACK delivery is dispatched asynchronously, which component owns the queued notification, its lifetime, its failure evidence, and its shutdown/join contract?
3. Can one Adapter/Submitter ACK failure quarantine only that Adapter-associated envelope and Submitter while allowing the shared Worker to finish already-issued independent operations, without permitting new unsafe work or releasing uncertain resources?
4. What exact state transition prevents a failed ACK from being retried, replayed, or silently dropped, while still allowing the second independent ACK failure to be observed?
5. How are callback ordering, concurrent Adapter shutdown, Worker shutdown, dispatcher failure, dispatcher wake failure, and partial initialization/teardown made fail-closed?
6. What externally observable snapshots prove two OS-issued operations, two terminal completions, two independent ACK failures, two retained records, and exact identity/cause correspondence?
7. Does the proposal preserve the current single-Worker topology and resource ownership, or does it introduce a new shared coordination object whose lifecycle must be specified?

## Initial candidate considered - dispatcher later deferred

A bounded terminal-notification dispatcher outside the Worker completion thread is one candidate for source-level feasibility review. The Worker would publish a terminal notification only after its own terminal/finalizer ownership transition is valid; a separately owned dispatcher would perform each Submitter ACK. This could allow the Worker to finish a second already-issued operation before the first ACK failure is delivered.

This candidate is **not yet shown safe**. In particular, the current `retain_envelope` path couples Adapter retention to shared Worker degradation/publication. The design must establish whether ACK-failure quarantine can be isolated per Adapter without losing Worker-level identity/accounting, and how the Worker/dispatcher stop or drain after failure. Do not implement this candidate until these ownership and lifecycle questions are answered.

## Explicit non-goals / prohibited shortcuts

- Do not suppress or defer the Worker DEGRADED check merely to keep dispatching.
- Do not continue processing new work after an uncertain ownership/ACK failure without a reviewed state machine.
- Do not add a sleep, timing race, scheduler stress, or callback barrier as acceptance evidence.
- Do not synthesize a second operation, mutate internal state/counters/lists, or use a second Worker to satisfy a same-Worker criterion.
- Do not retry ACK, release, close, callback, or I/O; do not replay uncertain writes.
- Do not alter Stage 5.16 or 5.19 acceptance wording to fit available evidence.

## Initial work plan (updated by the architecture review decision below)

1. Read-only source trace of Worker release callback, Adapter retained publication, Submitter ACK transition, and all relevant shutdown/destroy paths.
2. Produce at most two concrete architecture candidates with explicit ownership/state diagrams and failure matrices; reject candidates that require an unsafe post-DEGRADED Worker continuation.
3. Candidate A was selected only after tracing lock ordering, object lifetimes, callback cardinality, failure isolation, and teardown against the public lifecycle contracts.
4. Define a deterministic test fixture using existing supported seams where possible; any new test-only seam requires a separate narrow review and must expose only a real synchronization boundary.
5. Only after design review, implement in an isolated branch and prove the strict dual-ACK scenario with actual OS-issued operations.

## Current status

- Owner decision to preserve strict 5.16 criterion: RECORDED.
- Architecture candidate: Candidate A SELECTED; Candidate B deferred.
- Architecture implementation: AUTHORIZED in the broker-prototype branch only; Production wiring remains NOT AUTHORIZED.
- Stage 5.16: PARTIAL / NOT ACCEPTED.
- Stage 5.19: PARTIAL / DEPENDENCY BLOCKED.
- Stage 5.18: PARTIAL.
- Stage 5.20: INCOMPLETE; Gate D still NO ACTION APPROVED.
- Gate E: BLOCKED. Gate F: PENDING review/acceptance.
- Stage 5 / Phase 1: NOT ACCEPTED. Production: NO-GO.

No source, header, ABI, test, build, lifecycle, ownership, cleanup, recovery, retry, callback, or replay behavior is changed by this design-gate record. No tests/builds are claimed.
## Addendum - 2026-11-20 source-level candidate refinement

A focused read-only trace of `reaper-managed-worker-v2.c`, `integrated-submit-adapter-v2.c`, `integrated-submit-adapter-v2.h`, and `dedicated-io-submitter-v2.c` identified a narrower candidate than introducing a separate ACK-dispatch thread.

### Confirmed source facts

- `managed_worker_main` performs terminal hold, handle finalization, owner release, and Integrated release before invoking `op->release` synchronously (`reaper-managed-worker-v2.c:656-682`). The next loop entry exits only when the Worker core lifecycle is DEGRADED (`:499-503`).
- `adapter_worker_release` calls `raya_submitter_acknowledge` and, on failure, calls `retain_envelope` (`integrated-submit-adapter-v2.c:193-200`).
- `retain_envelope` publishes the Adapter envelope and Worker retained inventory record, then unconditionally calls `raya_reaper_mark_degraded_cause` on the shared Worker (`integrated-submit-adapter-v2.c:139-180`). This unconditional Worker degradation is the direct coupling that prevents the next completion.
- `raya_managed_worker_publish_retained_locked` only validates and links the retained record under the Worker lock; the publication helper itself does not mark the Worker DEGRADED (`reaper-managed-worker-v2.c:176-217`). Worker destruction separately requires an empty retained list (`:807-824`).
- `raya_submitter_acknowledge` can fail after the request has already transitioned to ACKED and `issuer_leases` has been decremented: failure to signal `changed_event` or `wake_event` degrades that Submitter and returns failure (`dedicated-io-submitter-v2.c:326-347`). The ACK is not safe to repeat.
- Adapter already has an interlocked admission gate (`lifecycle_state`, `adapter_enter`); the high bit closes admission (`integrated-submit-adapter-v2.c:32-45`, header `:76`). The only current public operation that sets that bit is `raya_integrated_adapter_destroy_ex`, which then checks entrants/active/retained counts and may return BUSY (`:580-631`). There is no close-admission-only API.

### Candidate A - Adapter-scoped post-terminal quarantine (selected by later architecture review)

For the narrow case where Worker terminal release has already succeeded and only Submitter ACK fails, investigate separating the failure domains:

1. Preserve the Adapter envelope and publish its Worker retained-inventory record exactly once, with immutable first-cause/stage evidence.
2. Preserve the exact Submitter failure state and do not retry ACK. Only the explicitly classified case where ACK committed to ACKED and its notification failed may use Adapter-scoped isolation; API-gate rejection, ACK state/lease invariant failure, or any unclassified false result keeps the existing Worker-degrading fail-closed behavior.
3. Rely on the affected Submitter's DEGRADED lifecycle to reject further admissions after the committed-notification failure. The Adapter API gate remains open so read-only retained-count diagnostics remain available; new Adapter submissions may enter setup but must be rejected by Submitter admission before enqueue/OS issue and follow existing cleanup.
4. Do not degrade the shared Worker solely for this classified ACK-committed/notification-failed case. The Worker may continue processing independent operations from other Adapters; the retained envelope remains owned/quarantined and Worker Destroy remains blocked by the retained list.
5. Preserve Worker degradation for API-gate rejection, ACK state/lease invariant failure, any unclassified false ACK result, and actual Worker/operation ownership, wait, finalizer, close, or accounting failures. The exception must not generalize to pre-terminal or uncertain I/O.

This candidate could let the same Worker terminally release a second independent operation after the first Adapter's Submitter has degraded, allowing the second independent Submitter to reach its own ACK failure. It does not release either retained envelope or make the failed ACK retryable.

### Candidate A implementation proof obligations

- Implement and test the defined lifecycle contract: Worker remains ACCEPTING after a successfully published ACK-notification-retained record, while Worker Destroy remains blocked by nonempty retained inventory.
- Prove that Submitter DEGRADED prevents every not-yet-issued request from reaching Issue, including requests already admitted or queued when the ACK notification fails. Queued work must remain quarantined; any pre-issue cleanup failure still degrades the Worker.

- Prove Worker inventory publication, Adapter inventory publication, and envelope lifetime remain consistent while the Worker continues processing other operations.
- Specify Worker shutdown/join behavior with retained post-terminal records. Worker Destroy must continue to reject nonempty retained inventory.
- Add the distinct retained-record ACK-notification cause bit proposed below; do not reuse `OWNERSHIP_INVARIANT` or silently relabel existing first-cause evidence.
- Prove that a failure to publish either retained record degrades the Worker and prevents continued processing; no partial publication may be treated as success.
- Test both ACK failure modes separately: ACK state committed but notification failed, and ACK transition/invariant failed before commit. No second ACK call is permitted.

### Candidate B - independent terminal ACK dispatcher (deferred)

A separate dispatcher could move ACK calls out of the Worker callback, but it adds another thread/queue/event lifecycle and still requires explicit rules for Adapter-scoped quarantine, Worker retained inventory, dispatcher failure, queue ownership, and shutdown. Do not implement this until Candidate A is rejected on a specific ownership/lifecycle proof, not merely because a dispatcher is familiar.

### Current disposition

At this point in the document Candidate A was under feasibility review; the later Architecture review decision below supersedes this interim disposition and selects Candidate A for prototype implementation. No code/tests/builds were changed or run by this source-level review. The strict dual-ACK criterion remains unchanged; Stage 5.16/5.19 remain PARTIAL and Production remains NO-GO.

### Addendum - same-Adapter in-flight admission boundary review

A further source trace checked whether Submitter DEGRADED prevents same-Adapter work from slipping past the ACK failure:

- `raya_integrated_adapter_submit` acquires an Adapter API entrant before entering `adapter_submit_impl`; the interlocked high bit prevents later entrants, while already-counted entrants may continue (`integrated-submit-adapter-v2.c:32-45, 551-560`).
- The submit path orders Worker admission, Submitter admission, Worker reservation, then Submitter enqueue (`integrated-submit-adapter-v2.c:437-499`).
- Submitter admission accepts only lifecycle RUNNING (`dedicated-io-submitter-v2.c:264-280`); enqueue accepts RUNNING or STOPPING but not DEGRADED (`:282-307`). Therefore an entrant that has not yet been admitted when ACK failure degrades the Submitter cannot gain a new Submitter admission. An entrant admitted earlier but not yet enqueued will have enqueue rejected; it then follows the existing abort/NOT_ISSUED cleanup path, whose failures must continue to quarantine/degrade the Worker.
- Work already enqueued/issued before the ACK failure is different: it is already committed and cannot be withdrawn by Submitter DEGRADED. It remains subject to its existing Worker completion and one ACK attempt; any failure must retain its own envelope. No cancellation, replay, or cleanup shortcut is implied.
- `adapter_destroy_ex` remains the existing caller-serialized way to close Adapter admission before post-join inventory capture; it is not called from the Worker callback. During live operation, the Adapter API gate stays open and Submitter DEGRADED is the barrier preventing new Issue.

This narrows the race but does not close the proof obligation. The test must hold an Adapter entrant at each meaningful boundary (before Submitter admission, after Submitter admission/before enqueue, and after enqueue/issue) and assert only the documented outcome. If an already-issued same-Adapter request remains, it must be allowed only through the existing terminal path and its exact ACK result; no queued/not-yet-issued request may reach Issue after Submitter DEGRADED. Any cleanup failure in a pre-issue entrant remains a Worker-level failure and must stop the Worker.

### Candidate A proposed state contract and strict acceptance test

The following is a review proposal only; it is not an implementation contract yet.

| Plane | Normal state | On a post-terminal ACK failure for Adapter A | Required invariant |
|---|---|---|---|
| Adapter A | Admission open; active envelopes tracked | Remains API-open; failed envelope moves to retained inventory | Submitter admission rejects new work before Issue; read-only retained-count remains observable; retained envelope is never freed |
| Submitter A | RUNNING | Preserve its exact failure state (DEGRADED for notification failure) | No second ACK; no issue/replay of queued uncertain work |
| Worker shared by A and B | ACCEPTING | Remains ACCEPTING only if terminal release and retained-record publication both succeeded | Worker continues only for independent operations; any Worker-level failure or failed retention publication still degrades it |
| Adapter A envelope | Active, Worker-terminal-released | Retained at SUBMITTER_ACK / POST_TERMINAL with immutable identity/cause | Callback suppressed; no release/free/retry/replay |
| Worker inventory | No record for this operation | One matching post-terminal record | Record pointer remains valid for Worker lifetime; nonempty inventory blocks Worker Destroy |

The same transition must occur independently for Adapter B. The exception is limited to an already-terminal operation whose only remaining failure is its Submitter ACK result. It must not cover uncertain OS issue, uncertain completion, finalizer/handle close, Worker accounting, or retention-publication failure.

#### Deterministic acceptance scenario

1. Start one shared Worker and two independent Adapter/Submitter pairs.
2. Submit two real named-pipe operations, one through each Adapter. Prove both have been accepted by the OS and handed to the same Worker before either ACK failure is induced.
3. Arm one exact ACK-notification failure for each Submitter using the existing per-Submitter event-handle dispatcher; do not use a timing race or a shared global selector.
4. Allow operation A to reach real terminal completion. Verify its ACK fails once, A's envelope is retained in Adapter A and Worker inventory, Submitter A is DEGRADED with request state ACKED and issuer lease released, and the shared Worker has not become DEGRADED. A subsequent A submit is rejected before Issue.
5. Allow operation B to reach real terminal completion on that same Worker. Verify its independent ACK fails once and B is retained with its own identity pair; Submitter B is DEGRADED with its own request state ACKED and issuer lease released.
6. Verify exactly two distinct OS-issued operations, two terminal completions, two distinct Adapter identity pairs, two POST_TERMINAL Worker records, one retained record per Adapter, no callback delivery, no ACK retry, no close retry, no free, no I/O replay, and no mutation of retained evidence.
7. Negative controls: a Worker finalizer/close failure, uncertain completion, failed Worker retained publication, or Worker accounting invariant must still degrade the Worker and stop further completion dispatch.

The test must not treat a request as OS-issued merely because it was Adapter-submitted or Worker-admitted. It must use observable evidence from the real Issue callback/OS result path and exact stable identities.

#### Test-fixture teardown gate

The expected successful acceptance result deliberately leaves two retained Adapter envelopes and two Worker retained records. Current public Adapter/Worker/Submitter Destroy contracts reject these states; they do not provide a safe cleanup route. Therefore the test cannot call Destroy, manually close handles, zero structures, free retained envelopes, or pretend the retained records were reconciled. Before implementing this test, the project must approve either (a) a narrow test-fixture lifecycle contract that preserves and safely disposes the process-isolated fixture, or (b) an explicitly process-isolated test executable whose process boundary is part of the test contract. This test teardown decision is independent of the Production quarantine policy.

### Addendum - Worker retained-inventory/lifecycle boundary confirmed

The final bounded source check confirmed the key precondition for Candidate A:

- `raya_managed_worker_admit` delegates to `raya_reaper_integrated_admit` under the Worker lock; `raya_managed_worker_reserve` delegates to Integrated reserve. Neither checks `retained_count` (`reaper-managed-worker-v2.c:403-434`).
- `raya_reaper_integrated_can_exit` checks only `reserved_slots`, Registry count, and the Integrated Core exit predicate; it does not check the Worker diagnostic retained list (`reaper-integrated-v2.c:224-227`).
- Worker `Destroy` does check `retained_count==0 && retained_head==NULL` (`reaper-managed-worker-v2.c:807-824`).

Therefore the current data model already separates the retained inventory from active admission/dispatch and from the Worker thread's drain predicate, while retaining a hard Destroy guard. This supports the feasibility of keeping the shared Worker operational after a *successfully published, already-terminal Adapter ACK-retained record*. It does not prove that changing the current unconditional degradation policy is acceptable or safe; that is the architecture decision still requiring review. The narrow exception must never apply to an operation whose terminal/finalizer/accounting release has not succeeded.

### Addendum - ACK failure typing boundary

The Adapter release callback currently receives only the Boolean result of `raya_submitter_acknowledge`. A false result can represent different Submitter-side outcomes: API-gate rejection before entering the Submitter, an ACK state/lease invariant failure before commit, or a committed ACK followed by changed/wake notification failure (which leaves the request ACKED and Submitter DEGRADED). These must not be collapsed into a claim that every failed call degraded the Submitter.

For Candidate A, the scope discriminator is the classified result from the ACK call after the Worker has completed terminal hold, finalizer close/commit, owner release, and Integrated release. Only ACK_COMMITTED_NOTIFICATION_FAILED is eligible for Adapter-scoped quarantine without Worker degradation. API-gate rejection, ACK state/lease invariant failure, and any unclassified false result retain the envelope and keep the existing Worker-degrading behavior. The retained cause should identify the ACK boundary without falsely asserting a Worker ownership failure; whether to add a distinct additive cause enum or rely on the existing `SUBMITTER_ACK` stage plus a documented cause remains an explicit design choice.

This boundary must be revalidated against all public Submitter API-gate and shutdown races before implementation. It does not authorize a second ACK attempt or a Submitter state mutation from the Adapter.

### Addendum - typed ACK result and retained-cause contract proposal

Candidate A must not infer the ACK result by reading Submitter internals from the Adapter or by treating every Boolean false as equivalent. The Submitter owns its request state and must report the outcome it committed.

Proposed additive contract:

- Add `raya_submitter_acknowledge_ex` with a typed result that distinguishes at least SUCCESS, COMMITTED_NOTIFICATION_FAILED (request is ACKED and issuer lease has been released, but changed/wake notification failed), NOT_COMMITTED_FAILURE (wait/state/lease/invariant failure), and API_GATE_CLOSED.
- Keep the existing Boolean `raya_submitter_acknowledge` as a compatibility wrapper returning true only for SUCCESS.
- The Adapter may use the no-Worker-degrade path only for COMMITTED_NOTIFICATION_FAILED. Every other non-success result follows the existing retain + Worker DEGRADED path.
- Do not let the Adapter mutate Submitter lifecycle or request state. Do not issue a second ACK.
- Add a distinct retained-record cause bit for post-terminal Submitter ACK notification failure (proposed `RAYA_RECOVERY_CAUSE_SUBMITTER_ACK_NOTIFICATION`, next unused bit). Store it only in the retained record; do not add it to Worker degradation causes. The recovery-policy evaluator's unknown-cause behavior is already fail-closed (`reaper-recovery-policy-v2.c` rejects any bit outside `ALL_CAUSES`), so accidental use as a Worker degradation cause remains blocked. Existing v1 inventory layouts need not change because the cause field is already a `uint32_t`; documentation must define the additive value.

The exact result must be captured under the Submitter lock at the point the transition/failure is known, including whether ACK committed and whether the issuer lease was released. This avoids unsynchronized cross-component reads and prevents the Adapter from misclassifying a duplicate ACK, API-gate rejection, or pre-commit wait failure as the narrow notification-only case.

## Architecture review decision - Candidate A selected for implementation

Based on the source trace and the Stage 5.20 single-owner policy, select Candidate A as the narrowest architecture change for Stage 5.16. Do not add a separate ACK dispatcher.

Rationale:

- At `adapter_worker_release`, Worker terminal hold, finalizer close/commit, owner release, and Integrated release have already succeeded. The Worker no longer owns an active I/O completion or open operation handle for this envelope; the remaining failure belongs to the Submitter ACK boundary.
- The Submitter owns ACK state and notification. A committed ACK whose notification fails is a Submitter-local degraded condition; it must not be reinterpreted as a Worker completion/finalizer failure.
- The existing Worker retained list is independent of Worker admission, reservation, and drain predicates, while Worker Destroy remains blocked by nonempty retained inventory. This supports continued service for other Adapters without releasing or forgetting the retained envelope.
- The exact typed ACK result prevents the exception from swallowing API-gate, pre-commit wait, state/lease invariant, or other unclassified failures. Those remain Worker-degrading as today.
- The change adds no thread, queue, event, or second Worker and preserves one explicit owner per lifecycle plane.

Implementation contract:

1. Add the typed Submitter ACK result API and preserve the existing Boolean API as a wrapper.
2. Add a distinct retained-record cause value for committed ACK notification failure; it is diagnostic only and is not a Worker degradation cause.
3. Add an Adapter-local post-terminal retention path that publishes both Adapter and Worker records exactly once. On success it suppresses callback/free, does not retry ACK, does not mark the shared Worker DEGRADED, and does not signal the Worker's wake event (the callback is executing on the Worker thread, which returns directly to its loop).
4. If Adapter or Worker retained publication fails, mark the Worker DEGRADED and exit through the existing fail-closed path. Every other non-success ACK result continues through the current Worker-degrading retention path.
5. Do not close the Adapter API gate. The Submitter's DEGRADED lifecycle rejects new Submitter admission; any already-admitted but not-enqueued request is rejected by Submitter enqueue and follows existing NOT_ISSUED cleanup. A cleanup failure remains Worker-degrading. The Adapter retained-count diagnostic remains observable while the Worker continues.
6. Keep Worker Destroy blocked by retained inventory. Shutdown/Join may complete after active Worker work drains; no retained record is removed or freed.
7. Add the strict two-Adapter/two-Submitter/one-Worker runtime test using two real OS-issued named-pipe operations and exact per-Submitter notification-failure injection. Use the existing API-test executable as the process boundary: retained fixtures are intentionally not destroyed/freed, and the test process reclaims them on exit. No manual handle close, structure zeroing, retained-list mutation, or fake cleanup is allowed.
8. Preserve negative behavior: pre-commit ACK failures, API-gate rejection, Worker close/finalizer failures, uncertain completion, ownership/accounting failures, and retained-publication failures still degrade the shared Worker.

This is an experimental broker-prototype architecture change only. It does not authorize Production integration, positive recovery, reconciliation, retry, replay, or Phase 1 acceptance. The strict 5.16 criterion and 5.19 dependency are unchanged.

### Design gate disposition

- Candidate A: SELECTED for implementation under the owner's strict-criterion decision.
- Candidate B dispatcher: NOT SELECTED.
- Production wiring: NOT AUTHORIZED.
- Strict 5.16 runtime proof: NOT YET RUN / NOT COMPLETE.

## Implementation and strict runtime acceptance result - 2026-11-20

<supersedes the earlier proposal-only, unimplemented, and teardown-blocked dispositions above>

**Stage 5.16 strict dual-post-terminal-ACK criterion: COMPLETE in the broker-prototype branch.** The owner selected preservation of the strict criterion; Candidate A is now implemented and exercised without relaxing the criterion or bypassing the Worker's DEGRADED exit for actual Worker failures.

### Implemented contract

- `dedicated-io-submitter-v2.h/.c`: added `raya_submitter_acknowledge_ex` and typed outcomes `SUCCESS`, `COMMITTED_NOTIFICATION_FAILED`, `NOT_COMMITTED_FAILURE`, `API_GATE_CLOSED`, and `INVALID_ARGUMENT`. The existing Boolean `raya_submitter_acknowledge` remains as a compatibility wrapper and returns true only for `SUCCESS`.
- `COMMITTED_NOTIFICATION_FAILED` is returned only after the request has transitioned to ACKED and its issuer lease has been released, but a changed/wake notification failed and the Submitter was degraded. Pre-commit wait/state/lease failures and API-gate rejection are distinct and do not qualify for the exception.
- `integrated-submit-adapter-v2.c`: only that exact typed result uses Adapter-local post-terminal retention. The path publishes the envelope in both Adapter and Worker retained inventories once, suppresses callback/free, does not retry ACK, and returns directly to the Worker dispatch loop without degrading or waking the shared Worker. It is guarded by the post-handoff/terminal-release state established by the existing lifecycle path.
- Failed Adapter/Worker retained publication, API-gate rejection, pre-commit ACK failure, and all other non-success results preserve the existing Worker-degrading fail-closed path. No Worker dispatch continues after a Worker-level failure.
- The Adapter API gate remains open. The affected Submitter's DEGRADED lifecycle rejects new admission and enqueue; a new Adapter submission is observed rejected before another OS Issue. Already queued work remains quarantined; no replay or cleanup shortcut is introduced.
- `RAYA_RECOVERY_CAUSE_SUBMITTER_ACK_NOTIFICATION` is an additive retained-record diagnostic (`1u<<6`) and is deliberately excluded from the recovery-policy evaluator's recognized Worker-degradation causes. The evaluator therefore remains fail-closed if this diagnostic is accidentally supplied as a Worker degradation cause.
- Worker shutdown/join can complete after active reservations and Registry work drain; Worker Destroy still rejects a nonempty retained inventory. No retained record is unlinked, freed, reconciled, or released.

### Strict acceptance evidence

The API test now has a dedicated `--strict-dual-ack` process mode. `build-integrated-submit-adapter-v2-api-test.cmd` launches that mode as a separate process before the normal suite on each of 10 iterations. This process boundary is intentional: the two retained Adapter envelopes and Worker records remain intact until that dedicated test process exits. The default suite no longer runs the retained fixture and then reuses its stack storage.

The strict scenario uses one real shared Managed Worker, two independent Adapter/Submitter pairs, and two separate real named-pipe READ operations. Before either client writes data, the test waits until both `ReadFile` callbacks have run and the same Worker reports `outstanding==2` and `registry.count==2`. It then completes both operations; each operation's finalizer-close boundary arms one independent changed-event ACK-notification failure for its own Submitter.

Observed and asserted:

- both operations were independently OS-issued and simultaneously outstanding/registered on the same Worker before completion;
- each operation reached its own terminal/finalizer/owner/Integrated release path and its own single ACK-notification failure;
- each Submitter is DEGRADED, each request is ACKED, and both issuer leases/queues/active issue counts are zero;
- each Adapter has exactly one retained envelope and the Worker has exactly two matching `POST_TERMINAL` records with distinct Adapter identity pairs;
- both owner and finalizer states are RELEASED; no user callback is delivered;
- the shared Worker remains ACCEPTING after both retained publications and shuts down/joins normally; its Destroy remains blocked by the retained list;
- a subsequent submission through an affected Adapter is rejected without another OS Issue;
- production Adapter v1 and Worker v1 inventories return the two matching identity pairs; the optional test-only validator also accepts the two-plane snapshot.

### Validation results

- Standard API regression: `/W4 /WX`; 10 isolated strict runs, 107 assertions each; 10 normal-suite runs, 863 assertions across 66 cases each. PASS.
- `RAYA_ADAPTER_TESTING` API regression: `/W4 /WX`; 10 isolated strict runs, 120 assertions each; 10 normal-suite runs, 1,048 assertions across 68 cases each. PASS.
- Recovery policy: 16 assertions, 0 failures. The new retained-only cause is explicitly verified BLOCKED by the Worker recovery-policy evaluator.
- Dedicated Submitter native core: 18 assertions per run, 25 consecutive runs PASS.
- Build scripts cleaned their declared EXE/OBJ outputs. `git diff --check` PASS.

### Updated Stage 5 disposition

- Stage 5.16: **COMPLETE** for the unchanged strict dual-post-terminal-ACK criterion in the broker-prototype branch.
- Stage 5.19: **COMPLETE** within the frozen v1 contract of independent per-plane copied inventories under each plane's quiescence preconditions. The strict runtime scenario now proves two Worker v1 records and one Adapter v1 record for each Adapter, with matching identity pairs. No atomic cross-plane snapshot is claimed or required.
- Stage 5.18: remains PARTIAL; its separate deterministic reserve-rejection and invariant-guard evidence gaps are unchanged.
- Stage 5.20: remains INCOMPLETE; no safe positive recovery/reconciliation action is approved.
- Gate D: COMPLETE / NO ACTION APPROVED. Gate E: BLOCKED. Gate F: pending complete Stage 5/Phase 1 owner acceptance and PR review. Stage 5/Phase 1 remain NOT ACCEPTED; Production remains NO-GO.

This is broker-prototype-only. It does not authorize Production wiring, reconciliation/release, retry, replay, callback redelivery, or changes to the Phase 1 recovery policy.
