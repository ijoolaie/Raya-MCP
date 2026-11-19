# Worker Retained Identity — Early Failure Classification (2026-11-18)

## Scope
Working Copy only. Inspected the exact initialization, admission, and retained-publication paths in:
- `reaper-managed-worker-v2.h/.c`
- `managed-pipe-io-v2.c`
- `integrated-submit-adapter-v2.c`
- `integrated-submit-adapter-v2-api-test.c`

## Confirmed classifications

### A. Direct managed-pipe path
1. Allocation/admission occurs before event and duplicate-handle setup.
2. If event creation fails, the operation is freed and the admission lease is finished: no retained record.
3. If DuplicateHandle fails and closing the created event also fails, the operation is retained before `raya_managed_operation_init`; it contains manually populated event/release fields but is not a successfully initialized operation.
4. If `raya_managed_operation_init` fails, it first zeroes the operation, then returns failure. The caller manually reconstructs some handle/release/finalizer fields and retains it. This is also not a successfully initialized operation.
5. Only after successful `raya_managed_operation_init` does the path set direction and proceed to reserve/issue.

### B. Adapter path
1. Adapter instance/operation IDs are allocated before event creation.
2. If DuplicateHandle fails and event close fails, `retain_envelope` retains the envelope before operation initialization. The embedded operation is partial.
3. If `raya_managed_operation_init` fails, it zeroes the embedded operation; the caller reconstructs partial fields and calls `retain_envelope`.
4. After successful initialization, direction is set, then Worker admission, Submitter admission, Worker reservation and enqueue proceed. Later retention paths therefore refer to initialized operations.
5. `retain_envelope` has two distinct effects: Adapter-envelope retention, and conditional Worker retained-list publication. The condition currently publishes when the Worker does not own the operation or terminal release has happened. This means pre-init/partial envelopes can be linked into the Worker retained list under current semantics.
6. The test-only transferred-degraded handoff hook publishes Adapter quarantine while Worker lock is held; it is a separate publication mode and must not be conflated with Worker retained-list membership.

## Identity design implications
- Do not infer "initialized operation" merely from an envelope being present in either retained list.
- Do not assign Worker operation IDs before `raya_managed_operation_init`: its `memset` clears all fields.
- A Worker identity registration step must happen after successful operation initialization and before any operation can be retained through later failure paths.
- Pre-init/initialization-failure records need an explicit identity state such as NOT_INITIALIZED / NO_WORKER_OPERATION_ID. They may remain in Adapter quarantine; whether they remain in the Worker retained list is an existing behavior requiring a separate compatibility decision, not an automatic consequence of adding IDs.
- Worker retained-list snapshots must represent partial records explicitly and must not fabricate stable IDs for them.
- Existing test-only `operation_id` and Worker list `operation_id` are pointer diagnostics. Existing validator/tests compare them for membership. Preserve this as a clearly named address diagnostic while introducing value-based IDs; do not silently repurpose it.
- Adapter operation identity and Worker operation identity are separate namespaces. Adapter-associated Worker records should carry an explicit optional association pair; direct Worker operations have no Adapter association.
- Worker epoch allocation, sequence exhaustion, duplicate publication prevention, and first-retain cause/stage all need deterministic tests.

## Locking constraints
- The test-only degraded handoff invokes the quarantine callback while holding the Worker lock; callback then takes Adapter lock. Existing documented nested order is Worker -> Adapter.
- Normal `retain_envelope` takes Adapter lock, releases it, then takes Worker lock. Do not introduce nested Adapter -> Worker locking.
- Any shared Worker retained-publication helper called from Adapter code must respect the Worker -> Adapter lock order and avoid callbacks while holding unexpected locks.

## Current status
- Source/test/build files changed: none.
- Builds/tests in this pass: none; baseline focused suites passed in the preceding regression pass.
- New report created and read back.
- Identity implementation remains NOT STARTED.
- Stage 5 implementation/recovery acceptance remains BLOCKED / NOT ACCEPTED.
- Phase 1 remains NOT ACCEPTED; Production remains NO-GO.

## Next safe steps
1. Freeze explicit operation identity state: uninitialized/partial vs registered/initialized.
2. Add Worker epoch + per-Worker monotonic ID registration after successful operation init.
3. Decide and document compatibility semantics for partial objects currently present in Worker retained list; retain their existing quarantine safety without assigning false operation identity.
4. Add a Worker-lock-only retained-list publication helper with duplicate detection, first-retain metadata, and separate handling for identity-less partial records.
5. Extend snapshots additively: stable Worker identity fields plus explicit identity-valid state; keep address diagnostics under renamed fields.
6. Update validator to distinguish Adapter envelope membership, Worker retained-list membership, and Worker operation identity; preserve existing cross-plane checks.
7. Add deterministic tests for direct and Adapter initialized paths, both partial-init failure paths, degraded-handoff test seam, duplicate publication, first-retain immutability, and ID exhaustion.
