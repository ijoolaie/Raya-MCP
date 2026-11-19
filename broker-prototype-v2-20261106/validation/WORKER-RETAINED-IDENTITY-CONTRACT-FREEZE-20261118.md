# Worker Retained Identity Contract Freeze — 2026-11-18

## Scope and outcome

This pass re-inspected the active Working Copy source files and all visible call sites of `raya_managed_operation_init`. It freezes a proposed additive identity contract only. No production source, header, test, build script, README, model, or build output was changed.

## Verified source facts

- `raya_managed_operation_init` is called by the Adapter submission path, the direct managed-pipe path, and multiple Worker-native tests.
- The operation initializer has no Worker argument. It therefore cannot allocate a Worker-scoped identifier or infer whether an operation is direct or Adapter-associated.
- The direct managed-pipe path has Worker context at admission and has more than one retained insertion path in `managed-pipe-io-v2.c`.
- Adapter-associated operations are retained through Adapter quarantine code in `integrated-submit-adapter-v2.c`; that code also inserts the operation into the Worker retained list under the Worker lock in one path.
- The Adapter test snapshot currently reports Adapter instance/operation identity separately from pointer-derived Worker/operation diagnostics. The latter are not stable identifiers.
- Worker retained-list insertion is not yet one shared operation-level helper across direct and Adapter-associated paths.
- Some retained cases can occur before normal Worker reservation/handoff; therefore allocating Worker identity only in the reserve or handoff function would leave valid retained operations unidentified.
- The Worker lock is the natural serialization point for Worker sequence allocation and Worker retained metadata publication. Adapter identity remains a separate namespace and must not be substituted for Worker identity.

## Frozen contract proposal

### Identity namespace

1. Each successfully initialized Worker receives a nonzero process-local `worker_epoch`. It is unique within the process lifetime and is not durable across process restart.
2. Each Worker operation receives a nonzero monotonic `worker_operation_id` from that Worker's sequence, allocated under the Worker lock before any path can retain the operation.
3. Allocation is fail-closed: zero is never issued; sequence exhaustion rejects identity registration and must not wrap or reuse an ID.
4. The Worker key is the pair `(worker_epoch, worker_operation_id)`. Neither pointer values nor Adapter operation IDs are acceptable substitutes.

### Inventory classification

Use explicit stable enum values:
- `RAYA_WORKER_INVENTORY_UNSPECIFIED = 0` (invalid for any registered operation)
- `RAYA_WORKER_INVENTORY_DIRECT = 1`
- `RAYA_WORKER_INVENTORY_ADAPTER_ASSOCIATED = 2`

Classification is set by the owning entry path, not guessed from pointer layout or call stack.

### Optional Adapter association

Store an explicit validity flag plus the pair:
- `adapter_identity_valid`
- `adapter_instance_id`
- `adapter_operation_id`

For DIRECT operations, validity is false and both numeric fields are zero. For ADAPTER_ASSOCIATED operations, validity is true and both IDs must be nonzero. This association is correlation metadata only; it does not transfer Adapter ownership or imply that the Adapter and Worker inventories are the same.

### First-retain evidence

Worker first-retain metadata is immutable after first insertion:
- Worker-specific retain-cause enum
- Worker-specific retain-stage enum
- A single shared insertion helper under the Worker lock must perform identity validation/allocation, first-retain evidence assignment, list insertion, and count increment exactly once.

All direct Worker retain branches and Adapter-to-Worker retained-list insertion must call that helper. Repeated retain publication must not rewrite the first cause/stage or duplicate list membership.

### Diagnostic boundary

- Test-only snapshots may copy the Worker epoch, Worker operation ID, inventory kind, optional Adapter pair, and first-retain cause/stage by value.
- Pointer addresses may remain separate diagnostic-only fields if clearly labelled as addresses.
- No production enumeration, release, reconciliation, retry, replay, callback, or quarantine-removal API is introduced by this identity slice.

## Required implementation sequence

1. Add Worker epoch and monotonic sequence fields; allocate the epoch during successful Worker initialization with checked nonzero process-local allocation.
2. Add explicit operation identity/classification/association fields to `raya_managed_operation`.
3. Add one Worker-lock-protected registration helper callable before any possible retain path. It must reject conflicting re-registration and validate inventory/association combinations.
4. Add one Worker-lock-protected retained-publication helper with immutable first-retain evidence and duplicate-publication protection.
5. Convert every direct retain insertion and Adapter-to-Worker retained insertion to the shared helper without changing quarantine ownership or cleanup behavior.
6. Extend test-only snapshot structures to copy metadata by value; preserve pointer diagnostics only as explicitly non-identity fields.
7. Add deterministic tests for direct identity, Adapter-associated identity, two Workers, multiple operations, association validity, repeated retain, first-retain immutability, zero/exhaustion, and early-retain-before-reserve.
8. Build only after checking C: and D: free space and confirming exact output locations; inspect all generated artifacts afterward.

## Explicit non-goals

No durable cross-process identity, production inventory API, reconciliation/release, retry/replay, I/O behavior change, orchestrator cursor change, Worker/Adapter ownership change, or full recovery acceptance.

## Blocking design constraint

The initializer is intentionally Worker-agnostic, while early-retain paths exist before reservation. A safe implementation therefore requires a separate Worker registration step invoked by both entry paths before any possible retain, plus a common retained-publication helper used by all insertion sites. Adding fields alone or allocating only during reservation would produce incomplete identity coverage. This pass did not make those code changes because the registration call ordering and all failure exits must be converted together to preserve quarantine semantics.
