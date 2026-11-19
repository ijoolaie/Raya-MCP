# Production Retained Inventory — Read-Only Contract Decision Record
**Date:** 2026-11-18  
**Status:** Proposed implementation baseline derived from current source and test-only snapshot behavior. This is not authorization for mutation, resource release, or replay.

## Decision
Implement two independent, caller-quiesced, copied read-only inventory snapshots:
1. Adapter-envelope inventory snapshot.
2. Worker-operation inventory snapshot.

Do not expose a combined atomic Adapter/Worker/Submitter snapshot. The existing test-only snapshot takes component locks separately and cannot promise atomic cross-plane state. A caller that needs correlation obtains the two snapshots separately and joins only through explicit stable Adapter identity pairs.

## Contract decisions

### Ownership and lifetime
- Caller must keep the target Adapter or Worker object alive for the entire call.
- Snapshot storage is caller-owned and copied by value; no internal pointers or handles are returned.
- The API does not acquire ownership of the object and does not extend its lifetime.
- No snapshot view remains valid after the call; callers own the returned copy.

### Quiescence
- Adapter snapshot: caller first closes Adapter admission, drains all Adapter entrants, and quiesces the associated Submitter (no queued requests, active issue, active producers, or direct Submitter callers).
- Worker snapshot: caller first prevents new Worker admission and joins the Worker thread; Worker retained inventory is then stable for copying.
- Do not nest Adapter, Worker, and Submitter locks to manufacture an atomic view.
- Non-quiescent calls fail without modifying lifecycle state.

These are intentionally conservative preconditions grounded in the already exercised test snapshot. Relaxing them requires a separate synchronization design and evidence; it is not part of the first production API slice.

### Inventory separation and identity
- Adapter records use the pair (adapter_instance_id, adapter_operation_id).
- Worker records use the pair (worker_epoch, worker_operation_id) when identity is valid.
- Worker DIRECT and ADAPTER_ASSOCIATED kinds remain distinct.
- An Adapter association on a Worker record is an optional correlation pair only; it does not transfer ownership.
- Identity-less partial Worker records remain enumerable with identity_valid=false and zero identity fields. Never synthesize an ID or infer kind from pointer/address/list order.
- Adapter and Worker counts are independent. Never compare them as if they were the same inventory.

### Copied fields
The first version copies only:
- record identity validity and namespace;
- inventory kind;
- Worker epoch and operation ID, where valid;
- optional Adapter instance and operation IDs;
- immutable first-retain cause and stage;
- a small, explicitly enumerated set of lifecycle facts proven stable under the owning object's lock and quiescence preconditions.

Do not copy raw addresses, HANDLE values, caller buffers, internal pointers, or unsynchronized Submitter/Worker mutable fields. If a field's synchronization cannot be proven, omit it rather than return a guessed value.

### Buffer and versioning
- Caller supplies an output array and capacity.
- The API reports the total required record count independently from the number copied.
- If capacity is insufficient, return an explicit insufficient-capacity result and do not silently present a truncated snapshot as complete.
- Output records carry a structure-size/version field. Unknown future fields are ignored by callers that understand the declared version; incompatible versions fail explicitly.
- No heap allocation is performed by the snapshot API.

Exact C type names, enum numeric values, and function signatures remain for the ABI freeze step; do not invent them in implementation before reviewing all public-header compatibility constraints.

### Error outcomes
The contract must distinguish at least:
- invalid/null object;
- object not initialized;
- required quiescence not established;
- insufficient caller capacity (with required count);
- retained-list structural inconsistency (cycle/count mismatch);
- unsupported version/structure size;
- internal failure.

Every failure is read-only and leaves ownership, lists, handles, counters, callbacks, and lifecycle state unchanged.

### Strict no-side-effect boundary
Snapshot calls must never:
- unlink or relink a retained record;
- close or duplicate a handle;
- free memory or decrement a retained count;
- acknowledge or notify a Submitter;
- invoke a callback/finalizer;
- retry completion observation or issue I/O;
- mutate lifecycle, degradation, reservation, registry, or Orchestrator cursor state;
- authorize or trigger replay.

Process-local identity is not durable across process restart and must not be advertised as a crash-recovery key.

## Explicitly deferred
- Reconciliation, release, retry, callback retry, and replay.
- Any generic release-by-ID interface.
- Cross-process/durable identity.
- Atomic cross-plane snapshots.
- Relaxed live/concurrent enumeration.
- Recipient-side WRITE receipt/idempotency protocol.

Unknown or conflicting I/O outcomes remain quarantined. An accepted or outcome-unknown WRITE is never replayed based on local retained inventory.

## Implementation sequence after this record
1. Public-header ABI review for additive versioned record types and status enums.
2. Source lock/lifetime review for the exact copied fields and preconditions.
3. Implement Adapter snapshot and Worker snapshot separately, with no mutation.
4. Add only the focused contract tests listed in PROJECT-ROADMAP.md.
5. Update the roadmap and this record with exact implementation evidence before considering reconciliation.

## Evidence inputs
- PRODUCTION-RETAINED-ENUMERATION-RECONCILIATION-CONTRACT-DRAFT-20261118.md
- RETAINED-RECOVERY-API-CONTRACT-GAP-ANALYSIS-20261118.md
- RETAINED-SNAPSHOT-STABLE-IDENTITY-VALIDATOR-20261118.md
- STAGE-5-SHARED-WORKER-DUAL-ADAPTER-RUNTIME-RESULTS-20261118.md
- STAGE-5-LIFECYCLE-RETAINED-RECOVERY-FINAL-REPORT-20261118.md and its dated supersession note

## Gate
Contract decision record: WRITTEN.  
Formal ABI freeze: PENDING.  
Production enumeration implementation: NOT STARTED.  
Reconciliation/release: NOT AUTHORIZED.  
Stage 5 acceptance: BLOCKED / NOT ACCEPTED.  
Phase 1: NOT ACCEPTED. Production: NO-GO.

## Public-header and lifetime review addendum (2026-11-18)

The focused review is recorded in PRODUCTION-RETAINED-INVENTORY-PUBLIC-HEADER-LIFETIME-REVIEW-20261118.md.

Contract refinements:
- Keep two independent APIs and record arrays: Adapter envelope inventory and Worker operation inventory. No combined atomic snapshot.
- Do not change existing public object struct layouts. Add only new versioned record types and functions after ABI freeze.
- Adapter snapshot preconditions: caller uses the existing raya_integrated_adapter_destroy_ex admission-close behavior and receives BUSY with admission_closed=true, zero entrants, zero active operations, and retained_count>0; Worker is joined/thread NULL; caller closes and drains Submitter operational admission using raya_submitter_admission_close, and verifies queue/active-producer/active-issue counts are zero; direct raw-pointer Submitter callers are quiesced.
- The snapshot itself does not close admission or call destroy. It copies no Submitter state, request pointers, operation pointers, HANDLEs, or raw addresses.
- Worker snapshot preconditions: Worker shutdown/admission closure has completed and Worker is joined/thread NULL; caller preserves object lifetime.
- Each snapshot traverses/copies only its own inventory under its owning lock. Cross-plane correlation occurs afterward by explicit Adapter identity pair; no nested Adapter/Worker/Submitter locks.
- Adapter record v1 is limited to Adapter instance ID, Adapter operation ID, and first-retain cause/stage; it intentionally contains no envelope lifecycle flags. Worker record v1 is limited to identity validity, Worker epoch/operation ID, inventory kind, optional Adapter pair, and first-retain cause/stage. Any field not proven stable is omitted.
- Accept the existing BUSY destroy result as the first API's Adapter admission-close precondition; no separate close-only API is added in this slice.

The remaining ABI freeze is bounded to exact C record field types/order, version/struct-size compatibility, status enum values, and function signatures.

## Formal ABI freeze — version 1 (2026-11-18)

**Decision: FROZEN FOR THE FIRST READ-ONLY IMPLEMENTATION SLICE.** This freezes only the additive copied-inventory ABI below. It does not authorize reconciliation, release, retry, callback retry, or replay. Existing public object layouts remain unchanged.

### Public status values

    typedef enum {
        RAYA_RETAINED_INVENTORY_OK = 0,
        RAYA_RETAINED_INVENTORY_INVALID_ARGUMENT = 1,
        RAYA_RETAINED_INVENTORY_NOT_INITIALIZED = 2,
        RAYA_RETAINED_INVENTORY_NOT_QUIESCENT = 3,
        RAYA_RETAINED_INVENTORY_INSUFFICIENT_CAPACITY = 4,
        RAYA_RETAINED_INVENTORY_INCONSISTENT = 5,
        RAYA_RETAINED_INVENTORY_UNSUPPORTED_VERSION = 6,
        RAYA_RETAINED_INVENTORY_INTERNAL_ERROR = 7
    } raya_retained_inventory_status;

Values are fixed and must not be renumbered. Success is zero. All nonzero results are side-effect-free failures.

### Version 1 record layouts

    #define RAYA_ADAPTER_RETAINED_RECORD_VERSION_1 1u
    #define RAYA_WORKER_RETAINED_RECORD_VERSION_1  1u

    typedef struct {
        DWORD struct_size;
        DWORD version;
        ULONGLONG adapter_instance_id;
        ULONGLONG adapter_operation_id;
        DWORD first_retain_cause;
        raya_adapter_retain_stage first_retain_stage;
    } raya_adapter_retained_record_v1;

    typedef struct {
        DWORD struct_size;
        DWORD version;
        BOOL identity_valid;
        raya_worker_inventory_kind inventory_kind;
        ULONGLONG worker_epoch;
        ULONGLONG worker_operation_id;
        BOOL adapter_identity_valid;
        ULONGLONG adapter_instance_id;
        ULONGLONG adapter_operation_id;
        DWORD first_retain_cause;
        raya_worker_retain_stage first_retain_stage;
    } raya_worker_retained_record_v1;

The record is copied by value. No pointers, HANDLEs, raw addresses, buffers, request/operation addresses, or mutable Submitter state are included. Adapter records contain only the Adapter identity pair and first-retain evidence. Worker records contain the Worker identity pair when valid, inventory kind, optional Adapter correlation pair, and first-retain evidence.

For an identity-less partial Worker record: identity_valid=FALSE, Worker IDs are zero, adapter_identity_valid=FALSE, Adapter IDs are zero, and inventory_kind=RAYA_WORKER_INVENTORY_UNSPECIFIED. Do not synthesize an identity. For a DIRECT record, Adapter association is false and both Adapter IDs are zero. For an ADAPTER_ASSOCIATED record, the association flag is true and both Adapter IDs are nonzero.

### Function signatures

    raya_retained_inventory_status
    raya_integrated_adapter_copy_retained_inventory_v1(
        raya_integrated_submit_adapter *adapter,
        DWORD requested_version,
        raya_adapter_retained_record_v1 *records,
        size_t capacity,
        size_t *required_count);

    raya_retained_inventory_status
    raya_managed_worker_copy_retained_inventory_v1(
        raya_managed_worker *worker,
        DWORD requested_version,
        raya_worker_retained_record_v1 *records,
        size_t capacity,
        size_t *required_count);

These are two independent APIs. No combined snapshot API is defined. The target object must remain alive for the entire call. Caller owns the output array and record copies. A NULL records pointer is valid only when capacity is zero; required_count is mandatory.

### Return and capacity semantics

- On entry, a non-NULL required_count is set to zero before other validation.
- NULL object, NULL required_count, or NULL records with nonzero capacity returns INVALID_ARGUMENT.
- An uninitialized object returns NOT_INITIALIZED.
- A caller that has not established the exact quiescence preconditions below receives NOT_QUIESCENT. The API does not close admission, wait, join, destroy, or otherwise establish quiescence.
- requested_version other than the corresponding version-1 constant returns UNSUPPORTED_VERSION.
- The implementation validates retained-list structure and its stored count while holding only that inventory's owning lock. A cycle, count mismatch, or invalid list structure returns INCONSISTENT.
- If capacity is smaller than the full inventory count, return INSUFFICIENT_CAPACITY, set required_count to the full required count, and do not write any output record. No partial/truncated success is permitted.
- On success, copy every record, set each record's struct_size and version, and set required_count to the copied count.
- INTERNAL_ERROR is reserved for an unexpected internal failure; it must not be used to mask a known validation outcome.
- No heap allocation occurs. Count arithmetic must be checked for overflow before copying.

### Exact caller-established quiescence

**Adapter API:** caller keeps Adapter, Worker, and Submitter storage alive; obtains the existing raya_integrated_adapter_destroy_ex BUSY result with admission_closed=TRUE, zero entrants, retained_count greater than zero, and active_count equal to retained_count (active_count includes retained envelopes, so requiring active_count=0 would make a retained snapshot impossible); completes Worker shutdown/join so thread == NULL; closes Submitter operational admission through raya_submitter_admission_close; verifies Submitter queue, active producers, and active issue are zero; and separately quiesces all direct raw-pointer Submitter callers. The snapshot call itself does not call destroy or mutate admission.

**Worker API:** caller keeps Worker storage alive, completes Worker shutdown/admission closure and join, and verifies thread == NULL. The Worker inventory is copied under the Worker lock only.

Each API takes only its own inventory lock. There is no nested Adapter/Worker/Submitter lock acquisition and no atomic cross-plane guarantee. Correlation is performed by the caller after both calls using the explicit Adapter identity pair only.

### ABI compatibility and extension rule

- Existing public object structures are not extended or reordered.
- Version-1 enum values and record field order/types are frozen as written here.
- Each record's struct_size is set to sizeof its version-1 type.
- A future incompatible record layout requires a new record type, version constant, and function suffix; version 1 is not silently reinterpreted.
- The status enum values remain stable across record versions.
- No new close-only lifecycle API is added in this slice; the existing BUSY destroy result is the documented Adapter admission-close precondition.

### Explicitly outside this ABI

No reconciliation, unlink, release, close, free, callback/finalizer, ACK, lifecycle mutation, retry, replay, durable identity, recipient receipt, or authorization mechanism is exposed. Accepted or outcome-unknown WRITE remains non-replayable.

### Gate result

- Semantic inventory decisions: FROZEN.
- Public record layouts/status values/function signatures: FROZEN for v1.
- Source implementation: NOT STARTED.
- Focused API verification: PENDING implementation.
- PR inventory: BLOCKED; installed GitHub repository search for "Raya MCP broker prototype" returned no matching repository. No repository or PR identity is inferred.
- Stage 5 implementation/recovery acceptance: BLOCKED / NOT ACCEPTED.
- Phase 1: NOT ACCEPTED. Production: NO-GO.
