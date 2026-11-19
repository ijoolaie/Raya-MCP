# Adapter Retained Inventory and Recovery Observability Gap — 2026-11-17

Status: source-backed review; no source modification or recovery action.
Scope: Working Copy only — `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`.
Production: NO-GO. Phase 1: NOT ACCEPTED.

## Executive conclusion

The Adapter really does maintain a retained envelope list, and the Adapter's `retain_envelope` path may also link the corresponding managed operation into the Worker retained list. This corrects the earlier Worker teardown review's statement that no production insertion/removal path was found for Worker retained entries. That earlier statement was incorrect because it inspected Worker source in isolation and missed the cross-component writes in `integrated-submit-adapter-v2.c`.

The production-facing API exposes only `raya_integrated_adapter_retained_count`. It does not expose a supported enumeration, evidence inspection, reconciliation, retry, release, or per-item resolution API. A rich snapshot function exists only behind `RAYA_ADAPTER_TESTING`; it is a test diagnostic, not a production recovery contract.

Therefore: retained operations are inventoried internally, but operationally unresolvable through the current public API. The safe behavior today is fail-closed retention and blocked Adapter Destroy, not blind retry or forced freeing.

## Exact retained insertion paths

### General retain path: `retain_envelope`

The function:
1. Acquires Adapter lock and, once per envelope, sets `e->retained`, links the envelope at `adapter->retained_head`, increments `adapter->retained_count`.
2. Acquires Worker lock. If Worker does not own the operation or the Worker terminal release has not already occurred, links the managed operation at `worker->retained_head` and increments `worker->retained_count`.
3. Marks Worker degraded with the supplied recovery cause.
4. Calls `worker_finish_once`, signals Worker wake, and marks further degradation if wake signaling fails.

This is a cross-component ownership/quarantine path; it is not merely a diagnostic counter.

### Transferred-degraded quarantine path

The test-seam quarantine publisher, compiled under `RAYA_ADAPTER_TESTING`, links the envelope into Adapter retained inventory and sets `transferred_degraded_quarantined`. The Worker already owns the operation, so the separate Worker-list insertion is not performed in this helper. This path preserves the Worker-owned operation and prevents the normal callback/free path.

### Retain causes visible in submit flow

The submit path can return `RAYA_ADAPTER_RETAINED` after, among other things:
- duplicate/event handle cleanup failure;
- managed operation initialization or ownership invariant failure;
- Worker admission failure followed by failed owner/finalizer cleanup;
- Submitter admission/enqueue/abort failure;
- caller-terminal, finalizer close/commit, owner release or Worker unreserve failure;
- Worker ownership transfer reported degraded.

The causes have materially different evidence and resource states. A single retained count cannot distinguish them.

## Public API versus test-only diagnostics

Production header/API:
- `raya_integrated_adapter_retained_count(adapter)`: count only.
- `raya_integrated_adapter_destroy_ex`: reports BUSY/RECONCILE when retained count is nonzero; it does not enumerate or resolve entries.
- no production function to enumerate an envelope, obtain stable operation identity, inspect its cause/effect state, reconcile evidence, retry a safe cleanup substep, or remove a resolved entry.

Test-only API:
- `raya_adapter_test_retained_snapshot` is declared and implemented only under `RAYA_ADAPTER_TESTING`.
- Requires Adapter admission closed and entrant count zero.
- Requires Worker joined and thread pointer NULL.
- Requires Submitter queue, active producers and active issue all zero; caller must quiesce direct Submitter APIs.
- Validates list cycles/count consistency and reports envelope/operation/request pointer values plus owner/finalizer/operation/reservation/registry/Worker/Submitter state.
- Returns raw process addresses, not durable IDs or safe handles.
- Does not reconcile, mutate, retry, release or free anything.

The test snapshot is useful for proving the failure state in tests, but it cannot serve as a runtime recovery API.

## Test evidence: two distinct retained shapes

### A. Finalizer close failure before Worker ownership

The API test injects ReadFile failure and CloseHandle failure. Assertions show:
- Adapter retained count = 1; Worker retained count = 1.
- Worker degradation includes HANDLE_CLOSE.
- Worker outstanding = 0 but reserved_slots = 1.
- Submitter request is NOT_ISSUED.
- Finalizer state is CLOSE_UNCERTAIN, release commit = 0.
- Pipe Owner remains CALLER_TERMINAL.
- callback count remains zero.
- Adapter Destroy refuses.

After Worker/Submitter threads are joined, test-only snapshot can inspect the item. It still does not resolve the uncertain handle close or release the reservation.

### B. Transferred-degraded quarantine after Worker ownership

The API test asserts:
- Adapter retained count = 1 while Worker retained count = 0 for this particular shape.
- Worker core queue/outstanding = 1 and reserved_slots = 1.
- Worker registry count = 0 at the observed point; the operation remains on the Worker core queue and is still Worker-owned.
- Submitter issuer lease remains 1 in the observed snapshot point.
- callback count remains zero.
- Adapter Destroy refuses.
- Test comment explicitly preserves operation, envelope, buffer and handles until process exit.

This is a distinct ownership topology from pre-Worker retained cleanup failure. Recovery cannot be generic across both.

## Critical correction to prior report

Prior report `WORKER-TEARDOWN-DEPENDENCY-DAG-RETAINED-SAFETY-REVIEW-20261117.md` said Worker retained-list insertion/removal was not found in `reaper-managed-worker-v2.c`. That narrow file scan missed `integrated-submit-adapter-v2.c`, where `retain_envelope` links into `w->retained_head` and increments `w->retained_count`. The correct statement is:
- Worker retained-list writes do exist in the Adapter's cross-component retention path.
- No supported production enumeration/reconciliation/release API for either retained inventory was identified.
- The test-only snapshot can enumerate both under strict quiescence conditions.

## Recovery design requirements before implementation

1. Give every retained envelope a stable, non-pointer operation identity that survives the lifetime needed for evidence collection; do not expose raw pointers as identifiers.
2. Store a first-failure cause and a typed ownership/effect snapshot per envelope. Preserve distinct stages: pre-issue, submitter admitted, OS issue accepted/unknown, Worker reservation, handoff transferred/degraded, terminal I/O, finalizer, Pipe Owner release, registry/reservation release, callback/ACK.
3. Define a production read-only enumeration API with explicit quiescence/lease rules and bounded caller buffers; do not expose mutable internals.
4. Define evidence-specific resolution actions. A failed CloseHandle may be retried only under a precise handle-ownership and idempotency proof. An uncertain overlapped I/O must never be reissued merely because it is retained.
5. Separate cleanup-only recovery from I/O replay. A resolved local handle/finalizer failure does not imply that remote WRITE effects did not happen.
6. Define cross-list invariants: one envelope identity, exactly one owning topology, consistent Adapter/Worker links, reservation and registry relation, and no double release.
7. Only after the production recovery API and invariants are designed, add owner-only Orchestrator cursor steps for enumerate → classify → reconcile → release. Keep UNKNOWN/uncertain states quarantined.
8. Add fault-injection tests for every retention cause and both ownership topologies, including duplicate reconciliation, stale identity, close retry, Worker abnormal exit, and Adapter destroy retry.

## Current safe operational rule

- `retained_count == 0`: this is a necessary Adapter Destroy condition, not by itself proof that Worker/Submitter teardown is safe.
- `retained_count > 0`: stop teardown at Adapter; preserve Adapter, Worker, Submitter, envelope and associated handles/storage as required by the specific ownership state.
- no generic retry, list unlink, counter decrement, manual handle close, free, or process-exit assumption is authorized by this review.

## Verification and scope

- Inspected Adapter header and implementation, Worker and integrated reaper interfaces/implementation, Adapter API tests and the test-only snapshot implementation.
- Verified actual cross-component retained list writes in Adapter implementation.
- No build or tests run; prior test assertions were inspected, not re-executed.
- No source/header/test/config/README changed.
- No process terminated, install/download performed, or temporary artifact created/deleted.
- No Production changes.

## Source fingerprints

| File | SHA-256 |
|---|---|
| integrated-submit-adapter-v2.h | DB52B1D6723DCA1159AD8737B83E64EFE26FF4D5F4053A32F9D1C8CD6C0AA1DC |
| integrated-submit-adapter-v2.c | A9B39D0D4094E8418F820E53B3E28FEE64FC7B0A9EC417450F25EC422EE8D87F |
| integrated-submit-adapter-v2-api-test.c | 9F62641155FF413A69DEA3DDDCBD26DF1B062184B6ADE359BF9F42B05C1A142B |
| reaper-managed-worker-v2.h | 5D0230BC5295AF2DFE8B8BC4B64E0E7D1938F977A446F0C785CEA0C2E0918F96 |
| reaper-managed-worker-v2.c | 5B0B43BA60303A0EC3EA07A9E26A641E32E40812702C0F87EC37C1ADD11DD8DD |
| reaper-integrated-v2.h | 557072426F216D1AC33BA65E885ADA72890FBBDC8DAFF7BD614492AE2F6DA1AD |
| reaper-integrated-v2.c | 3CFECEB6850324C0D8436D161F26C010555E63C3E1CB8F5F597829A53725E952 |
