# Read-Only Retained Inventory — Public Header and Lifetime Review
**Date:** 2026-11-18  
**Scope:** Read-only inspection of public headers and current snapshot/lifecycle implementation. No source or test files changed.

## Findings

### Existing public object layout
- `raya_integrated_submit_adapter` is a concrete public struct. It already contains its lock, process-local Adapter instance/sequence IDs, Submitter and Worker pointers, Adapter retained-list head/count, active count, and admission gate.
- `raya_managed_worker` is a concrete public struct. It already contains its lock, thread, Worker retained-list head/count, Worker epoch/sequence, and joined/started/initialized flags.
- `raya_managed_operation` already contains Worker identity, inventory kind, optional Adapter association, and first-retain cause/stage.
- Adapter envelope itself is opaque in the public header; its private layout contains Adapter operation ID, first-retain evidence, lifecycle flags, request/operation records, and linked-list pointer.
- Therefore the first read-only API slice can add new record types and functions without adding fields to existing public object structs. Avoid changing existing public struct layouts.

### Existing test snapshot is not a production API template
`raya_adapter_test_retained_snapshot` is declared and compiled only under `RAYA_ADAPTER_TESTING`. It:
- requires Adapter admission closed and no Adapter entrants;
- requires Worker joined and Worker thread NULL;
- checks Submitter queue, active producers, and active issue under Submitter lock; caller must separately quiesce direct Submitter API callers;
- copies Adapter records under Adapter lock, Worker records under Worker lock, then Submitter state under Submitter lock;
- traverses Worker registry while holding Worker lock;
- exposes raw envelope/operation/request addresses as diagnostic fields;
- returns a combined view whose component reads are sequential, not one atomic cross-plane snapshot.

Production API must not copy raw addresses or simply remove the testing macro from this function. The test-only shape includes fields and cross-plane diagnostics that are unsuitable as a stable production contract.

### Lock and lifetime evidence
- Adapter retained-list insertions occur under Adapter lock. Normal Adapter quarantine releases Adapter lock before acquiring Worker lock; preserve this non-nested pattern.
- Worker retained-list publication is explicitly a caller-holds-Worker-lock operation. It validates identity and first-retain evidence, rejects duplicate membership, links the record, and increments Worker retained count while that lock is held.
- Worker epoch and operation identity are assigned under Worker lock.
- Submitter admission-close API atomically closes the operational API gate and reports active public entrants. It does not itself establish queue/issue drain; the caller must also verify queue and active counters.
- Adapter currently has no separate close-admission-only API. `raya_integrated_adapter_destroy_ex` closes Adapter admission first, then returns BUSY if entrants, active operations, or retained entries remain. For a non-empty retained Adapter, the existing way to establish closed admission is a BUSY destroy result with admission_closed=true, zero entrants, and retained_count>0. The snapshot function itself must not call destroy or mutate the gate.
- Worker shutdown/join are lifecycle operations that stop new work and establish a joined Worker; they are caller actions before snapshot, not side effects of the snapshot API.

## Contract refinements required
1. Keep Adapter and Worker snapshots as separate API calls and separate output types.
2. Adapter snapshot preconditions must explicitly include: Adapter initialized; admission closed; zero Adapter entrants; zero active Adapter operations; Worker joined/thread NULL; Submitter operational admission closed and drained; queue, active producers, and active issue zero; direct raw-pointer callers quiesced. If a Submitter fact cannot be guaranteed from the caller's preconditions, omit it from the first snapshot record.
3. Worker snapshot preconditions: Worker initialized; shutdown/admission closure completed; Worker joined/thread NULL; caller guarantees Worker storage remains alive throughout the call.
4. Snapshot calls take only the owning inventory lock for their list traversal/copy. No cross-component nested lock acquisition.
5. The Adapter snapshot may copy Adapter identity and first-retain metadata plus only those envelope flags proven stable after the above quiescence. It must not expose request/operation pointers or unsynchronized Submitter/Worker state.
6. The Worker snapshot may copy Worker identity, inventory kind, optional Adapter identity pair, and first-retain cause/stage. Identity-less partial records remain explicitly identity-less.
7. Existing `struct_size` result conventions provide a local precedent for versioned records, but exact record layout, version semantics, and function signatures still require ABI freeze.
8. The existing Adapter destroy API is an awkward but source-backed admission-close mechanism. Decide whether the first API contract accepts this caller precondition or requires a narrowly scoped close-admission API; do not silently add one during snapshot implementation.

## Gate status
- Public struct layout review: COMPLETE.
- Current lock/lifetime boundary review: COMPLETE for the inspected paths.
- Exact production copied-field list: PARTIAL; omit fields without proven synchronization.
- Adapter admission-close API decision: OPEN.
- ABI freeze: PENDING.
- Production read-only enumeration: NOT IMPLEMENTED.
- No build/test required or run for this source/header review.


## Superseding source-backed clarification — Adapter active_count (2026-11-18)

The frozen inventory contract was reconciled against the implementation of adapter_free and Adapter submit tracking. active_count is incremented when an envelope is tracked and decremented only when that envelope is freed. A retained envelope is deliberately not freed, so it remains included in active_count. Therefore the earlier phrase "zero active Adapter operations" is not a satisfiable precondition for a non-empty retained inventory.

For the read-only Adapter inventory API, the correct quiescence condition is:
- Adapter admission is closed and Adapter entrants are zero;
- Adapter retained list is non-empty and structurally consistent;
- active_count equals retained_count, meaning every remaining tracked envelope is retained and no non-retained operation remains active;
- Worker is joined/thread NULL and its caller-active count is zero;
- Submitter admission is closed with zero API entrants; queue_count, active_producers, and active_issue are zero;
- caller has separately quiesced direct raw-pointer Submitter callers.

The API must check these conditions without changing them. It must not require active_count==0. This addendum supersedes only the contradictory active-count wording above; all other lifetime, lock-order, and read-only decisions remain unchanged.
