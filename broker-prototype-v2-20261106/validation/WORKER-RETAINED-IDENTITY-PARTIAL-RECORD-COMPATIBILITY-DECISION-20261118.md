# Worker Retained Identity — Partial Record Compatibility Decision (2026-11-18)

## Decision
Preserve existing Worker retained-list membership semantics for partial/pre-initialization records in this additive identity slice, but represent them explicitly as records with no valid Worker operation identity.

This avoids changing quarantine/ownership behavior while preventing false identity claims.

## Record classes
- INITIALIZED_OPERATION: raya_managed_operation_init returned success; receives a Worker identity only after that call and before any later retention-capable operation.
- PARTIAL_OPERATION_RECORD: an operation allocation is retained after event/duplicate-handle/finalizer setup failure, or after raya_managed_operation_init returned failure and the caller reconstructed only the cleanup fields. It remains eligible for the existing Worker retained list, but Worker identity-valid is false and Worker epoch/operation ID are zero.
- ADAPTER_ENVELOPE_ONLY: retained Adapter envelope that does not enter the Worker retained list under the existing retain_envelope predicate (for example while Worker still owns the operation). It has Adapter identity, but no Worker retained-list membership claim.

## Why preserve partial Worker list membership
- Current direct path inserts partial records into Worker retained list when duplicate-handle cleanup fails or operation initialization fails.
- Current Adapter path's retain_envelope inserts the embedded operation into Worker retained list when the envelope is newly retained and either Worker never owned the operation or terminal release has occurred.
- Changing this predicate during identity work would alter established quarantine inventory and potentially shutdown/destroy safety. The identity feature does not need that behavioral change.
- A missing Worker identity is therefore a valid and expected state for a Worker retained-list item; list membership and operation identity are separate facts.

## Identity and evidence fields
- Worker-level: nonzero process-local worker_epoch; monotonic next_operation_id allocator protected by Worker lock.
- Operation-level: identity_valid, worker_epoch, worker_operation_id, inventory_kind, optional Adapter association valid flag and (adapter_instance_id, adapter_operation_id).
- For partial records: identity_valid=FALSE, epoch/id zero, inventory kind identifies the attempted origin when known (DIRECT or ADAPTER_ASSOCIATED); Adapter association may be copied only when the Adapter IDs are already allocated and immutable.
- First-retain evidence is separate from operation identity: a partial record can have immutable first-retain cause/stage even without a Worker operation ID.
- Existing pointer values remain address diagnostics only; rename them in test-only snapshots instead of repurposing.

## Publication contract
One Worker-lock-held helper will:
1. Check whether the exact operation pointer is already linked; reject duplicate publication without incrementing count.
2. If first-retain evidence is unset, set Worker recovery cause and Worker-specific retain stage exactly once.
3. Link the operation and increment retained count exactly once.
4. Never allocate IDs, close/free handles, release leases, run callbacks, or call Adapter code.
5. Accept identity-less partial records, but enforce identity invariants whenever identity_valid=TRUE.

Identity registration is a separate Worker-lock-protected API:
- Valid only after successful operation init.
- Allocates nonzero monotonic per-Worker operation ID and stamps epoch/inventory/optional Adapter pair.
- Refuses invalid combinations, zero/wrap/exhaustion, duplicate registration, or registration against an uninitialized Worker.
- Does not require an admission lease; this supports explicit lifecycle semantics without coupling identity to admission.
- Must be called before any later path can retain the initialized operation.

## Locking
- Publication helper requires caller already hold Worker lock.
- Adapter retention must not nest Adapter lock around Worker lock. Current code releases Adapter lock before entering Worker lock; preserve that ordering.
- Degraded-handoff callback continues to publish Adapter quarantine under Worker -> Adapter order and does not implicitly add Worker retained-list membership.
- Snapshot continues to require quiescence and must copy identity/evidence fields by value.

## Required tests
- Worker epoch nonzero and differs across sequential Worker instances.
- Direct initialized operation receives unique ID.
- Adapter initialized operation receives unique ID and exact Adapter association pair.
- Partial direct records remain in Worker list with identity_valid false and zero ID.
- Partial Adapter records preserve current list membership predicate and have identity_valid false.
- Adapter-only envelope is not misreported as Worker list membership.
- Duplicate publication does not duplicate list node or count.
- First-retain cause/stage remains unchanged on repeated retain attempt.
- Sequence exhaustion and invalid association fail closed.
- Snapshot pointer diagnostics remain address-only and are not compared as stable IDs.

## Scope boundary
This change does not add production enumeration, release, reconciliation, replay, retry, or quarantine removal. It does not alter Adapter ownership, Submitter acknowledgement, Worker lifecycle, operation issue semantics, or orchestrator cleanup cursors.

## Current turn
- Source/header/test/build files changed: none.
- No build/test run.
- No artifacts deleted.
- This report is the compatibility decision artifact and was read back after writing.
- Worker identity implementation and identity-specific tests remain NOT STARTED.
- Stage 5 implementation/recovery acceptance remains BLOCKED / NOT ACCEPTED.
- Phase 1 NOT ACCEPTED; Production NO-GO.
