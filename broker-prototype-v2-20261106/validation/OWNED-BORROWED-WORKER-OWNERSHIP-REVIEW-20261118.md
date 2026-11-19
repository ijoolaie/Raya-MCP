# OWNED / BORROWED Worker Ownership Review — 2026-11-18

## Scope
Read-only audit of Orchestrator Context registration, Registry strict close policy, cleanup-owner APIs, Worker/Adapter/Submitter ownership contracts, checked-in Orchestrator tests, and the latest real-I/O / OWNED-BORROWED validation reports. No source, header, test, build script, README, or configuration changed. Existing test evidence was reviewed; no build/test rerun was performed.

## Ownership contract

- Orchestrator Context stores non-owning pointers to Adapter, Submitter, and Worker. Context storage is caller-owned, must remain stable after registration, and must outlive Registry reclamation.
- Adapter initializes/uses Submitter but borrows Worker. Adapter Destroy closes Adapter admission and destroys only Adapter state; it does not stop/join/destroy Submitter or Worker.
- Orchestrator always owns the Adapter and Submitter cleanup sequence in this composition. Worker ownership is immutable Context metadata: BORROWED or OWNED.
- Context API leases protect Context/component-pointer access. BeginClose blocks new acquisitions; one cleanup owner is elected; outstanding leases must drain before owner-only Context retrieval and cleanup.
- Cleanup owner must serialize lifecycle work and retain exclusive ownership until all Context use ends. Registry locks are not held while calling component APIs.

## Strict cleanup policy

Common required steps for both ownership modes:
0. Adapter Destroy
1. Submitter admission close
2. Submitter Shutdown
3. Submitter Join
4. Submitter Destroy

OWNED Worker additionally requires:
5. Worker Shutdown
6. Worker Join
7. Worker Destroy

BORROWED Worker requires only steps 0–4. Steps 5–7 remain NOT_ATTEMPTED / not required by the immutable policy; Orchestrator Worker teardown APIs reject BORROWED before calling any Worker lifecycle API. This does not claim Worker shutdown/join/destroy success. External owner retains responsibility for Worker storage, users, and teardown.

Strict Registry close is required for Orchestrator Context. Generic Registry close and generic-policy completion cannot bypass the Context-specific required mask. Reclamation requires the exact owner, drained leases, matching object kind, and all required steps COMMITTED + SUCCESS. OWNED abnormal Join or degraded/failed Worker teardown blocks Destroy/reclamation.

## OWNED path evidence

- Orchestrator Worker Shutdown is gated on successful common steps 0–4.
- Worker Join is gated on successful Worker Shutdown.
- Worker Destroy is gated on successful Worker Shutdown and Join.
- A Join that reaps the thread handle but observes abnormal exit is recorded as COMMITTED + ABNORMAL_EXIT, not success; it does not satisfy the required OWNED teardown chain.
- Worker Destroy is delegated to the component API, which requires joined state and STOPPED/empty/zero-retained inventories. The Orchestrator does not bypass component safety checks.
- Checked-in test_real_io_orchestrator_owned_lifecycle exercises real named-pipe asynchronous READ and WRITE through Adapter/Submitter/Worker, validates payload/callback and drained Adapter/Worker/Submitter inventories, then executes all eight cleanup steps and strict Context reclamation.
- Latest report records 10 consecutive Orchestrator test runs × 216 assertions = 2,160 passed, 0 failed. The separate owned lifecycle E2E report records four runs × 216 assertions = 864 passed, 0 failed. These are historical recorded results, not rerun in this audit.

## BORROWED path evidence

- Orchestrator Worker Shutdown/Join/Destroy entrypoints reject BORROWED ownership before invoking Worker APIs.
- Checked-in test_borrowed_worker_lifecycle_boundary runs common Adapter/Submitter cleanup and reclaims Context without Worker teardown.
- After Context reclamation, test verifies external Worker remains initialized, accepting, and usable via an admission/finish pair; the external lifecycle owner then performs Worker Shutdown, normal Join, and Destroy.
- Latest combined Orchestrator test report includes this scenario in the 10 × 216 passing runs. Earlier dedicated E2E report records the same boundary.
- BORROWED test is lifecycle-boundary validation, not a second real pipe I/O transfer. Real READ/WRITE are covered in the OWNED scenario.

## Cross-plane and lifetime checks

- Adapter retained/active inventory must be empty before Adapter Destroy can commit. Therefore a live Adapter envelope/callback cannot remain reachable through the Adapter's own retained inventory after successful Adapter Destroy.
- Submitter admission closes and Submitter thread is shut down/joined/destroyed before Context reclamation. Successful teardown requires queue, producers, active issue, and issuer leases to drain.
- For BORROWED Worker, Context reclamation does not imply Worker termination or global Worker quiescence. The inspected Worker and operation release callback do not retain an Orchestrator Context pointer; the callback context is the Adapter envelope. External Worker users/storage lifetime remains the external owner's responsibility.
- If future callbacks capture Context or future Worker/Adapter operations retain a Context pointer, the BORROWED boundary must be revisited.
- No arbitrary raw-pointer lifetime gate protects late external callers after the external owner frees Worker storage. External owner must quiesce its own users before freeing it.

## Gaps / limitations

- Registry cursor is in-memory, not durable; crash recovery of cleanup ownership/cursor state is not established.
- These tests establish focused real-I/O and ownership lifecycle paths, not the production entrypoint's complete caller chain or product-wide acceptance.
- Retained/uncertain failure paths remain fail-closed and unresolved; OWNED/BORROWED correctness does not add retained reconciliation/release/replay.
- No full product deployment, durable WAL, recipient receipt/idempotency, or full Raft implementation is implied.

## Conclusion
The current ownership boundary is coherent and tested: Orchestrator owns Adapter/Submitter cleanup in both modes; it additionally tears down Worker only in OWNED mode. BORROWED mode correctly reclaims its own Context after common cleanup while leaving Worker alive for its external owner. No ownership-policy defect was established in this review. External raw-pointer quiescence and non-durable cleanup cursor remain explicit constraints.

No source changes, no build artifacts created, no cleanup performed, no Commit/Reset. Stage 5.10 review complete (inspection only).
