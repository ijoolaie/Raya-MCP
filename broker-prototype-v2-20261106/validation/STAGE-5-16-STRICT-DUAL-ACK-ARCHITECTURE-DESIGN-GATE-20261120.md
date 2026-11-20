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

It authorizes a separately reviewed architecture-design work item to find a way to satisfy the unchanged criterion while preserving fail-closed isolation. It does not select an architecture, authorize Production changes, authorize a test hook, or permit weakening any lifecycle/recovery contract. Implementation remains gated on a concrete design review and bounded test strategy.

## Source-backed constraint

The current Worker performs its release callback synchronously on its single completion thread. The Adapter ACK-failure path retains the envelope and degrades the shared Worker before returning. The Worker exits at the next loop entry when it observes DEGRADED. Therefore the current synchronous callback topology cannot process a second completion on that Worker after the first ACK failure.

A timing barrier inside the first ACK callback is not a valid solution: it blocks the only Worker thread that must process the second completion. The existing mixed-stage test remains useful evidence for two Adapter-associated retained records, but its second operation is rejected before OS issue and does not satisfy this criterion.

## Design questions that must be answered before implementation

1. Can terminal completion notification/ACK delivery be separated from the Worker completion loop without transferring or duplicating ownership of the operation envelope?
2. If ACK delivery is dispatched asynchronously, which component owns the queued notification, its lifetime, its failure evidence, and its shutdown/join contract?
3. Can one Adapter/Submitter ACK failure quarantine only that Adapter-associated envelope and Submitter while allowing the shared Worker to finish already-issued independent operations, without permitting new unsafe work or releasing uncertain resources?
4. What exact state transition prevents a failed ACK from being retried, replayed, or silently dropped, while still allowing the second independent ACK failure to be observed?
5. How are callback ordering, concurrent Adapter shutdown, Worker shutdown, dispatcher failure, dispatcher wake failure, and partial initialization/teardown made fail-closed?
6. What externally observable snapshots prove two OS-issued operations, two terminal completions, two independent ACK failures, two retained records, and exact identity/cause correspondence?
7. Does the proposal preserve the current single-Worker topology and resource ownership, or does it introduce a new shared coordination object whose lifecycle must be specified?

## Candidate to investigate - not selected

A bounded terminal-notification dispatcher outside the Worker completion thread is one candidate for source-level feasibility review. The Worker would publish a terminal notification only after its own terminal/finalizer ownership transition is valid; a separately owned dispatcher would perform each Submitter ACK. This could allow the Worker to finish a second already-issued operation before the first ACK failure is delivered.

This candidate is **not yet shown safe**. In particular, the current `retain_envelope` path couples Adapter retention to shared Worker degradation/publication. The design must establish whether ACK-failure quarantine can be isolated per Adapter without losing Worker-level identity/accounting, and how the Worker/dispatcher stop or drain after failure. Do not implement this candidate until these ownership and lifecycle questions are answered.

## Explicit non-goals / prohibited shortcuts

- Do not suppress or defer the Worker DEGRADED check merely to keep dispatching.
- Do not continue processing new work after an uncertain ownership/ACK failure without a reviewed state machine.
- Do not add a sleep, timing race, scheduler stress, or callback barrier as acceptance evidence.
- Do not synthesize a second operation, mutate internal state/counters/lists, or use a second Worker to satisfy a same-Worker criterion.
- Do not retry ACK, release, close, callback, or I/O; do not replay uncertain writes.
- Do not alter Stage 5.16 or 5.19 acceptance wording to fit available evidence.

## Required next work

1. Read-only source trace of Worker release callback, Adapter retained publication, Submitter ACK transition, and all relevant shutdown/destroy paths.
2. Produce at most two concrete architecture candidates with explicit ownership/state diagrams and failure matrices; reject candidates that require an unsafe post-DEGRADED Worker continuation.
3. Select no candidate until its lock ordering, object lifetimes, callback cardinality, failure isolation, and teardown are proven against the public lifecycle contracts.
4. Define a deterministic test fixture using existing supported seams where possible; any new test-only seam requires a separate narrow review and must expose only a real synchronization boundary.
5. Only after design review, implement in an isolated branch and prove the strict dual-ACK scenario with actual OS-issued operations.

## Current status

- Owner decision to preserve strict 5.16 criterion: RECORDED.
- Architecture candidate: NOT SELECTED.
- Architecture implementation: NOT AUTHORIZED by this document.
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

### Candidate A - Adapter-scoped post-terminal quarantine (preferred for feasibility review; not selected)

For the narrow case where Worker terminal release has already succeeded and only Submitter ACK fails, investigate separating the failure domains:

1. Preserve the Adapter envelope and publish its Worker retained-inventory record exactly once, with immutable first-cause/stage evidence.
2. Preserve the exact Submitter failure state and do not retry ACK. When ACK commits to ACKED but its notification fails, the Submitter is DEGRADED; other false-return causes (for example, a closed API gate or ACK invariant rejection) must be classified separately rather than assumed to have the same lifecycle result.
3. Close admission for the affected Adapter through a separately specified close-only lifecycle operation; do not call Destroy from the Worker callback as a shortcut.
4. Do not degrade the shared Worker solely because this already-terminal operation's Submitter ACK notification failed. The Worker may continue processing independent operations from other Adapters; the retained envelope remains owned/quarantined and Worker Destroy remains blocked by the retained list.
5. Preserve Worker degradation for actual Worker/operation ownership, wait, finalizer, close, or accounting failures. The exception must be narrowly typed to the post-terminal Adapter ACK failure and must not generalize to pre-terminal or uncertain I/O.

This candidate could let the same Worker terminally release a second independent operation after the first Adapter's Submitter has degraded, allowing the second independent Submitter to reach its own ACK failure. It does not release either retained envelope or make the failed ACK retryable.

### Candidate A unresolved proof obligations

- Define whether a Worker with one or more post-terminal Adapter ACK-retained records may continue serving other Adapters, and specify the exact lifecycle state/acceptance contract.
- Define a public close-admission-only API and its concurrent-entrant semantics. Existing Destroy cannot be invoked as a proxy because it combines admission closure with destruction preconditions and is caller-serialized.
- Prove that a Submitter ACK failure cannot leave an already-admitted same-Adapter producer able to issue new work after the Adapter gate closes; queued/active work must remain quarantined under the Submitter's existing DEGRADED behavior.
- Prove Worker inventory publication, Adapter inventory publication, and envelope lifetime remain consistent while the Worker continues processing other operations.
- Specify Worker shutdown/join behavior with retained post-terminal records. Worker Destroy must continue to reject nonempty retained inventory.
- Add distinct cause/stage semantics if `OWNERSHIP_INVARIANT` is too broad to distinguish an ACK notification failure from a Worker ownership invariant; do not silently relabel the existing first-cause evidence.
- Prove that a failure to publish either retained record degrades the Worker and prevents continued processing; no partial publication may be treated as success.
- Test both ACK failure modes separately: ACK state committed but notification failed, and ACK transition/invariant failed before commit. No second ACK call is permitted.

### Candidate B - independent terminal ACK dispatcher (deferred)

A separate dispatcher could move ACK calls out of the Worker callback, but it adds another thread/queue/event lifecycle and still requires explicit rules for Adapter-scoped quarantine, Worker retained inventory, dispatcher failure, queue ownership, and shutdown. Do not implement this until Candidate A is rejected on a specific ownership/lifecycle proof, not merely because a dispatcher is familiar.

### Current disposition

Candidate A is the next source/design feasibility target, not an approved architecture. No code, header, ABI, test hook, lifecycle, ownership, recovery, or Production behavior changed in this review. No tests/builds were run. The strict dual-ACK criterion remains unchanged; Stage 5.16/5.19 remain PARTIAL and Production remains NO-GO.
