# STAGE 5.26 — Dual-Adapter Snapshot Evidence Readiness — 2026-11-18

## Scope
Read-only audit of the existing test-only retained snapshot contract and its ability to classify the failed dual-Adapter/one-Worker experiment. No source or test code changed.

## Confirmed snapshot capabilities

The existing `raya_adapter_test_retained_snapshot` already captures, per Adapter-retained envelope:

- Stable Adapter instance and operation IDs.
- First retain cause and stage.
- Worker admission / ownership / terminal-release / finished flags.
- Owner, finalizer, integrated operation and reservation states.
- Submitter request issue and publish effect states, request state, lifecycle, issuer leases, active issue/producers and queue count.
- Shared Worker lifecycle, reserved/outstanding/queued counts, caller-active count, registry count and degradation causes.

It separately captures the Worker retained list with Worker epoch/operation ID, inventory kind, optional Adapter identity pair and Worker first-retain cause/stage. Thus the snapshot is sufficient to distinguish an Adapter envelope retained before OS issue from a terminal-released operation retained after Submitter ACK failure, provided the correct post-quiescence snapshot is taken.

## Critical preconditions

The snapshot returns a complete result only after:
- Adapter admission is closed and no Adapter entry is active.
- Shared Worker is joined and its thread pointer is null.
- That Adapter's Submitter queue, active issue and active producers are all zero.
- Caller has quiesced direct Submitter APIs.
- Caller supplies enough output capacity.

The snapshot is diagnostic and test-only. It locks Adapter, Worker and Submitter planes separately; it is not an atomic simultaneous cross-plane snapshot. In a two-Adapter/one-Worker experiment, take one snapshot for each Adapter after both Submitters are quiescent and the shared Worker is joined, then compare the duplicated Worker inventory by stable Worker and Adapter identity. Do not take the snapshots while the Worker is live or treat sequential per-Adapter snapshots as atomic.

## Classification rules for the next experiment

A terminal ACK-retained operation should show:
- Adapter flag bits: retained, Worker-owned, terminal-released, Worker-admitted and Worker-finished set.
- Submitter request issue and publish effects COMMITTED.
- Submitter request state and lifecycle reflecting the ACK-failure path.
- Adapter first-retain stage SUBMITTER_ACK.
- Matching Worker retained record with ADAPTER_ASSOCIATED identity and POST_TERMINAL stage.

A pre-issue / admission-rejected retained envelope should not be described as terminal completion. It is expected to lack terminal-release evidence and may show Worker admission false and issue/publish effects NOT_ATTEMPTED. Its Worker retained-list membership, if present, is only a quarantine inventory record and does not prove OS I/O acceptance. Use the captured fields rather than inferring this category from retained counts alone.

## Feasibility decision

The previous Stage 5.24 experiment's abbreviated diagnostic summary did not preserve the full per-envelope snapshot values needed to classify Adapter B precisely. Therefore its exact retain stage and issue/publish effect remain unproven. The source trace establishes a plausible ordering, not the missing runtime facts.

A new controlled experiment with Adapter A completing normally and Adapter B receiving the injected ACK failure is technically distinct and may be useful. It can validate two Adapter identities and two real Worker terminal paths, but only B is retained due to ACK failure. It cannot satisfy the original requirement of two independent Adapter-associated retained submissions. Do not claim 5.16 complete from that experiment.

## Decision and status

- Snapshot instrumentation is already sufficient; no production or test API extension is justified at this stage.
- No new test/build was run.
- No quarantined resource was released, replayed or deleted.
- No source/header/build script/README/TLA+ model/config changed.
- Stage 5.16 remains BLOCKED / NOT COMPLETE.
- Stage 5 implementation/recovery acceptance remains BLOCKED / NOT ACCEPTED.
- Phase 1 remains NOT ACCEPTED; Production remains NO-GO.

## Next safe step
Run the separately scoped success-A / ACK-failure-B shared-Worker experiment only if its setup can guarantee that B reaches actual Worker terminal release before the sole ACK failure is injected. Capture both complete snapshots after the shared Worker and both Submitters are quiescent. If that precondition cannot be established without changing production semantics, stop and document the blocker rather than adding a synchronization hook inside the Worker release callback.

## Addendum - 2026-11-19 success-A / ACK-quarantine-B snapshot boundary

Reviewed the existing `test_shared_worker_two_adapter_success_then_ack_quarantine` against the Stage 5.26 next-safe-step criteria and the production Adapter inventory preconditions. The test already executes the distinct, safe ordering: Adapter A completes its real READ and callback successfully before Adapter B is submitted; Adapter B then reaches real Worker terminal release and its injected Submitter ACK failure retains B. It verifies A has zero retained/active envelopes, B has one retained envelope, B's Worker operation is ADAPTER_ASSOCIATED and POST_TERMINAL, owner/finalizer are RELEASED, and the Worker reservation/outstanding counts are zero. Both Submitters and the shared Worker are subsequently joined/quiescent. B is captured by both the production Worker v1 inventory and the test-only Adapter/Worker snapshot. This is one successful Adapter path plus one retained Adapter path; it is not two retained submissions and does not satisfy 5.16.

A complete post-quiescence Adapter snapshot for A cannot be added to this existing sequence using the current production lifecycle API without changing the contract. `raya_integrated_adapter_copy_retained_inventory_v1` requires Adapter admission closed and `active_count == retained_count`. For A, both counts are zero after successful completion. The only existing production operation that closes Adapter admission is `raya_integrated_adapter_destroy_ex`; when A has no active or retained records, that function proceeds to `DeleteCriticalSection(&a->lock)` and `memset(a,0,sizeof(*a))`, destroying the object rather than leaving an initialized, closed, quiescent Adapter available for the snapshot. The test currently destroys A before Worker join. Calling the snapshot after that would violate object initialization/lifetime and is not valid. Calling destroy before the Worker join does not help because the production inventory API separately requires the Worker joined. The test-only snapshot has the same post-join Worker requirement and does not provide an approved close-only Adapter lifecycle operation.

Therefore the existing B snapshot is valid, and A's successful outcome is independently established by its completion callback and zero retained/active counts, but a paired post-quiescence A+B Adapter snapshot is not currently expressible with the supported lifecycle API. Do not fabricate a retained envelope for A, keep a destroyed Adapter alive, mutate counters, or add an unreviewed close-only API/test hook. Resolving this narrower diagnostic gap would require a separately reviewed Adapter admission-close-without-destroy lifecycle contract; it is not justified by the frozen v1 inventory ABI and is not implemented here.

Disposition: Stage 5.26's success-A / ACK-failure-B runtime ordering is already demonstrated; its stronger request for both complete post-quiescence Adapter snapshots is BLOCKED by the absence of a supported close-only Adapter state. This does not change 5.16 (two Adapter-associated retained submissions: BLOCKED), 5.18 (PARTIAL), 5.19 (real Adapter-associated and real DIRECT production Worker snapshots now covered, combined cross-plane atomic snapshot explicitly out of scope), or 5.20 (no safe reconciliation action/API is approved). No source/header/test/build script or lifecycle behavior changed. No test was rerun because this is a source/API lifecycle review. No retained resource was released, retried, replayed, or deleted. Phase 1 remains NOT ACCEPTED; Production remains NO-GO.

## Correction / superseding addendum - 2026-11-19 supported empty-Adapter quiescence

The preceding addendum's conclusion that a paired Adapter snapshot was not expressible was too strong. Source review identified a supported way to leave an empty Adapter initialized with admission closed: hold a real Adapter API entrant in the existing injected `create_event` callback, call `raya_integrated_adapter_destroy_ex` while that entrant is active (it returns BUSY at the entrant gate and leaves the object/lock initialized), then release the callback so the real submission exits and the entrant count drains. The Adapter remains initialized, admission-closed, and has `active_count == retained_count == 0`. After the shared Worker is joined and the Submitter is shut down/joined with its API admission explicitly closed, the production Adapter inventory API accepts the zero-record snapshot. This uses existing test injection and lifecycle APIs; it adds no production API or behavior.

The test now retains Adapter A's initialized object through the post-join snapshot and also snapshots Adapter B's actual ACK-retained envelope. The earlier paragraph claiming this cannot be done is superseded by this correction. The paired snapshots remain sequential, not atomic, exactly as the frozen v1 contract requires.
