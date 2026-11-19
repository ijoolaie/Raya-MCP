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
