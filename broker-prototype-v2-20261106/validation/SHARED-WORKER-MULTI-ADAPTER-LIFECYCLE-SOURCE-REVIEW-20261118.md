# Shared Worker / Multiple Adapter Lifecycle Source Review — 2026-11-18

## Result

**A single Worker shared by multiple Adapter/Submitter pairs is supported by the inspected admission and ownership structure at source level, subject to serialized lifecycle teardown.** This is a source review conclusion, not yet a runtime integration-test result.

## Evidence

1. `raya_integrated_adapter_start_with_apis` stores the supplied Worker pointer and starts a Submitter. It does not install or replace the Worker's close callback or lifecycle API.
2. Each Adapter stores its own Submitter pointer and has its own Adapter lock, admission state, instance ID, operation ID sequence, and retained-envelope list.
3. Each Adapter-associated operation registers its Worker identity against the same Worker instance, with its own Adapter instance/operation identity pair.
4. All adapters call `raya_managed_worker_admit`; that function serializes admission through the one Worker critical section and the shared integrated Worker admission state.
5. Worker admission/reservation/outstanding/registry/retained state is owned by the Worker, not by an individual Adapter. The inspected code contains no single-Adapter owner field or one-Adapter-only admission guard.
6. The Worker close callback and its context are Worker-wide, configured on the Worker. They are not Adapter-specific. A shared-worker test therefore needs one stable Worker callback/context capable of routing test faults by exact handle or operation metadata, and must not replace the callback while work is active.
7. Adapter Destroy closes only that Adapter's admission and checks its active/retained state. Its public contract explicitly says it does not stop either component. It does not stop the shared Worker.
8. Submitter lifecycle is per Submitter. Each Submitter must be shut down, joined, and destroyed separately after its Adapter's admission is closed and active Adapter calls have drained.
9. The Worker must be shut down, joined, and destroyed exactly once, only after every Adapter has stopped admitting, every associated Submitter has completed shutdown/join, and Worker caller/outstanding/registry/retained state permits Worker teardown.
10. Existing ownership contract says Worker is borrowed by Adapter and must outlive Adapter. Thus Worker storage remains valid through destruction of both Adapters.

## Required shared topology for Stage 5.16 test

- One initialized Worker.
- Adapter A + Submitter A, with independent Adapter API context and Submitter API context.
- Adapter B + Submitter B, with independent Adapter API context and Submitter API context.
- Both adapters borrow the same Worker.
- Worker-level close callback/context is installed once before either Adapter starts and remains stable until Worker teardown.
- A test-only dispatcher routes Submitter ACK faults by exact event handle to the intended Submitter context. A Worker close fault is routed by exact handle/operation identity; no ordering assumptions.
- Submit one operation through Adapter A and one through Adapter B.
- Cause each independent operation to reach the real Adapter-associated retained path. Verify two Adapter envelopes in separate Adapter snapshots and two Worker records in the shared Worker inventory with distinct Worker identities and exact matching Adapter identity pairs.
- After quiescence, destroy in a serialized order: close Adapter A and B admission; shutdown/join/destroy Submitter A and B individually; destroy Adapter A and B individually while Worker remains alive; then shutdown/join/destroy the Worker once if its strict lifecycle preconditions permit. If retained resources prevent teardown, preserve them; do not fake release.

## Test harness changes still required

- Replace the single `g_ack_state` pointer with a bounded registry keyed by the exact Submitter wake/changed event handles.
- Replace single Submitter pointer assumptions in `api_state` and close callback routing with explicit per-instance contexts.
- Use one Worker close callback with a shared, stable dispatcher context.
- Ensure the fault is armed for one selected operation/Submitter and does not degrade the other Submitter before its independent operation has been admitted and issued.
- Ensure both submissions are real public Adapter submit calls; no manual envelope construction or retained-list insertion.
- Ensure the test validates both Adapter snapshots and one shared Worker snapshot without assuming list order.
- Exercise teardown serialization and preserve retained resources.

## Limits of this conclusion

- No runtime test with two Adapters sharing one Worker was run in this review.
- No claim is made that arbitrary concurrent Adapter Destroy, Submitter Destroy, or Worker Destroy calls are safe; they must be serialized.
- This review does not establish production enumeration/reconciliation APIs.
- No source or test code was changed during this source-review pass.

## Acceptance

- Shared Worker topology: SUPPORTED BY INSPECTED ADMISSION/OWNERSHIP STRUCTURE, runtime proof pending.
- Stage 5.16: NOT COMPLETE.
- Stage 5.20: draft contract remains unapproved and production APIs absent.
- Stage 5 implementation/recovery acceptance: BLOCKED / NOT ACCEPTED.
- Phase 1: NOT ACCEPTED.
- Production: NO-GO.
