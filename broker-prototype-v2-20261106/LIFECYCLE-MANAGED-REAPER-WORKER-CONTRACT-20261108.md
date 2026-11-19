# Lifecycle-managed Completion Reaper Worker — Implementation Contract
Date: 2026-11-08
Status: Design gate; implementation and runtime acceptance are still required.

## Decision

Do not extend `reaper-worker-v2.c` in place. It is a legacy FIFO worker over `raya_reaper_sync`; its callback returns a boolean and its terminal path immediately releases the core operation. The lifecycle-managed worker must be a separate implementation over `raya_reaper_integrated`, Registry, WaitPlan, the pipe owner primitive, and the resource finalizer.

Do not modify or wire `pipe-io-v2.c`.

## Operation record and ownership

One caller-allocated operation record remains at a stable address from admission until the release callback. It owns:
- embedded `raya_integrated_op` (base queue node and dedicated completion event);
- the exact `OVERLAPPED` passed to the initiating ReadFile/WriteFile;
- the exact issuing pipe HANDLE used for that pending I/O (already duplicated/owned by the operation);
- private I/O buffer and its capacity/length;
- pipe-owner state and finalizer state;
- a release callback/context.

The dedicated event in the registry must be the same event represented by `OVERLAPPED.hEvent` (accounting for the low-bit notification convention where applicable). The issuing HANDLE and OVERLAPPED address are immutable after handoff. Caller code must not access or free the record, buffer, event, or issuing HANDLE after ownership transfers.

The worker does not allocate per-operation threads. Exactly one worker thread owns collection and finalization.

## Shared synchronization

The worker owns one CRITICAL_SECTION protecting:
- integrated core lifecycle, admission leases, queue and outstanding counts;
- reservation state and registry;
- operation collect/owner/finalizer state transitions that are shared with callers;
- wake-event reset and producer SetEvent operations.

Every producer must reserve and hand off while holding this same lock through the integrated APIs. Producers enqueue and signal the manual-reset wake event while holding the lock. Worker wake handling resets the event and inspects queue/lifecycle predicates under that lock, preventing a lost-wake gap.

WaitForMultipleObjects, GetOverlappedResult, CloseHandle on per-operation resources, and release callbacks run outside the lock.

## Worker loop

1. Acquire lock; reset wake event. Reset failure records WAITSET_INVALID and exits degraded without releasing operations.
2. Promote queued operations into Registry until empty. Any promotion/registry mismatch records OWNERSHIP_INVARIANT and exits degraded.
3. If integrated can-exit is true, leave lock and exit normally.
4. Build a WaitPlan under lock (wake + at most 63 operation events, with rotating fairness); leave lock.
5. Wait on that immutable plan outside lock. WAIT_FAILED captures GetLastError immediately, records WAIT_FAILED under lock and exits degraded. Wake restarts the loop. Any other unexpected result is an ownership/waitset failure.
6. For an operation result, reacquire lock; resolve exact operation/event against the current Registry and claim it. A stale/mismatched result records OWNERSHIP_INVARIANT and exits. Release lock.
7. Confirm that exact event is signaled. Call GetOverlappedResult using the operation's exact issuing HANDLE and exact OVERLAPPED pointer, with bWait=FALSE. Classify only:
   - success => terminal success;
   - ERROR_IO_INCOMPLETE => incomplete, retain ownership and return collect state to pending;
   - ERROR_OPERATION_ABORTED => terminal aborted only with signaled exact event and exact issuing pair;
   - every other failure => COMPLETION_UNCERTAIN; retain all resources and do not retry/free.
8. For terminal results, reacquire lock and use integrated terminal-hold. This removes the operation from Registry but leaves outstanding/reserved ownership held. For incomplete, restore pending state and continue. For uncertain or invariant failure, degrade and exit retaining ownership.
9. Release lock. Run ordered finalizer: close the issuing pipe HANDLE, then completion event HANDLE. A failure is sticky CLOSE_UNCERTAIN; record HANDLE_CLOSE under lock, keep outstanding/reservation held, and exit degraded. Never retry an uncertain close.
10. Commit finalizer release only after both closes positively succeeded. Reacquire lock and call integrated release. Only after successful accounting release may the operation release callback run outside the lock to free its record and private buffer. The callback must not be invoked earlier.
11. After terminal release, continue the loop. During shutdown, exit only when caller_active, queue, registry, outstanding, and reservations are all zero.

## API contract for producers

- Admission creates a lease before the caller starts/queues work.
- A caller that abandons before handoff must finish its lease under lock and signal the worker if shutdown is pending.
- A successful handoff transfers ownership exactly once. Wake SetEvent failure does not roll back transfer; it records WAKE_SIGNAL and must retain the queued operation. No unsafe caller fallback is permitted.
- Shutdown transitions ACCEPTING to STOPPING and signals wake under lock. New admissions fail; already admitted callers may finish or hand off.
- A shutdown timeout is incomplete, not success. No forced thread termination or resource cleanup is permitted.

## Fault handling and test gates

The new implementation must have deterministic fault injection for ResetEvent, SetEvent, WaitForMultipleObjects, GetOverlappedResult, each CloseHandle position, release callback, and thread startup. Tests must assert exact degradation bitsets and that uncertain ownership remains counted and unreleasable.

Before any transport integration, complete the native 28-group acceptance matrix, including:
- empty/startup/shutdown and admission races;
- producer concurrency, pre-admitted handoff during STOPPING, wake/reset races;
- 1, 63, 64, 70, and 256 operation fairness/windows;
- wake failure and WAIT_FAILED retention (no implicit polling recovery);
- stale wait-plan/registry and event mismatch;
- incomplete completion and terminal success/abort;
- uncertain GetOverlappedResult failures;
- first and second handle-close failures;
- shutdown with active callers, queued operations, registry operations, and terminal-held operations;
- release callback failure/ordering and worker join/destroy;
- no per-operation thread creation and no resource release before finalizer commit.

## Explicit non-goals

No service provisioning, Production integration, polling recovery, modification of `pipe-io-v2.c`, changes to tunnel configuration, or changes to IranPriceScout. Production remains NO-GO until all acceptance gates are passed and separately reviewed.
