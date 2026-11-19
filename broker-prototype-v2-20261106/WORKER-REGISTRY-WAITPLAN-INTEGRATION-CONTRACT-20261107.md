# Worker + Registry + WaitPlan Integration Contract (2026-11-07)

Status: design/audit addendum only. No Worker integration is implemented by this document.

## Source audit findings

The current components are individually tested but are not yet one ownership system:

1. `reaper-worker-v2.c` calls `raya_reaper_sync_take`, which waits only on the manual-reset wake event and dequeues one FIFO item. It does not build or wait on a Registry Wait Plan.
2. `raya_reaper_sync_handoff` moves an operation into the FIFO core queue and signals the wake event. It does not register an operation event.
3. `raya_reaper_waitplan_build/resolve` operates only on the separate Registry. It has no relationship to the FIFO queue or Core's outstanding counter.
4. `raya_reaper_sync_terminal` releases Core accounting but does not remove a Registry entry. Conversely, Registry removal does not release Core accounting.
5. `raya_reaper_op` currently contains only `next` and `state`; it has no completion-event member. The worker therefore has no authoritative operation-event handle to register.
6. The current collector callback returns a Boolean. It cannot distinguish terminal success, terminal abort, still incomplete, or uncertain API/handle failure.
7. The current WaitPlan is a snapshot of active Registry entries only. The worker must ensure queued operations are promoted to active Registry entries before waiting, and it must not wait indefinitely with queued work omitted.
8. Registry capacity is 256. Admission/handoff must not permit a 257th outstanding operation to become untrackable. Capacity reservation must be enforced before ownership transfer, not after.
9. `raya_reaper_waitplan_resolve` is safe only while the same lock protects Registry membership and event/op lifetime. Single Worker must remain the only remover/free authority.

## Required integrated ownership model

One shared lock protects:
- Core lifecycle and admission counters;
- FIFO queue links and operation states;
- Registry membership and rotating cursor;
- operation/event lifetime and the transition to terminal accounting.

The only allowed path is:

`CALLER_PENDING -> QUEUED -> WORKER_PENDING + REGISTRY_MEMBER -> WORKER_COLLECTING -> (INCOMPLETE | UNCERTAIN | TERMINAL) -> RELEASED`

Rules:
- Handoff must reserve bounded tracking capacity before it transfers ownership. If the 256-entry total outstanding limit is reached, reject handoff while caller still owns the operation.
- The operation record must carry its dedicated completion event, or the handoff contract must transfer an immutable op/event pair as one object. Do not reconstruct the event from a handle value or an unrelated table after handoff.
- Worker drains queued items into Registry under the shared lock. It changes state to worker-owned and registers the exact operation/event pair in one critical section. If registration fails, mark sticky DEGRADED and retain ownership/accounting; never drop the operation.
- Worker builds a Wait Plan under the lock, unlocks, and calls `WaitForMultipleObjects` with the plan's wake + operation handles.
- On a signaled operation slot, Worker reacquires the lock, resolves the exact pair, verifies state and membership, and marks/claims the operation as COLLECTING. It then unlocks before calling any completion API.
- The completion collector returns a four-way result: TERMINAL_SUCCESS, TERMINAL_ABORTED, INCOMPLETE, UNCERTAIN. A Boolean result is insufficient.
- INCOMPLETE: retain Registry membership, resources, outstanding count and worker ownership. Do not remove, release or free. Rebuild/re-probe with bounded scheduling.
- UNCERTAIN: sticky DEGRADED; retain ownership and all resources. Do not claim clean shutdown.
- TERMINAL_SUCCESS/TERMINAL_ABORTED: reacquire the lock; verify the same op/event pair is still registered and claimed by this worker; remove Registry entry and release Core outstanding accounting atomically. Resource close/free happens outside the lock, exactly once, after terminal proof.
- Producers may append queued items while Worker waits. They must not remove/free Registry members or their event handles.
- On Wake Event, reset and inspect queue/lifecycle predicates while holding the shared lock. Drain newly queued operations before rebuilding the wait plan.
- On timeout, rotate/rebuild the plan; queued work must still be promoted and fairness maintained.
- On WAIT_FAILED, capture GetLastError immediately, mark sticky DEGRADED, and retain all active operation ownership. Only separately proven signaled events may proceed to collection.
- Shutdown stops admission, wakes Worker, continues draining admitted callers and all tracked operations, and exits only when caller_active, queued, Registry count and outstanding are all zero. Timeout is incomplete shutdown, not permission to free state.

## API shape for the next implementation

Prefer a new experimental Worker variant rather than silently changing the currently tested FIFO Worker API.

- Add an explicit event-bearing operation envelope or a documented event field to the operation record.
- Replace Boolean collector with an enum-valued nonblocking collector. The collector must use the exact issuing pipe HANDLE and OVERLAPPED pair, and must not infer terminal completion from timeout or cancellation request.
- Add lock-held helper operations for queue-to-registry promotion and terminal Registry/Core release. These helpers must be atomic with respect to the shared lock.
- Keep Win32 waits, completion collection, CloseHandle and memory deallocation outside the lock.
- Preserve the existing FIFO Worker tests as regression tests; label the new integrated Worker as experimental until all required acceptance groups pass.

## Required focused tests before touching pipe-io-v2.c

1. Handoff reserves capacity and rejects overflow without losing caller ownership.
2. Worker promotes one and many queued items into Registry with exact event pairing.
3. Producer append while Worker waits does not invalidate the current plan.
4. Wake reset/enqueue race cannot lose queued work.
5. Completion event maps to exact operation and only one Worker can claim it.
6. INCOMPLETE retains Registry membership, outstanding count, event and operation state.
7. UNCERTAIN marks DEGRADED and retains all resources.
8. Both terminal result classes remove Registry membership and release Core accounting exactly once.
9. Registry removal and Core terminal accounting cannot be observed half-completed.
10. More than 63 active entries are discovered fairly over bounded rotations.
11. WAIT_FAILED retains all unproven entries and does not clean up.
12. Shutdown waits for admitted callers, queue, Registry and outstanding to drain.
13. Registry capacity exhaustion is handled before ownership handoff.
14. Multi-producer append stress under the shared lock; no duplicate op/event pairs or lost wake.
15. Injected event signal/reset failures retain ownership and force degraded/incomplete status.

## Gate

Do not modify `pipe-io-v2.c` or connect the experimental Worker to production transport until the integrated Worker passes its focused tests and the full 28-group named-pipe acceptance matrix. The old Boolean-callback Worker remains a synthetic queue prototype only. Production remains NO-GO.
