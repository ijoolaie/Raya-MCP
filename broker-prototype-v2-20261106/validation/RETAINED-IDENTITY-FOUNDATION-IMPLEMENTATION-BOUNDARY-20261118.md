# Retained Identity Foundation — Existing State and Implementation Boundary — 2026-11-18

## Scope
Inspected current Working Copy identity and retention fields in `integrated-submit-adapter-v2.h/.c`, `reaper-managed-worker-v2.h/.c`, `reaper-integrated-v2.h`, all checked-in C call sites of `raya_managed_operation_init`, and current test-only retained snapshot structure. This is an implementation-boundary review. No source/header/test/build script/README changed; no build/test rerun.

## Existing foundation already present

### Adapter inventory
- Adapter has a process-local monotonic `instance_id`, allocated by InterlockedIncrement64; non-positive/wrap/exhaustion fails closed.
- Each Adapter submission receives a monotonic `operation_id` under the Adapter lock; wrap/exhaustion fails closed.
- Adapter envelope stores the operation ID.
- First Adapter retain records `first_retain_cause` and `first_retain_stage` only when `!e->retained`; later retain attempts do not overwrite the first values.
- The test-only snapshot exposes Adapter instance ID, stable operation ID, first retain cause and stage.
- Thus Adapter's current effective key is the pair `(adapter_instance_id, operation_id)`. The instance ID can serve as a process-local Adapter owner epoch for that namespace, but is not a durable cross-process epoch.

### Worker inventory
- `raya_managed_operation` has no stable operation ID, owner epoch, inventory kind, or first-retain cause/stage.
- `raya_managed_worker` has no instance ID or operation sequence.
- Worker retained list contains operation pointers; the test-only snapshot exports raw pointer values only.
- Direct managed-pipe operations created through `managed-pipe-io-v2.c` and standalone Worker tests do not pass through Adapter ID allocation.
- Therefore Adapter identity cannot be assumed for every Worker retained operation, and Worker retained list cardinality cannot be treated as Adapter-envelope cardinality.

## Important identity distinction
The Adapter's `operation_id` is already a stable per-Adapter logical ID, whereas snapshot fields named `operation_id` / `envelope_id` are pointer-derived diagnostic addresses. These are different concepts and must not be conflated. The pointer fields are process-local diagnostics, not stable identities.

## First implementation slice — proposed, not applied

### Identity schema
Use a small explicit identity tuple on every managed operation:
- `owner_instance_id`: nonzero process-local Worker instance epoch.
- `operation_id`: nonzero monotonic sequence within that Worker epoch.
- `inventory_kind`: enum with distinct values for direct Worker operation and Adapter-associated Worker operation.
- Adapter envelope retains its existing Adapter-local `(adapter_instance_id, adapter_operation_id)` identity as a separate association, not as a replacement for Worker operation identity.

Worker identity allocation must be centralized in the Worker admission/reservation path, under Worker lock, so direct and Adapter-associated operations share one collision-free namespace. Allocation failure/exhaustion must reject before ownership transfer and must not reuse zero or a prior value. Whether an operation ID is consumed on a later admission failure is intentionally monotonic and acceptable; gaps are harmless.

### First-retain metadata
- Keep Adapter's existing first cause/stage immutability.
- Add a separate Worker-retained first cause/stage only when the Worker node is first linked into the Worker retained list.
- Define the Worker retain-cause/stage domain explicitly before adding fields; do not silently map Adapter causes to Worker causes when the two failure planes differ.
- Inventory kind is immutable at operation initialization/admission, not inferred later from pointer membership.
- No record version/effect ledger is included in this first slice.

### Cross-inventory association
- Adapter-associated Worker operation stores the Adapter identity pair as an explicit optional association.
- Direct Worker operations have no Adapter association; represent absence explicitly (zero tuple plus a validity flag or tagged optional), never by a guessed pointer.
- Worker identity remains the primary key in Worker inventory. Adapter identity remains the primary key in Adapter inventory.

## Preconditions before code edit
1. Freeze enum values and optional-association representation.
2. Confirm every operation admission path reaches the centralized Worker identity allocator before transfer.
3. Confirm Worker retained-list insertion has one authoritative first-link site; if multiple sites exist, funnel through a helper that only sets first metadata on first insertion.
4. Extend test-only snapshots with copied identity/inventory/cause fields; do not expose new production enumeration or mutation APIs in this slice.
5. Preserve the legacy pointer diagnostics only as explicitly named diagnostic addresses, not as identity.
6. Add deterministic tests for direct Worker, Adapter-associated Worker, multiple operations, two Worker instances, first-retain immutability, zero/exhaustion failure, and duplicate/repeated retain.
7. Build only focused Worker and Adapter test targets after checking exact output files and disk space.

## Safety boundary
This slice must not:
- enumerate retained production state;
- unlink or release retained nodes;
- close handles, free buffers/envelopes, decrement counters, invoke callbacks;
- retry completion or replay I/O;
- persist identity across process restarts;
- change Orchestrator cleanup cursor or teardown behavior.

## Decision
The Adapter namespace already has a meaningful local identity and immutable first-retain cause/stage. The material gap is Worker-wide identity for both direct and Adapter-associated operations, explicit inventory-kind and association metadata, and Worker first-retain metadata. No source changes were made because adding only one field or reusing Adapter IDs would create an incomplete identity scheme for direct Worker operations. The next safe code change is a coordinated Worker identity allocator plus additive operation metadata and focused tests, after the enum/association contract is fixed.

## Verification
- Confirmed the Adapter identity allocator and immutable first-retain assignment in source.
- Confirmed Worker operation/worker structs currently lack identity and Worker retain metadata.
- Enumerated all current `raya_managed_operation_init` call sites; direct Worker and Adapter paths both exist.
- Confirmed no `.git` metadata exists at Working Copy root; no git status/commit claim is made.
- No test/build outputs created; no cleanup performed.

No source changes, no build artifacts created, no Commit/Reset.
