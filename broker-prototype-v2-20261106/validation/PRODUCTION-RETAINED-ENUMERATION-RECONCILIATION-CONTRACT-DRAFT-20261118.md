# Production Retained Enumeration and Reconciliation Contract — Evidence-Based Draft — 2026-11-18

## Status

**DESIGN DRAFT ONLY — NOT APPROVED, NOT IMPLEMENTED, NOT AN AUTHORIZATION TO RELEASE QUARANTINED RESOURCES.**

This document records the verified implementation boundary and the contract questions that must be resolved before production recovery APIs can be designed. It deliberately does not invent a public function signature or a release algorithm.

## 1. Verified current implementation boundary

Source inspection of `integrated-submit-adapter-v2.h/.c` and `reaper-managed-worker-v2.h/.c` confirms:

- Production Adapter API exposes `raya_integrated_adapter_retained_count`, a count only.
- Adapter retained envelopes are stored in an Adapter-owned linked list.
- Worker retained operations are stored in a separate Worker-owned linked list.
- The Worker structure exposes internal `retained_head` and `retained_count` fields in its C structure, but these are not a supported safe enumeration API.
- The by-value Adapter/Worker retained snapshot is compiled only under `RAYA_ADAPTER_TESTING`.
- The test snapshot requires closed Adapter admission, joined Worker, Submitter with no queued/active issue/producers, and caller quiescence for direct Submitter APIs. It takes Adapter and Worker snapshots under separate locks and therefore is diagnostic, not an atomic cross-plane transaction.
- Adapter and Worker retained membership are not interchangeable. An Adapter envelope may be retained without corresponding Worker retained-list membership; Worker may also contain direct operations unrelated to an Adapter.
- Stable process-local identities now exist for initialized Worker operations and Adapter envelopes. Worker identity is `(worker_epoch, worker_operation_id)`; Adapter identity is `(adapter_instance_id, adapter_operation_id)`. Adapter association on Worker entries is correlation metadata, not ownership transfer.
- Worker IDs are process-local and not durable across process restart.
- Existing destroy paths reject unsafe teardown when retained resources remain. They do not imply a generic retained-resource release path.

## 2. Inventory domains that must remain separate

Any future production inventory interface must represent at least these distinct domains without merging their counts or membership:

1. **Adapter-envelope inventory** — Adapter-owned retained envelopes, identified by Adapter instance and operation ID.
2. **Worker-operation inventory** — Worker retained operations, identified by Worker epoch and Worker operation ID where identity is valid.
3. **Identity-less partial Worker records** — retained-list entries created before successful operation initialization; these must be explicitly marked identity-less and must not be assigned guessed IDs.
4. **Adapter-associated Worker records** — Worker operations with an explicit Adapter identity pair.
5. **Direct Worker records** — Worker operations with no Adapter association.

A cross-plane relationship may be reported only when the explicit Adapter association pair matches. Pointer values, list order, raw addresses, and sequence-number coincidence are not identity.

## 3. Read-only enumeration requirements (proposed)

Before any reconciliation operation is considered, a production read-only inventory API would need a separately reviewed contract covering:

- Caller authorization and object lifetime: how callers prove Adapter/Worker objects remain alive for the entire call.
- Concurrency: whether enumeration requires externally quiesced admission, an internal lock protocol, or a generation/version retry scheme.
- Consistency level: explicitly state whether Adapter and Worker inventories are independently consistent snapshots or one atomic cross-plane snapshot. Current test implementation only supports the former.
- Bounded output: caller-provided capacity or another explicit allocation/ownership policy; report required counts without exposing internal pointers.
- Stable copied fields: identity validity, identity namespace, inventory kind, optional Adapter association, first-retain cause/stage, and only state fields whose reads are synchronized.
- Partial records: explicit identity-less representation; never synthesize an identity.
- Error semantics: distinguish invalid object, not initialized, not quiescent/retryable, insufficient capacity, inconsistent internal list, and internal failure.
- No side effects: enumeration must not change ownership, release handles, unlink records, invoke callbacks, acknowledge requests, retry I/O, or alter lifecycle state.
- Versioning: structure size/version rules and forward-compatible field handling.
- Privacy/security: do not expose kernel handles, raw pointers, caller buffers, or other sensitive addresses as stable identity.

The current test snapshot's quiescence preconditions are a proven safe diagnostic baseline, not automatically a production API decision.

## 4. Reconciliation requirements (proposed, not yet specified)

A reconciliation API must not be designed as a generic `release(id)` operation. Before any mutating action is specified, the project must define:

- Which exact state transitions are eligible and which are permanently quarantined.
- Evidence required to establish terminal completion and ownership transfer.
- Which component is the sole authority for each resource and state transition.
- How stale observations are rejected (identity plus generation/version or equivalent).
- How concurrent reconciliation attempts are serialized and made idempotent.
- Whether a reconciliation action is observation-only, metadata repair, or resource release; these must be distinct actions.
- What happens when one sub-step succeeds and a later step fails.
- How a partially completed action is recorded and retried without repeating irreversible effects.
- How callers distinguish "not attempted", "attempted but unknown", "committed", and "failed".
- Which operations are forbidden when the OS accepted I/O but recipient completion/commit is unknown.

Until those rules are specified and tested, quarantined resources remain retained. No generic release, replay, resubmission, or callback retry is permitted by this draft.

## 5. Explicit no-replay rule

A successful OS `WriteFile` acceptance is not proof of recipient commit. A missing or failed Submitter ACK is not evidence that the recipient did not process the write. Consequently:

- Never replay an accepted or outcome-unknown WRITE based only on retained inventory.
- Never resubmit an operation merely because its completion/ACK observation is missing.
- Retrying an observation of an incomplete operation is distinct from issuing the I/O again.
- Any future exception requires a separately defined recipient-side idempotency/receipt protocol and evidence; none is established by the current retained identity fields.

## 6. Ownership and lock-order constraints verified in current code

- Adapter retains its envelope in its own inventory; Worker owns the Worker retained-operation list.
- The documented nested lock direction in the test-only degraded handoff path is Worker then Adapter.
- Normal Adapter retention releases the Adapter lock before taking the Worker lock.
- A new API must not introduce an Adapter-to-Worker nested lock path that reverses the existing Worker-to-Adapter order.
- Adapter Destroy does not stop/join/destroy Submitter or Worker. Worker is borrowed by Adapter.
- Any future reconciliation operation must preserve OWNED/BORROWED ownership contracts and must not tear down externally owned components.

## 7. Unresolved decisions — blockers, not assumptions

The following are not decided by current source or tests and require explicit design review:

1. Whether production enumeration is per-object or a coordinated cross-plane diagnostic.
2. Whether callers must quiesce admission externally or the API internally establishes a snapshot barrier.
3. Whether production enumeration may allocate, and who owns returned storage.
4. Whether identity-less partial records are enumerable in production or only represented through aggregate counts.
5. Whether any retained state can ever become releasable without process restart.
6. Which actor can provide authoritative evidence of recipient-side WRITE completion.
7. Whether a future durable identity is required across process restart.
8. Whether reconciliation is a separate privileged subsystem or part of Adapter/Worker APIs.
9. Required ABI/version compatibility policy for public structures.
10. Security/authorization model for mutating reconciliation actions.

No answer to these questions is inferred here.

## 8. Required review and implementation sequence

1. Review and approve the inventory-domain separation and no-side-effect requirements.
2. Decide the unresolved enumeration questions above.
3. Freeze a versioned read-only enumeration contract.
4. Implement enumeration only after lock/lifetime/concurrency review.
5. Add tests for concurrent admission, quiescence, insufficient capacity, partial records, identity collisions, and cross-plane non-atomicity.
6. Separately decide whether any reconciliation mutation is safe and define its evidence/authorization/state machine.
7. Only then implement a narrowly typed reconciliation API and positive tests for each explicitly permitted transition.
8. Keep unknown I/O outcomes quarantined; do not add generic release or replay.

## Acceptance

- Existing identity contract baseline: PRESENT.
- Production enumeration API: ABSENT.
- Production reconciliation API: ABSENT.
- Production release/replay API: ABSENT / NOT AUTHORIZED.
- This draft: WRITTEN FOR REVIEW; NOT APPROVED.
- Stage 5.20: PARTIAL / NOT COMPLETE.
- Stage 5 implementation/recovery acceptance: BLOCKED / NOT ACCEPTED.
- Phase 1: NOT ACCEPTED.
- Production: NO-GO.
