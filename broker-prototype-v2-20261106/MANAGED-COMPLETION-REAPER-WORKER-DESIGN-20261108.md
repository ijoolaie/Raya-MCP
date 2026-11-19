# Managed Completion Reaper Worker — implementation design (2026-11-08)

Status: design gate for a new experimental worker variant. No worker source, pipe transport, service, or production behavior is changed by this document.

## Decision

Do not extend `reaper-worker-v2.c`. It is a FIFO/synthetic-callback worker and its Boolean collector plus immediate terminal release cannot represent native I/O lifetime. Implement a separate `reaper-managed-worker-v2.*` experimental module that owns one worker thread and composes the integrated core, Registry, WaitPlan, owner primitive, completion classifier, and two-phase finalizer.

The existing FIFO worker and its tests remain untouched as regression-only artifacts.

## Operation envelope

One caller-allocated operation record must remain at a stable address from successful admission through final release. It contains:

- `raya_integrated_op integrated` as the first member (required for current base-to-container casts);
- the exact duplicated issuing pipe HANDLE used in the pending ReadFile/WriteFile;
- the exact OVERLAPPED object used for that operation;
- its dedicated manual-reset completion event, identical to `OVERLAPPED.hEvent` and `integrated.completion_event`;
- private I/O buffer and length/direction metadata;
- `raya_pipe_owner` and `raya_pipe_finalizer` state;
- an explicit worker pin/phase field for assertions and teardown.

The event and pipe HANDLE values are transferred as owned resources. No component may reconstruct, reopen, substitute, or close them before terminal collection. The caller must not touch/free the envelope after handoff. Capacity reservation (256) must happen before ownership transfer.

## One synchronization domain

The managed worker owns one CRITICAL_SECTION protecting all of:

- integrated Core lifecycle, admission leases, FIFO links, outstanding count and reserved slots;
- Registry entries and rotating cursor;
- operation phase/claim/pin and owner-state transitions;
- wake-event predicate transitions and shutdown predicates.

All integrated/core/registry/wait-plan helpers that are documented lock-held are called only under this lock. Producers may reserve and hand off under this same lock. Producers signal the manual-reset wake event while still holding the lock after queue mutation. No producer may remove Registry entries or free handed-off operation records.

## Worker loop

1. Lock; promote queued operations into Registry using `raya_reaper_integrated_promote_one` until queue empty. Each promotion atomically pairs the exact op/event and moves Core state to WORKER_PENDING. A promotion invariant failure is sticky DEGRADED; retain every resource and outstanding count.
2. While locked, build a WaitPlan from the Registry and wake event. This snapshot pins all referenced operation records and event handles because only this worker can remove/free them.
3. Unlock; call `WaitForMultipleObjects` on the plan's wake + up to 63 operation events. Never hold the Reaper lock across a wait.
4. Capture `GetLastError()` immediately on WAIT_FAILED, before any other Win32 call. Mark WAIT_FAILED under lock; retain all entries. No polling recovery is enabled.
5. On wake: lock, ResetEvent, inspect lifecycle/queue predicates, unlock, and loop to promote. Reset failure is WAITSET_INVALID and blocks clean exit.
6. On operation slot: lock and resolve the exact op/event against the current Registry. Reject stale/mismatched results. Claim it with `raya_reaper_integrated_claim`, pin the record, then unlock.
7. Outside lock, call `GetOverlappedResult(exact_issuing_pipe, exact_overlapped, ..., FALSE)`. The wait result establishes that this operation's dedicated event was observed signaled; it does not alone establish completion. Classify API success as terminal success, ERROR_IO_INCOMPLETE as incomplete, and every other error as uncertain. ERROR_OPERATION_ABORTED is terminal-aborted only if the classifier's exact-pair and signaled-event requirements are satisfied.
8. Lock; call `raya_reaper_integrated_terminal_hold` with the four-way result. INCOMPLETE returns the op to COLLECT_PENDING and retains Registry, handles, buffer and outstanding ownership. UNCERTAIN sets COMPLETION_UNCERTAIN and retains everything. Terminal results remove the Registry member and transition Core to TERMINAL_HELD without decrementing outstanding/reservation. The operation record remains pinned by the outstanding count.
9. For terminal results only, unlock and close the duplicated issuing pipe HANDLE then completion-event HANDLE via the finalizer. No CloseHandle or callback executes under the Reaper lock.
10. If either close fails, lock and mark HANDLE_CLOSE; retain the operation record, failed handle value, outstanding count and reservation. The finalizer is sticky; do not retry a failed close and do not claim clean shutdown.
11. If both closes succeed, commit finalizer release outside lock. Then lock and call `raya_reaper_integrated_release`. Only after that succeeds may the worker drop its pin and free the operation envelope, OVERLAPPED and private buffer exactly once. A failed accounting release is OWNERSHIP_INVARIANT and the envelope must be retained.
12. Shutdown changes ACCEPTING to STOPPING under lock and signals wake. The worker exits only when caller_active, queue, Registry, outstanding and reserved_slots are all zero and no operation is pinned/in a finalizer phase. A timed-out join reports incomplete shutdown; it never authorizes cleanup.

## Important integration constraints

- `raya_reaper_integrated_terminal_hold` removes Registry membership before finalizer execution. This is safe only because the operation remains outstanding/TERMINAL_HELD and the single worker retains its stable record pin. No Registry snapshot may be used as a substitute for this pin.
- `raya_reaper_integrated_release` currently checks HANDLE_CLOSE and leaves outstanding/reservation held on failure. The managed worker must record that cause before any attempted release.
- Existing `raya_reaper_integrated_admit/reserve/handoff` are lock-held primitives and do not signal a wake event. The new wrapper must implement admission leases and signal wake atomically with queue handoff under the shared lock.
- Existing `raya_reaper_waitplan_build` mutates the Registry cursor. It must be called under the shared lock; its rotating 63-event windows must remain fair across all 256 entries.
- WaitPlan stale resolution, event reset, wake reset, SetEvent failure, WAIT_FAILED, and operation completion are distinct transitions with distinct cause classification.
- No per-operation thread, no blocking GetOverlappedResult(TRUE), no fire-and-forget cancellation, no path-based fallback, and no automatic retry after uncertain completion/close.
- Do not wire this worker into `pipe-io-v2.c` until it passes the full 28-group native named-pipe matrix.

## Required tests before transport integration

1. Single operation: terminal success; exact pipe/OVERLAPPED pair; both handles closed; release once.
2. Single operation: terminal abort with signaled exact pair.
3. Signaled event + ERROR_IO_INCOMPLETE: Registry/outstanding/handles/buffer retained; next collection remains possible.
4. Signaled event + arbitrary GOR error: COMPLETION_UNCERTAIN; no close/free.
5. Close issuing pipe fails; HANDLE_CLOSE sticky; event and record retained; shutdown cannot be clean.
6. Pipe close succeeds, event close fails; partial close tracked; no retry/free/release.
7. Duplicate event/operation registration rejected before ownership transfer.
8. Capacity 256 accepted, 257th rejected with caller ownership unchanged.
9. Four concurrent producers with worker wait/promotion; no lost wake or duplicate handoff.
10. Producer enqueue/reset race with manual-reset wake event.
11. Wake SetEvent failure retains queued operations and reservations.
12. ResetEvent failure retains all ownership and blocks shutdown.
13. WAIT_FAILED retains every operation; no polling fallback.
14. 64+ operations across rotating WaitPlan windows; prove all 256 can be observed fairly.
15. Stale WaitPlan result after legal Registry mutation rejected without dereference/free.
16. Shutdown while a caller admission lease is held; pre-admitted handoff accepted, new admission rejected.
17. Shutdown while operation is incomplete/uncertain/terminal-held/finalizing.
18. Cancellation request success/failure does not imply terminal completion.
19. Worker thread creation failure does not leak initialized synchronization primitives or misreport STOPPED.
20. Worker join timeout leaves all worker state intact; later join may succeed only after true drain.
21. Injected CloseHandle failures for both close positions.
22. Allocation/initialization failure before handoff leaves caller ownership intact.
23. Exact one-time free instrumentation for record, OVERLAPPED and buffer.
24. Worker unexpected exit records a sticky cause and prevents destruction with outstanding work.
25. All Win32 waits, GOR, CloseHandle and allocator callbacks are proven outside the shared lock.
26. Registry/core/reservation accounting remains consistent on every injected failure.
27. Concurrent shutdown and producer admission/handoff stress.
28. Full actual named-pipe acceptance matrix against this managed worker, not only a parallel model.

## Gate

This document is not evidence that the worker exists or passes tests. The next implementation step is the standalone managed-worker source plus focused native tests. Until all 28 groups pass, `pipe-io-v2.c` remains untouched and compile-only, no service is provisioned, no Production integration is allowed, and Production remains NO-GO.
