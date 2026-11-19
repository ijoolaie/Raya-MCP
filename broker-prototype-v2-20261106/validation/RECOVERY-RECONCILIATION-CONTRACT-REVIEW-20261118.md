# Recovery / Reconciliation Contract Review — 2026-11-18

## Scope
Consolidated the current recovery contract from source-backed retained recovery, UNCERTAIN, TERMINAL_HELD, and retained ownership/I/O boundary reviews. Inspected existing production APIs and diagnostics. This is design/contract review only; no source, header, test, build script, configuration, or README changed. No build/test rerun, install, download, process termination, Commit, or Reset.

## Current contract disposition

The retained lists are quarantine ownership structures, not recoverable work queues. The production system currently has no safe general-purpose retained enumeration, reconciliation, release, or replay API. Therefore the present contract must remain fail-closed:

- Preserve retained envelope/operation storage and any still-owned handles.
- Keep Adapter Destroy, Worker Destroy, Submitter Destroy, and strict Context reclamation blocked when their ownership/in-flight gates report unresolved state.
- Do not unlink retained nodes, decrement counters, manually close handles, free buffers/envelopes, invoke callbacks, or reissue I/O based only on a retained count or coarse degradation bit.
- Treat unknown or conflicting evidence as UNKNOWN and retain it.

## Independent inventory domains

At least three namespaces exist and must not be collapsed:
1. Adapter envelope inventory.
2. Adapter-associated Worker managed-operation inventory.
3. Direct managed-pipe Worker retained-operation inventory.

Their cardinalities are not equivalent; a Worker-retained operation may have no Adapter envelope. Cross-links must be explicit and stable, and missing associations must remain absent.

## Evidence required before any recovery action

A future record needs:
- Stable nonzero operation identity plus a non-reused owner-instance epoch/generation.
- Immutable first-retain cause and stage.
- Monotonic record version/sequence.
- Explicit inventory kind and stable cross-component associations.
- Independently recorded Issue, handoff, completion, finalizer close substeps, finalizer commit, owner terminal/release, reservation/unreserve, Submitter ACK, and callback-disposition effects.
- Native error, responsible operation, provenance, and authority level for each fact.

The current pointer/address identity, global Worker degradation bits, mutable component fields, and test-only raw-address snapshot do not satisfy this contract. The test snapshot is quiescence-dependent, samples component locks separately, and is not an atomic production snapshot.

## Action boundary

- **First implementation slice:** stable identity, owner epoch, inventory kind, and immutable first-retain metadata only. No release, unlink, close, retry, callback, free, or cursor mutation.
- **Second slice:** copied read-only production enumeration with explicit snapshot lifetime, quiescence/lease, bounded pagination, truncation, corruption and list/count consistency semantics.
- **Later reconciliation:** owner-only per-record state transitions with unique reconciliation/action ID and compare-and-swap against record version. Every action must be typed to one failure class and prove its exact local preconditions.
- **Orchestrator integration:** only after component identity/snapshot/action contracts are implemented and tested. Reconciliation cursor steps must preserve partial effects and require fresh evidence before resuming.

A generic release-by-ID operation is prohibited. A failed CloseHandle or zero counter alone is not proof that a handle is safe to close or storage safe to free.

## Replay boundary

OS ReadFile/WriteFile acceptance (including ERROR_IO_PENDING) is not recipient/application commit. Any operation with Issue ACCEPTED or UNKNOWN must not be reissued by local recovery. Uncertain WRITE replay requires recipient-side stable idempotency identity and an authoritative status/receipt protocol; neither is established by the inspected live path. Cleanup-only recovery and I/O replay must remain separate contracts.

## Required reconciliation sequence

1. Close admission and quiesce all relevant component callers.
2. Enumerate Adapter, Worker-associated, and direct Worker inventories separately.
3. Classify each stable record from immutable per-entry evidence.
4. Apply only a narrowly specified, authorized local action with exact effect preconditions.
5. Revalidate owner, finalizer, reservation, registry, Submitter, and cross-inventory consistency.
6. Release a record only when all required local effects are COMMITTED and no UNKNOWN/conflicting effect remains.
7. Permit normal component teardown only after every relevant inventory is independently empty and verified.

Any UNKNOWN, conflict, stale identity/version, partial commit, or insufficient evidence blocks reclamation and remains quarantined.

## Verification / decision

- Re-read RETAINED-RECOVERY-API-CONTRACT-GAP-ANALYSIS-20261118.md.
- Re-read UNCERTAIN-PATH-CLASSIFICATION-20261118.md and TERMINAL-HELD-PATH-REVIEW-20261118.md.
- Re-read RETAINED-CAUSE-OWNERSHIP-IO-BOUNDARY-MATRIX-20261117.md.
- Cross-checked current Adapter, Worker, integrated Reaper, and Submitter APIs: production retained count exists; production per-entry enumerate/reconcile/release/replay does not.
- Existing test-only snapshot remains diagnostic and quiescence-dependent; it is not promoted to a production recovery contract.
- No code change or test rerun was warranted for this consolidation.

## Conclusion
The recovery contract is now explicit: preserve quarantine; no generic release; no uncertain I/O replay; stable identity and immutable evidence must precede read-only production enumeration; evidence-specific reconciliation and Orchestrator cursor integration come later. This review does not authorize implementing those APIs.

No source changes, no build artifacts created, no cleanup performed, no Commit/Reset. Stage 5.11 review complete (contract consolidation only).


## Supersession note — Gate C/Gate D evidence (2026-11-18)

The historical statement in this review that production read-only enumeration is absent is superseded by PRODUCTION-RETAINED-INVENTORY-IMPLEMENTATION-20261118.md. The two v1 copied read-only inventory APIs now exist and passed focused verification. This does not change the reconciliation decision: the copied v1 records do not include a complete immutable effect ledger, per-handle finalizer substeps, callback/ACK disposition, recipient receipt, or durable identity. Gate D therefore approved no mutation; retained resources remain quarantined for the current process lifetime. Gate E positive recovery tests and Gate F Stage 5 acceptance remain blocked.
