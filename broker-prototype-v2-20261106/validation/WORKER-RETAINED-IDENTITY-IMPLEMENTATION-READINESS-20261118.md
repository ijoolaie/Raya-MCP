# Worker Retained Identity Implementation Readiness — 2026-11-18

## Outcome

The implementation sequence was revalidated against the current Working Copy. The contract is implementable, but the complete code-and-test slice was not applied in this pass. No production source, header, test, build script, README, model, or build output was changed.

## Additional verified constraints

1. Worker operation initialization is Worker-agnostic and clears the entire operation with `memset`; identity cannot safely be added before that initializer and expected to survive it.
2. Direct managed-pipe submission calls Worker admission before creating the event and duplicating the pipe. Event creation failure is a clean rejection; duplicate-handle plus event-close failure can retain an operation before operation initialization and before reservation.
3. Adapter submission allocates its Adapter operation ID before event creation. A duplicate-handle failure combined with event-close failure can retain the envelope before operation initialization and before Worker admission.
4. Adapter submission can also retain after Worker admission fails if ownership/finalizer cleanup becomes uncertain. Such a retained Adapter envelope can be added to the Worker retained list even though it has no Worker admission lease.
5. The Worker retained list has two direct-path insertion sites in `managed-pipe-io-v2.c`, and an Adapter-associated insertion site in `integrated-submit-adapter-v2.c`. These must all converge on one duplicate-safe helper.
6. The test-only degraded-handoff hook publishes Adapter quarantine while holding the Worker lock, but that path currently publishes Adapter membership rather than Worker retained-list membership. Its identity registration and diagnostic expectations must remain distinct from actual Worker retained-list membership.
7. Existing API tests intentionally compare pointer diagnostics for cross-plane membership. Replacing those fields outright would silently change test semantics; additive by-value identity fields and a separate pointer-diagnostic field are required, followed by explicit validator/test updates.
8. Existing native test build scripts clean their declared outputs automatically. Any new build must verify the exact script cleanup list and confirm it does not delete pre-existing unrelated artifacts.

## Safe implementation order

A coordinated implementation must be performed as one source/test change set:

1. Add a process-local Worker epoch allocator with checked nonzero allocation; assign it only after Worker initialization has established a valid lock and before the Worker becomes externally usable.
2. Add Worker operation sequence state and operation metadata fields.
3. Add a registration API protected by the Worker lock. It must be callable even for an initialized Worker whose admission is closed, because pre-admission Adapter cleanup failures can still become retained diagnostics. It must not create an admission lease.
4. In the direct path, register after Worker admission and before event creation; ensure all failure exits after registration either free the unissued operation or retain it with its identity.
5. In the Adapter path, register after Adapter operation ID allocation and before event creation, because pre-admission close failures can retain. The Worker identity record must be marked ADAPTER_ASSOCIATED and carry the exact Adapter instance/operation pair.
6. Add a Worker retained-publication helper that validates prior registration, assigns first-retain cause/stage once, rejects duplicate list insertion, links the operation and increments the count atomically under the Worker lock.
7. Convert both direct insertion sites and the Adapter Worker-list insertion to that helper. Keep Adapter envelope retention separate from Worker operation retention.
8. Preserve the test-only degraded-handoff hook semantics; do not force a Worker retained-list insertion when current behavior intentionally has only Adapter quarantine membership.
9. Extend test-only snapshots additively with copied Worker epoch, operation ID, inventory kind, Adapter association validity/pair, and first-retain cause/stage. Retain pointer diagnostics under explicit address-only names.
10. Update the retained snapshot validator and add focused deterministic tests for direct operations, Adapter association, multiple Workers, multiple operations, pre-admission retention, repeated publication, first-retain immutability, invalid association combinations, and allocator exhaustion.
11. Check C:/D: free space and exact output absence, run the focused API test script, inspect exit code and assertions, verify cleanup, then run the direct managed-pipe native test script if the focused suite passes.
12. Record the exact changed files, test runs, assertion counts, and cleanup results.

## Why code was not changed here

The change crosses two submission pipelines, three Worker-list insertion sites, an Adapter-only quarantine path, and a test validator that currently treats pointer identity as a cross-plane membership key. A partial field/API change would make some retained cases appear identified while leaving other early-retain cases unidentified or would conflate Adapter-only quarantine with Worker inventory. The required focused test set is not present yet. Applying only a subset would create false confidence in recovery inventory coverage.

## Status

- Contract freeze: COMPLETE.
- Source-path and failure-order audit: COMPLETE.
- Implementation: NOT STARTED.
- Focused identity tests: NOT STARTED.
- Build/test validation: NOT RUN.
- Stage 5 implementation/recovery acceptance: BLOCKED / NOT ACCEPTED.
- Phase 1: NOT ACCEPTED.
- Production: NO-GO.
