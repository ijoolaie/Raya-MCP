# Recovery Test Coverage / Feasibility Review — 2026-11-18

## Scope
Audited current recovery-related test cases and the production API surface to determine which recovery behaviors can be tested safely today. Reviewed the existing retained quarantine regression and snapshot state-domain/owner-finalizer validator reports. No source, header, test, build script, README, or configuration changed. No build/test rerun because no production recovery action/API exists and the latest focused regression evidence is already recorded.

## Existing tests that are executable today

- Adapter API negative integration: OS-accepted READ followed by handoff/ownership uncertainty; retained Adapter/Worker ownership; reservation/Submitter lease preservation; callback suppression; Adapter Destroy refusal.
- Finalizer close-failure negative integration: CLOSE_UNCERTAIN, retained inventories/accounting, callback suppression, teardown refusal.
- Submitter ACK failure after Worker release: retained envelope after finalizer/owner/integrated release; no callback/free; component teardown blocked.
- Worker wake failure after transfer: test-seam-only scenario for transferred but untracked operation.
- Retained snapshot validator: list cycle/count, identity membership, operation/collection enum domains, owner/finalizer commit bounds, reservation/registry cross-plane consistency, and negative synthetic malformed snapshots.
- UNCERTAIN completion tests: registry/outstanding/reservation retention, no callback, degraded Worker, and no clean Join/Destroy.
- Terminal-held/close-failure tests: terminal collection, handle-close substeps, callback suppression and retained accounting.

Recorded evidence: the Adapter API negative integration suite passed 10 × 751 assertions (7,510 total); test-only seam and snapshot validator runs have separate recorded passes in the existing reports. These are prior recorded executions, not rerun in this review.

## Recovery behaviors that cannot be tested as successful operations yet

No production API currently supplies:
- Stable per-entry retained identity and owner epoch.
- Production enumeration of Adapter and Worker retained namespaces.
- Immutable per-entry first-retain cause/effect history.
- Owner-only evidence-based reconciliation transition.
- Safe release/unlink/free for a proven retained class.
- Authoritative recipient status/receipt or idempotency contract for accepted/uncertain WRITE.

Consequently, a positive recovery test would have to invent an unsupported action, manipulate internal structures, or free/close resources without proof. Such a test would encode unsafe behavior and is not authorized. Test-only snapshot diagnostics cannot substitute for production recovery.

## Required future test plan (blocked on contract/API)

1. Identity allocation: uniqueness across owner epochs, no reuse, overflow/exhaustion fail-closed, concurrent allocation.
2. First-retain immutability: repeated retain with different later degradation cause does not overwrite first cause/stage.
3. Inventory topology: Adapter-only, Worker-associated, and direct Worker-only records; explicit absent cross-links; counts not conflated.
4. Snapshot semantics: copied bounded snapshot, pagination/truncation, quiescence/lease lifetime, cycle/count corruption, version consistency, concurrent mutation behavior.
5. Reconciliation authorization: wrong owner/epoch, stale record version, duplicate action ID, wrong inventory kind, and conflicting action rejected without mutation.
6. Narrow pre-I/O cleanup: prove NOT_ISSUED plus withdrawn Submitter request before any handle/resource release; inject every close/commit/unreserve failure.
7. Post-Issue handoff uncertainty: preserve operation and prove no ReadFile/WriteFile resubmission.
8. Finalizer uncertainty: no blind CloseHandle retry; preserve ambiguous substep.
9. Post-terminal ACK failure: preserve envelope/callback bookkeeping and prove no I/O reissue or duplicate callback.
10. Direct managed-pipe retention: reconcile only within Worker namespace; no assumed Adapter envelope.
11. Orchestrator integration: only after all component tests above; cursor persists partial effects and refuses teardown until all inventories are independently verified empty.

Every positive release test must include a negative counterpart for stale identity/version, wrong owner, unknown effect, and cross-plane inconsistency.

## Decision

- Existing fail-closed negative tests are meaningful and should remain.
- No new successful recovery test can be added safely before stable identity, read-only enumeration, and evidence-specific reconciliation APIs exist.
- Do not add test-only helpers that manually unlink retained nodes, close ambiguous handles, decrement counters, free envelopes, or replay accepted I/O.
- No new build was run; prior negative regression artifacts were already reported at zero remaining. No cleanup was necessary.

## Conclusion
Stage 5.13 recovery-test feasibility review is complete. Current tests validate quarantine and refusal, not recovery. Positive recovery testing is blocked by missing production identity, enumeration, and reconciliation contracts; this is a capability gap, not a test failure.

No source changes, no build artifacts created, no cleanup performed, no Commit/Reset. Stage 5.13 review complete (coverage/feasibility only).
