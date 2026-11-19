# Transferred-Degraded Quarantine Design Gate — 2026-11-16

## Scope and evidence
Read-only design review in the Working Copy. No source code, test code, configuration, Production, or source-project files were changed in this step.

Reconfirmed source path:
- `reaper-managed-worker-v2.c`: `raya_managed_worker_handoff` takes `w->lock`, performs `raya_reaper_integrated_handoff` (which queues the operation, increments queued/outstanding, and changes reservation to HANDED_OFF), changes owner to Reaper, then signals `wake_event`. On signal failure it marks the core DEGRADED and returns `RAYA_MANAGED_HANDOFF_TRANSFERRED_DEGRADED` before releasing `w->lock`.
- `integrated-submit-adapter-v2.c`: `adapter_publish` receives that result, treats every result except NOT_TRANSFERRED as transferred, sets `worker_owns_operation=TRUE`, and returns success to the Submitter. It does not register the envelope in Adapter Retained.
- `managed_worker_main` checks DEGRADED at the top of its loop, but after `WaitForMultipleObjects` returns it resolves and claims the selected event before returning to the top check. A worker already waiting when handoff fails may remain asleep indefinitely if no other handle becomes signaled. If another operation event wakes it, it can enter completion handling; therefore delayed registration after `raya_managed_worker_handoff` returns is not a safe atomicity boundary.
- `retain_envelope` inserts into Adapter Retained, and inserts into Worker Retained only when the Worker does not own the operation or terminal release already happened. Its current use after the handoff result is too late to guarantee race-free registration.
- `worker_finish_once` concerns caller admission accounting; it does not release the queued operation, I/O, reservation, or Submitter issuer lease.

## Confirmed failure state
The test-enabled real named-pipe integration previously reproduced:
- OS accepted overlapped I/O (`ERROR_IO_PENDING`).
- Integrated handoff committed: operation is QUEUED, reservation HANDED_OFF, owner REAPER_QUEUED, queued/outstanding/reserved are 1/1/1.
- Wake signal failed and core became DEGRADED with WAKE_SIGNAL.
- Worker remained blocked until an external test signal.
- Adapter Retained and Worker Retained were both empty; Adapter active_count remained 1.
- Submitter issuer lease remained 1; no ACK or callback occurred.
- Operation buffer, issuing pipe handle, completion event, and OVERLAPPED remained live. The test did not release them.

## Required design contract
1. **Atomic transfer/quarantine publication.** Add a Worker handoff variant or internal hook that executes a small Adapter registration callback while `w->lock` is still held, after queue/ownership transfer is committed and before the wake signal is attempted. On WAKE_SIGNAL failure, the callback must link the envelope into an Adapter-owned in-flight/quarantine registry before the Worker lock is released. Do not add this pending-I/O operation to Worker Retained: Worker/Reaper still owns the queued operation and its completion resources.
2. **Lock order.** The proposed nested order is Worker lock -> Adapter lock for this single publication callback. Audit and enforce that no path nests Adapter lock -> Worker lock. Existing `retain_envelope` takes these locks sequentially, not simultaneously; preserve that property elsewhere. Do not take Submitter lock in this callback. Add a concurrency regression that forces an operation completion event to become signaled around the failed wake and proves there is no use-after-free, double registration, lost envelope, or duplicate callback.
3. **Distinct lifecycle class.** Represent `TRANSFERRED_DEGRADED_PENDING` separately from ordinary Retained states. Track at least: queue ownership committed, wake failure observed, Adapter envelope registered, I/O completion known/unknown, Worker thread state, Submitter ACK state, and callback disposition. Registration is not permission to dequeue, collect, close, free, ACK, replay, or callback.
4. **Independent Worker wake-failure observation.** The current infinite wait can strand the thread after a failed wake. A safe design must guarantee a bounded path to observe DEGRADED without relying on the failed event. Candidate mechanisms require separate evaluation: bounded wait timeout/polling; a separately managed supervisor/secondary signal; or another wait primitive with a documented failure contract. Do not simply retry SetEvent and assume it succeeds. Do not change production wait semantics in this design-only step.
5. **Recovery remains fail-closed.** Until Worker is joined and exact operation/event/pipe/OVERLAPPED identity is reconciled, retain all I/O resources and Submitter lease. Never route this state through NOT_ISSUED cleanup or `raya_pipe_owner_caller_collect`. No generic core reset, lifecycle promotion, queue clearing, or restart.
6. **Exactly-once constraints.** A callback may only be considered after terminal collection, finalizer close/release, owner release, integrated release, and Submitter acknowledgement are independently reconciled. ACKED-with-notification-failure must not be ACKed again. Existing void callback does not promise durable exactly-once external effects.
7. **Observability.** Snapshot must report queue membership and operation state in addition to Adapter/Worker Retained lists. Separate sequential lock snapshots are diagnostic only, not an atomic cross-component snapshot; add generation/version validation or a coordinated snapshot protocol before treating combined observations as a consistency proof.

## Test plan before any production change
- Test-only positive case: force `SetEvent` failure after real queue handoff; assert atomic Adapter quarantine registration before handoff releases Worker lock; assert Worker Retained remains empty and all ownership/resource counters remain held.
- Race case: arrange operation completion immediately before/during failed wake; force Worker to leave wait via operation event; assert either the Worker stops before touching the envelope or it completes through the existing terminal path, with no stale quarantine pointer, double free, or duplicate callback.
- Wake-observation case: no operation event becomes signaled; verify Worker observes DEGRADED within a bounded interval using the chosen independent mechanism.
- Negative controls: omit registration hook, register after unlock, inject duplicate registration, and disable independent wake observation; each must be detected by the test harness.
- Run test-enabled and non-test baseline builds in isolated unique temporary directories only after confirming disk capacity and runner cleanup behavior. Preserve all existing project outputs and evidence.

## Decision
**Design gate: NOT READY FOR PRODUCTION.** The defect is confirmed, but a post-return call to the current `retain_envelope` is not a safe fix because publication is racy. Next work should implement only a test-only handoff publication hook and deterministic race tests in the Working Copy. Production behavior remains unchanged; no build was run in this design step.

## Environment
- Working Copy: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`
- C: 9,959,186,432 bytes free at inspection.
- D: 2,113,348,488 bytes free at inspection.
- No file cleanup, build, TLC run, commit, reset, or Production/source-project modification in this step.