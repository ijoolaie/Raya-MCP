# Stage 5.16 Multi-Adapter Fault-Injection Feasibility — 2026-11-18

## Result

**No second independent Adapter-associated retained operation was created in this pass.** This is a source-level feasibility assessment, not a runtime proof.

## Verified facts

1. `raya_integrated_adapter_start_with_apis` accepts caller-supplied Adapter, Submitter, and Worker pointers. The public header describes the Worker as borrowed and requiring it to outlive the Adapter. It does not explicitly state whether multiple Adapters may concurrently borrow one Worker; that sharing/lifecycle contract must be reviewed before using this as the test topology.
2. `raya_managed_worker_admit` serializes admission through the Worker lock and the integrated Worker admission routine.
3. Each Adapter initializes a caller-supplied Submitter. Distinct Adapter instances can therefore be paired with distinct Submitter storage at the API shape level, while both can be supplied the same Worker pointer. This alone does not prove the full shared-Worker topology is lifecycle-safe.
4. The existing ACK fault seam is single-instance:
   - `g_ack_state` is one global pointer.
   - `test_submitter_set_event` dispatches failure through that one pointer.
   - `api_state` holds one Submitter pointer.
   - The Worker close callback is configured once on the Worker and receives one context.
   - The current close callback arms the one ACK fault after its second close.
5. The existing ACK failure test creates one Adapter/Submitter/Worker triple, submits one operation, and observes one Adapter retained envelope. The Submitter then enters DEGRADED. It cannot be used as proof that a second independent submission was accepted and retained.
6. The production test-only snapshot API is per Adapter for Adapter envelopes but copies the Worker retained list associated with that Adapter's Worker. Its quiescence requirements still apply.

## Required test-harness work before a runtime test

- Review and explicitly establish whether two Adapter instances may safely share one Worker for concurrent submission and eventual teardown.
- Replace the single global ACK-state pointer with a bounded test-only dispatcher keyed by the exact Submitter event handles (or another unambiguous per-Submitter discriminator).
- Make Worker close-fault accounting identify which Submitter's ACK should fail without relying on list order or operation pointer values as identity.
- Preserve a single Worker close callback and a documented context lifetime covering both Submitter instances.
- Submit one operation through each independent Adapter/Submitter pair.
- Induce one deterministic ACK failure per Submitter without degrading the other Submitter before its operation has been accepted and completed.
- Join/quiesce all involved components under the existing ownership order.
- Capture Worker inventory through a test snapshot and prove two distinct Adapter identity pairs, two distinct Worker operation identities, and matching Adapter associations.
- Verify each Adapter inventory independently and validate that the two Adapter records correspond to two separate successful submit calls.
- Keep both uncertain retained operations quarantined; do not release/replay them for test cleanup.

## Safety boundary

Do not simply call submit twice on the same Submitter after its first ACK failure. Do not manually construct or link a second envelope/operation. Do not share a Worker across Adapters until the ownership/lifecycle contract is reviewed. Do not add production fault hooks for a test-only scenario.

## Next decision point

First perform a narrow source review of Worker shared-consumer assumptions and Adapter/Submitter shutdown ordering. If shared Worker use is not explicitly safe, use separate Worker instances and redesign the evidence requirement (or add an explicitly supported test fixture) rather than asserting a shared Worker topology.

## Acceptance

- 5.16 runtime multi-submission proof: NOT COMPLETE.
- Single-global fault hook limitation: CONFIRMED.
- Shared Worker across multiple Adapters: API-shaped possibility, lifecycle safety NOT ESTABLISHED.
- No source/test code changed in this feasibility pass.
- Stage 5: BLOCKED / NOT ACCEPTED.
- Phase 1: NOT ACCEPTED.
- Production: NO-GO.

## Addendum - 2026-11-19 runtime evidence superseding the source-only feasibility result

The earlier statement that no second independent Adapter-associated retained operation was created was accurate for that source-review pass but is superseded by the mixed-stage runtime test documented in `STAGE-5-16-TO-5-20-EXECUTION-RESULTS-20261118.md`. Two real Adapter API submissions, from separate Adapter/Submitter pairs sharing one Worker, now produce two retained envelopes and two identity-bearing Worker inventory records. One is B's post-terminal ACK quarantine; the other is A's pre-issue Worker-admission rejection followed by an injected Adapter-local close failure. Separate production Adapter v1 inventories and the shared production Worker v1 inventory capture both records.

The exact dual-ACK scenario proposed in this feasibility document remains unproven: the second operation was not admitted to the Worker or issued to the OS, because the shared Worker had already degraded. Accordingly, this result is additional supported evidence, not acceptance of the stricter dual-ACK test requirement. No production behavior or recovery semantics changed.