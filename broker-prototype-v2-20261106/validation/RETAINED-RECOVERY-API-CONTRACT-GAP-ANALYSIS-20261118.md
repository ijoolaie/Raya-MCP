# Retained Recovery API Contract Gap Analysis — 2026-11-18

## Scope and disposition

Working Copy only: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`.

This is a source-backed, read-only design analysis. No C/C++ source, header, test, build script, configuration, or README was changed. No build/test, install/download, process termination, commit, or reset was performed.

Production remains **NO-GO**. Phase 1 remains **NOT ACCEPTED**. This report does not authorize a recovery/release API implementation.

## Executive conclusion

The existing retained lists are quarantine ownership structures, not recoverable queues. A safe production interface cannot be implemented as a generic `enumerate → retry → unlink/free` sequence.

The source confirms five independent blockers:

1. **No stable identity:** Adapter envelope and request identity are currently process addresses/pointers. There is no stable non-pointer operation ID or instance epoch.
2. **No per-entry retain cause:** `retain_envelope(e, cause)` records the cause in Worker-wide degradation bits, but the envelope itself does not store its first-retain cause. A later retain call may contribute a different global cause; the entry cannot be independently classified from production state.
3. **No immutable per-entry effect ledger:** Issue/handoff/completion/finalizer/owner/unreserve/ACK progress is distributed among mutable fields and component structures. There is no unified immutable record proving which effects occurred.
4. **Three distinct inventory domains:** Adapter envelopes, Adapter-associated Worker retained operations, and direct managed-pipe Worker retained operations are not a single one-to-one inventory. A Worker list entry may have no Adapter envelope.
5. **No recipient outcome evidence:** `ReadFile`/`WriteFile` acceptance is not remote application commit. There is no recipient receipt/status query or stable idempotency key propagated to the recipient. Uncertain WRITE must never be replayed by a local cleanup API.

Consequently, production must continue to fail closed: retained entries stay quarantined, Adapter Destroy remains blocked, and no generic unlink, counter decrement, manual close, free, or I/O retry is permitted.

## Source-backed observations

### Adapter envelope

`integrated-submit-adapter-v2.c` defines an envelope containing the Submitter request, managed operation, Adapter pointer, callback/context, immediate error, READ/WRITE discriminator, and lifecycle flags (`retained`, `worker_admitted`, `worker_finished`, `worker_owns_operation`, `worker_terminal_released`, plus test-only quarantine state). It has no production stable ID, first-retain cause, or typed per-effect record.

`retain_envelope`:
- links a newly retained envelope into `adapter->retained_head` and increments `retained_count`;
- conditionally links its managed operation into Worker retained inventory when Worker does not own it or terminal release already occurred;
- marks Worker degradation with the supplied cause;
- calls `worker_finish_once` and signals the Worker wake event.

The cause is therefore applied to a Worker-wide degradation field; it is not stored on the retained envelope. Repeated calls do not relink the envelope, but still apply the supplied Worker degradation cause.

### I/O and ownership boundary

`adapter_issue` invokes the injected `ReadFile` or `WriteFile`. A successful return or `ERROR_IO_PENDING` is treated as Issue accepted. That is OS submission acceptance, not recipient/application commit.

`adapter_publish` transfers the operation to Worker. If handoff reports NOT_TRANSFERRED after Issue accepted, the code explicitly retains the envelope and comments that it must not be routed through NOT_ISSUED cleanup. This confirms that Issue and Worker ownership transfer are separate effect planes.

The Worker release callback marks terminal release, then attempts Submitter ACK. If ACK fails, it retains the envelope. This is a post-terminal local lifecycle failure, not permission to repeat I/O.

### Inventory domains

- Adapter envelope list: `raya_adapter_envelope *retained_head`.
- Worker retained list: `raya_managed_operation *retained_head`.
- Direct managed-pipe paths in `managed-pipe-io-v2.c` independently insert operations into Worker retained inventory.

The Adapter helper links to the Worker list conditionally. The test-only transferred-degraded path adds the Adapter envelope while the Worker owns the operation and deliberately does not duplicate the operation into Worker retained list. Direct managed-pipe retention has no corresponding Adapter envelope by necessity of that API profile.

Thus the inventories require separate namespaces and explicit cross-links. Counts must never be compared as if they were equivalent.

### Current diagnostics are not a production contract

The production API exposes Adapter retained count and a Destroy refusal path. The richer `raya_adapter_test_retained_snapshot` is compiled only under `RAYA_ADAPTER_TESTING`.

That snapshot:
- requires Adapter admission closed and no Adapter entrants;
- requires Worker joined and its thread pointer NULL;
- requires Submitter queue/producers/active issue to be zero, with direct Submitter callers quiesced by the caller;
- checks list cycles and count consistency;
- returns raw addresses for envelope, operation and request identity;
- reads Adapter, Worker and Submitter structures under separate locks;
- does not reconcile, retry, release, unlink or free anything.

It is a useful test diagnostic, but not a stable identity scheme or a production recovery interface. The raw-address fields are explicitly unsuitable as durable IDs. Its multi-component view is also not an atomic transaction across all three component locks.

## Required contract before implementation

### 1. Identity and inventory model

Assign a nonzero, monotonically increasing operation ID before any admission/Issue boundary. Scope it by a non-reused owner-instance epoch (or equivalent generation token). Exhaustion must fail closed; IDs must not wrap or be reused while any record or external evidence can refer to them.

Use distinct record kinds:
- Adapter envelope;
- Worker-owned managed operation;
- direct managed-pipe retained operation.

Represent cross-component association with stable IDs, not pointers. An association may be absent; do not manufacture an Adapter envelope for direct Worker retention.

### 2. First-failure and effect evidence

Each retained record must preserve:
- immutable first-retain cause and stage;
- current owner/topology and owner generation;
- monotonic lifecycle sequence/version;
- typed effect states, independently recorded at minimum for OS Issue, Worker handoff, Worker terminal completion, finalizer handle-close substeps, finalizer release commit, Pipe Owner terminal/release, Worker reservation/unreservation, Submitter ACK, and callback disposition;
- native error and the operation that produced it;
- evidence provenance and whether evidence is authoritative, observed, or unknown.

Unknown must remain unknown. A later global Worker degradation cause must not overwrite or stand in for per-record history. A successful local cleanup effect must not be interpreted as proof of recipient commit.

### 3. Read-only enumeration

Expose a production read-only snapshot API only after defining:
- who owns the snapshot and how long its storage remains valid;
- how Adapter admission is closed/drained;
- how direct Submitter callers and Worker activity are quiesced or leased;
- whether the result is a stable copied snapshot or a lease-protected view;
- per-inventory pagination/bounded buffers and explicit truncation;
- count/list consistency checks and cycle/corruption reporting;
- stable IDs and explicit cross-links.

Do not return raw pointers, mutable internal structures, or handles as public identities. Snapshot failure must not mutate lifecycle state.

### 4. Evidence-specific reconciliation

Reconciliation must be an owner-only, per-record state transition with a unique action/reconciliation ID and compare-and-swap against the record version. It must be idempotent under duplicate requests and reject stale versions, wrong owner epochs, wrong inventory kinds, and conflicting actions.

The first implementation slice should be read-only classification. Later action handlers must be separately specified and tested for each failure class:
- pre-I/O handle cleanup;
- caller-side admission/reservation cleanup before Issue;
- Issue accepted but handoff NOT_TRANSFERRED;
- Worker transferred-degraded;
- finalizer close uncertainty;
- reservation/unreserve uncertainty;
- post-terminal Submitter ACK failure;
- direct managed-pipe retention.

A generic `release(retained_id)` API is prohibited. Handle-close retry requires an explicit handle-ownership and substep proof; do not infer safety merely from a failed call or a zero counter. If proof is unavailable, preserve quarantine.

### 5. Replay boundary

No local recovery operation may reissue an operation when Issue is ACCEPTED or UNKNOWN. In particular, uncertain WRITE requires recipient-side stable idempotency identity plus an authoritative recipient status/receipt protocol before replay can even be considered. Those facilities are absent from the inspected live path.

Cleanup-only recovery and I/O replay must be separate APIs, permissions, audit events, and state machines.

### 6. Orchestrator integration

Only after component-level identity, snapshot, and reconciliation contracts are implemented and tested should the Orchestrator gain retained-recovery cursor steps. Proposed high-level ordering:

1. Close admission and quiesce all component callers.
2. Enumerate each inventory namespace independently.
3. Classify each stable record using immutable evidence.
4. Apply only explicitly authorized, evidence-backed local reconciliation actions.
5. Verify all cross-list, owner, reservation, registry and finalizer invariants.
6. Release a record only after every required local effect is COMMITTED and no unresolved/UNKNOWN effect remains.
7. Permit normal component teardown only after all inventories are empty and independently verified.

A record in UNKNOWN, conflicting, partially applied, or insufficient-evidence state remains quarantined and blocks teardown.

## Decision table

| Condition | Permitted behavior now | Prohibited behavior |
|---|---|---|
| Issue NOT_ATTEMPTED and independently proven | Design a narrow local cleanup action after its contract/tests exist | Generic free/unlink based only on this label |
| Issue ACCEPTED or UNKNOWN | Preserve operation; collect authoritative evidence | Reissue READ/WRITE or treat OS acceptance as recipient commit |
| Handoff NOT_TRANSFERRED after Issue accepted | Preserve caller/operation ownership and quarantine | NOT_ISSUED cleanup or replay |
| Worker transferred-degraded | Preserve Worker ownership and associated resources | Caller-side free or duplicate Worker insertion |
| Finalizer close uncertain | Preserve finalizer and owning storage | Blind repeated CloseHandle/manual handle close |
| ACK failed after terminal release | Preserve callback/ACK bookkeeping; do not repeat I/O | Re-run Issue or callback without an idempotency contract |
| Direct managed-pipe Worker retention | Track in Worker inventory namespace | Assume an Adapter envelope exists |
| Evidence conflict or stale identity/version | Fail closed and retain | Resolve by counters, pointer equality, or guessed state |

## Recommended next implementation slice

Do not implement a release/reconcile API yet. The next safe code slice, after design approval, is **identity + immutable first-retain metadata only**, with no list unlink, no retry, no handle close, no callback, no freeing, and no Orchestrator cursor mutation. It should:
- add owner-instance epoch and monotonic ID allocation before admission;
- record inventory kind and first-retain cause/stage exactly once;
- preserve the record when later Worker degradation causes change;
- add focused tests for uniqueness, exhaustion, repeated retain, concurrent retain, and distinct inventory topologies.

Then separately design and test a copied read-only production snapshot. Only later consider narrow reconciliation actions with evidence.

## Verification

- Re-read the retained observability gap report, retained cause/ownership/I/O matrix, negative integration regression report, Adapter header and full Adapter implementation.
- Inspected direct managed-pipe retained insertion sites and Worker retained insertion paths.
- Reconfirmed Working Copy path.
- Disk free at start of this pass: C: 10.17 GB; D: 4.48 GB.
- Confirmed source hashes unchanged from the preceding validation fingerprints:
  - Adapter C: `A9B39D0D4094E8418F820E53B3E28FEE64FC7B0A9EC417450F25EC422EE8D87F`
  - Adapter H: `DB52B1D6723DCA1159AD8737B83E64EFE26FF4D5F4053A32F9D1C8CD6C0AA1DC`
  - Worker C: `5B0B43BA60303A0EC3EA07A9E26A641E32E40812702C0F87EC37C1ADD11DD8DD`
  - Worker H: `5D0230BC5295AF2DFE8B8BC4B64E0E7D1938F977A446F0C785CEA0C2E0918F96`
- No build/tests run because this pass made no code changes.
