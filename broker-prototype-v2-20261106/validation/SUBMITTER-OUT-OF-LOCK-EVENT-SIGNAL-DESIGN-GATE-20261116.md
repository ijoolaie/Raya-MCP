# Submitter out-of-lock Event signaling design gate — 2026-11-16

## Evidence reviewed

- `dedicated-io-submitter-v2.c/.h`, including all `set_event`/`reset_event` call sites and the `raya_submitter_win32_api` injection seam.
- Submitter creates both `wake_event` and `changed_event` as manual-reset events (`create_event(NULL, TRUE, FALSE, NULL)`).
- `submitter_main` resets `wake_event` while holding the Submitter lock, then rechecks `queue_count` and `exit_ready_locked` before unlocking and waiting.
- `raya_submitter_acknowledge` resets `changed_event` while holding the lock, then rechecks request state before unlocking and waiting.
- Earlier test-only measurement (14 producers, 70 operations, five runs per wait mode) found p95 Event API time per lock interval 29.0–29.8 μs versus p95 total Submitter hold 30.8–31.3 μs; p95 non-API remainder 1.1–1.2 μs. Wall-clock maxima include scheduler preemption and are not API CPU time.

## Lost-wakeup analysis

The current reset-and-recheck structure provides a viable basis for signaling after unlock, provided the state predicate and event reset remain serialized by the same lock:

1. If a producer publishes a queued request before the worker acquires the lock, the worker sees nonzero `queue_count` and does not enter the empty-queue reset/wait path.
2. If the worker holds the lock, observes an empty queue, resets `wake_event`, rechecks empty, and unlocks before a producer enqueues, the producer's post-unlock SetEvent wakes it.
3. If a request is consumed before its delayed SetEvent occurs, the later signal is merely spurious; the worker rechecks the queue predicate under lock.
4. For `changed_event`, the ACK waiter resets and rechecks `request->state` under lock. A publisher that has already changed the state is observed before the waiter waits; a publisher changing it after the waiter unlocks signals the event. A delayed signal after the waiter observes the terminal state is spurious, not a lost state transition.

This is a source-level argument for the normal success path, not a complete concurrency proof for all failure interleavings.

## Blocking issue: signal failure and degraded-state visibility

Moving signaling outside the lock creates a new interval after state publication but before notification. If `SetEvent(wake_event)` fails in that interval, the producer must reacquire the lock to mark DEGRADED. But the Submitter thread may already be asleep in `WaitForSingleObject(wake_event, INFINITE)`. Marking DEGRADED does not wake that thread. Retrying the same failed signal is best-effort, not a guarantee. The existing `degrade_locked` currently attempts both events while holding the lock, but it also cannot guarantee wakeup if the wake event operation continues to fail.

`changed_event` has a separate semantic constraint: ACK may already be committed (`REQ_ACKED`, issuer lease decremented) when notification fails. The caller must retain the request and must not repeat ACK; notification repair is a separate operation. Moving SetEvent out of the lock must preserve this commit/notification split.

## Design constraints before any implementation

- Keep all predicate/state transitions, queue mutations, request-state changes, and event reset plus predicate recheck under the Submitter lock.
- A future out-of-lock signal design must publish a generation/notification intent under the lock, capture the exact event handle, unlock, signal, then reconcile a failed signal under the lock without rolling back committed state.
- It must define how the Submitter thread observes a failed wake signal while it may be in an INFINITE wait. A generation counter alone cannot wake a thread blocked on an event. A bounded wait, independent failure-notification channel, or another guaranteed wake mechanism would be required; none is approved here.
- It must preserve ACKED state and issuer-lease accounting if `changed_event` notification fails; never re-ACK.
- Custom `set_event`/`reset_event` callbacks need an explicit blocking/reentrancy contract. Current API validation checks non-null pointers but does not state that callbacks are nonblocking or non-reentrant.
- Add deterministic race tests for producer-vs-reset, ACK-vs-reset, shutdown-vs-reset, event failure after state publication, delayed signal, duplicate signal, and concurrent failure reconciliation. Include a deliberately blocking callback and a reentrant callback negative control if those behaviors are permitted by the API contract.

## Decision

**Do not move Event API calls outside the Submitter lock yet.** The normal-path reset/recheck protocol appears adaptable, and the measurement shows the Event API interval dominates the p95 lock hold in this workload. However, the wake-failure case is not safely solved while the worker uses an INFINITE wait. First specify and prove an independent degraded-state wake mechanism and the ACK notification repair contract; then prototype in a derived test copy and run the existing failure suites. No production or working-copy source change was made for this design review.

## Scope and cleanup

Read-only design review only. No source/header/config/build change, no build/test, no TLC, no file deletion, no commit/reset, and no Production/source-project change.