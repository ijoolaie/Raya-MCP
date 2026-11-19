# Adapter Retained Identity and First-Cause Metadata Slice — 2026-11-18

## Scope

Implemented in Working Copy only:

`D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`

This slice adds process-local Adapter instance identity, per-Adapter monotonic operation identity, and immutable first-retain cause metadata. It does not add a production enumeration, reconciliation, retry, release, unlink, or recovery API.

## Implementation

### Identity

- Each successful Adapter initialization receives a process-local positive `ULONGLONG instance_id` from an Interlocked process-wide sequence.
- A non-positive sequence result (including signed wrap/exhaustion) fails Adapter initialization closed.
- Each admitted submit attempt allocates a positive monotonic `operation_id` under the Adapter lock, scoped by its Adapter instance.
- Operation ID exhaustion fails the submit attempt before resource admission.
- The composite identity is `(adapter_instance_id, operation_id)`.
- The operation ID is not a pointer and does not depend on allocator address reuse.
- Identity is process-local and in-memory only. It is not persisted, not stable across process restart, and is not yet propagated to the recipient or direct managed-pipe API.

### First-retain cause

- On the first transition into Adapter retained inventory, the supplied cause is copied into `first_retain_cause` under the Adapter lock.
- Subsequent retain calls do not overwrite the first cause.
- Worker-wide degradation causes continue to accumulate independently; they are not treated as a substitute for the per-envelope first cause.
- The cause is a classification only, not proof that any cleanup or I/O effect committed.

### Test-only diagnostic

The existing `RAYA_ADAPTER_TESTING` snapshot now returns:
- Adapter instance ID;
- stable per-Adapter operation ID;
- first-retain cause.

It still returns legacy pointer diagnostics for test correlation. This remains a test-only diagnostic, not a production API or durable record.

## Explicitly deferred

- First-retain stage/substage metadata (the current cause alone does not fully locate the failure boundary).
- Stable IDs for direct managed-pipe Worker-only retained operations.
- A process-wide shared identity namespace across Adapter and direct managed-pipe entry points.
- Immutable per-effect ledger and evidence provenance.
- Production copied snapshot API, pagination and consistency/version semantics.
- Owner-only reconciliation API or any resource release/replay behavior.
- Recipient-visible idempotency identity and authoritative receipt/status query.

No uncertain READ/WRITE is retried. No retained item is unlinked, closed, freed, or otherwise reclaimed by this change. Adapter Destroy continues to refuse while retained items exist.

## Validation

### Normal configured build

Ran `build-integrated-submit-adapter-v2-api-test.cmd` with Visual Studio 2026 Build Tools 18.7.3, x64, `/W4 /WX /TC`.

- 10 consecutive executable invocations.
- Each: 751 assertions across 66 cases passed.
- Total: 7,510 assertions.
- Exit code 0.
- Expected temporary executable/object artifacts remaining: 0.

### Test-seam build

Separately compiled the same source set with `/DRAYA_ADAPTER_TESTING`, x64, `/W4 /WX /TC`, then ran once.

- 894 assertions across 68 cases passed.
- Exit code 0.
- Expected temporary executable/object artifacts remaining after explicit cleanup: 0.

The test-seam run exercises the extended retained snapshot and first-cause/identity assertions. It remains a test-only diagnostic.

### Files changed

- `integrated-submit-adapter-v2.h`: Adapter identity counters and test snapshot fields.
- `integrated-submit-adapter-v2.c`: instance/operation ID allocation, envelope identity, first-retain cause capture, test snapshot population.
- `integrated-submit-adapter-v2-api-test.c`: retained snapshot assertions for nonzero instance identity, first operation ID, and first-retain cause.

No README, build script, project configuration, Orchestrator, Worker, Submitter, direct managed-pipe source, or Production entrypoint was changed. No commit or reset.

## Remaining safety boundary

The composite identity is only unique within this process lifetime and Adapter instance. It is not yet a global identity across all retained inventory domains, and it cannot support crash recovery or recipient-side deduplication. The first cause is not a complete stage/effect ledger. Therefore this slice does not close the retained observability gap and does not justify any production recovery or release operation.

Phase 1 remains **NOT ACCEPTED**. Production remains **NO-GO**.
