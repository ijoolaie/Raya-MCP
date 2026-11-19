# Pipe I/O Cancellation Reaper — Design Contract (2026-11-06)

Status: design only. No implementation or runtime acceptance is implied.

## Scope

Replace the per-operation fallback thread in `pipe-io-v2.c` with one lifecycle-managed reaper. The reaper owns only operations whose cancellation did not drain within the bounded synchronous interval. It does not retry I/O, reuse a connection, or decide whether a timed-out write was delivered.

## Required invariants

1. A `pending_io` allocation, its `OVERLAPPED`, event, duplicated pipe handle, and private buffer remain alive until the kernel has completed the operation and the completion result has been collected.
2. Exactly one owner exists at every point: the calling I/O routine, the reaper queue/worker, or a terminal cleanup path. Ownership must never be inferred from a timeout alone.
3. A caller may return only after either (a) it has collected terminal completion and released the operation, or (b) ownership has been transferred to the initialized reaper under its queue lock.
4. Enqueueing must not allocate memory. Each operation carries an intrusive queue link and an explicit ownership/state field.
5. No path may close the duplicated pipe handle, close the event, free the private buffer, or free the OVERLAPPED while an I/O operation may still reference them.
6. Reaper initialization failure prevents pipe-I/O admission. There is no per-operation thread fallback and no silent leak fallback.
7. The public result remains conservative: timeout/error after a partial or uncertain transfer requires discarding the connection. Reaping is cleanup, not evidence that the peer did or did not receive bytes.
8. Calls on one byte stream remain externally serialized. The reaper does not provide stream-level read/write ordering.

## Operation state machine

States:
- `CALLER_OWNED_PREPARED`: operation resources allocated and initialized; no I/O request has yet been submitted.
- `CALLER_OWNED_PENDING`: ReadFile/WriteFile returned FALSE with immediately captured `ERROR_IO_PENDING`; kernel owns the in-flight I/O while the caller retains lifecycle ownership.
- `CALLER_OWNED_SYNC_COMPLETE`: ReadFile/WriteFile returned TRUE; the I/O is already complete and must not be cancelled or handed to the reaper. The caller must obtain the immediate byte count through the separately validated synchronous-completion output contract.
- `CALLER_OWNED_SUBMISSION_FAILED`: ReadFile/WriteFile returned FALSE with an error other than `ERROR_IO_PENDING`; no pending I/O was submitted and resources may be cleaned up through the terminal submission-failure path.
- `CALLER_OWNED_COLLECTING`: caller has won the ownership lock and observed a signaled event; caller exclusively owns the attempt to collect the completion result.
- `CALLER_OWNED_TERMINAL`: terminal completion result collected; caller may clean up.
- `REAPER_QUEUED`: caller transferred ownership while holding the reaper lock.
- `REAPER_OWNED_PENDING`: worker removed the item from the queue and is waiting for terminal completion.
- `REAPER_OWNED_TERMINAL`: worker collected terminal completion.
- `RELEASED`: resources closed/freed exactly once.

Allowed transitions:
- Prepare resources -> `CALLER_OWNED_PREPARED`.
- ReadFile/WriteFile returns TRUE -> `CALLER_OWNED_SYNC_COMPLETE`; collect the immediate byte count using the validated synchronous-success output path, then `CALLER_OWNED_TERMINAL` -> `RELEASED`. Do not call `CancelIoEx`, enqueue, or assume `GetOverlappedResult` reports this operation.
- ReadFile/WriteFile returns FALSE with `ERROR_IO_PENDING` -> `CALLER_OWNED_PENDING`; normal/cancelled completion -> `CALLER_OWNED_COLLECTING` -> `CALLER_OWNED_TERMINAL` -> `RELEASED`.
- ReadFile/WriteFile returns FALSE with any other immediately captured error -> `CALLER_OWNED_SUBMISSION_FAILED` -> `RELEASED`; no cancellation or reaper handoff is permitted.
- Cancellation requested; bounded drain observes completion -> `CALLER_OWNED_COLLECTING` -> `CALLER_OWNED_TERMINAL` -> `RELEASED`.
- If completion collection unexpectedly reports `ERROR_IO_INCOMPLETE`, remain caller-owned in `CALLER_OWNED_COLLECTING` (or transition to an explicitly defined caller-owned degraded state); do not enqueue implicitly or release resources.
- Cancellation requested; bounded drain expires -> under queue lock transfer `CALLER_OWNED_PENDING` to `REAPER_QUEUED`; caller must not touch or free the operation afterward.
- Worker dequeue -> `REAPER_OWNED_PENDING`; terminal completion -> `REAPER_OWNED_TERMINAL` -> `RELEASED`.

No transition from a reaper-owned state back to caller ownership is permitted. Duplicate enqueue, duplicate dequeue, double close, and double free are invariant violations.

## Queue and worker synchronization

**Worker scheduling constraint:** a single worker must not dequeue one operation and block indefinitely in `GetOverlappedResult(..., TRUE)`; that would prevent it from servicing other queued completions. The worker must multiplex completion events.

- Initialize the lock, wake event, queue head, active-operation list, counters, and worker before marking the subsystem accepting.
- Queue insertion/removal and ownership-state changes occur under the same lock.
- The worker wake event is manual-reset. Producers enqueue and call `SetEvent` while holding the queue lock. After waking, the worker acquires that lock, resets the wake event, and inspects queue/lifecycle predicates before releasing the lock. This prevents a producer from inserting between the worker's empty-queue check and wake-event reset and losing the only notification.
- The worker's wait-handle snapshot is built while holding the lock. Active operation entries remain reaper-owned and cannot be removed/freed by any other thread; after releasing the lock, the worker may safely wait on the snapshot. Rebuild the snapshot after every wake, timeout, rotation, or completion.
- In normal mode, use `WaitForMultipleObjects` with at most 64 handles: one validated wake event plus at most 63 operation events. Keep excess operations in the protected queue until an active slot is available. In sticky polling-only mode, omit the unusable wake event and wait on at most 63 validated operation events; if none exist, sleep for the finite poll interval and re-check predicates under the lock.
- When more than 63 operations are active/queued, rotate the monitored window on a bounded interval (for example 100 ms) so an unsignaled early subset cannot starve later operations. A signaled operation outside the current window remains owned and is discovered when its window is selected.
- On a signaled operation event, the worker obtains exclusive ownership under the lock, removes it from the active set, then calls `GetOverlappedResult(..., FALSE)` outside the lock. Because the event is the operation's completion event, a successful call records the byte count and a failed call records the terminal Win32 error (including `ERROR_OPERATION_ABORTED`); either result is terminal completion, not a reason to wait again. Cleanup may begin only after this collection step. Unexpected API/handle misuse is recorded as an invariant failure, not silently treated as a successful transfer.
- **Completion-result consistency:** a signaled event is a prerequisite to collecting completion, not permission to free the operation. If `GetOverlappedResult(..., FALSE)` fails with `ERROR_IO_INCOMPLETE`, retain ownership and all kernel-visible resources, record an invariant/worker diagnostic, and re-probe through the controlled worker path; do not mark terminal or release. For any other result, distinguish documented terminal I/O errors from API/handle misuse using an operation-specific contract, never a generic error-name allowlist. The current narrow candidate contract permits `ERROR_OPERATION_ABORTED` only after the dedicated event for that exact operation is observed signaled and `GetOverlappedResult` is called with the exact issuing pipe handle and OVERLAPPED pointer. A context-aware pure helper (`pipe-completion-context-v2.c`) tests this decision but cannot verify the event or handle identity itself; the eventual call site must establish both. Disconnect errors remain uncertain until a separate named-pipe-specific contract and live tests establish their exact completion semantics. Only a positively established terminal result permits cleanup. Apply the same rule to zero-time recovery probes after a wait failure.
- **Wait-set integrity:** before each `WaitForMultipleObjects` call, assert that the handle array contains no duplicate handles; in normal mode the validated wake event appears exactly once, while in polling-only mode it appears zero times; every operation event belongs to one distinct currently reaper-owned operation. If this invariant fails, do not wait on an ambiguous set or release any affected operation; enter sticky degraded state and retain ownership/accounting.
- Capture `GetLastError()` immediately when `WaitForMultipleObjects` returns `WAIT_FAILED`, before diagnostics or any other Win32 call can overwrite the thread's last-error value. Store the captured numeric error in the worker diagnostic state.
- The worker must re-check the queue predicate and active set after every wake, timeout, or completion. Spurious wakeups are harmless.
- A completion that races with queue insertion is safe only when the ownership transition is linearized under the same subsystem lock used by the worker:
  1. The caller may wait on the operation event without holding the subsystem lock. A signaled wait does not itself change ownership.
  2. Before deciding to retain or transfer ownership, the caller acquires the subsystem lock and verifies that the operation is still in `CALLER_OWNED_PENDING`. No worker path may inspect or dequeue caller-owned operations.
  3. While still holding the lock, the caller performs a zero-time event probe. If the event is signaled, the caller retains ownership and changes the state to a caller-collecting/terminal-check state (or equivalent explicit state); it then releases the lock and calls `GetOverlappedResult(..., FALSE)`. The worker must not enqueue, dequeue, or free this operation. If completion collection unexpectedly reports `ERROR_IO_INCOMPLETE`, the caller retains ownership and follows the documented degraded/re-probe path; it must not free the operation.
  4. If the zero-time event probe is not signaled, the caller inserts the operation into the intrusive queue and changes ownership to `REAPER_QUEUED` atomically under the lock. It signals the wake event before releasing the lock. After that ownership transition, the caller must not read, write, probe, cancel, close, or free any operation field or resource—not even to perform a post-enqueue diagnostic check. The caller may use only independent stack/local values that were copied before handoff.
  5. The worker may dequeue only after acquiring the same lock and observing `REAPER_QUEUED`; it changes the state to `REAPER_OWNED_PENDING` before releasing the lock. It alone may then inspect the operation event and collect completion. If the event became signaled between the caller's zero-time probe and queue insertion, the persistent event state is observed by the worker after dequeue or in its admission probe.
  6. The caller's wait result and the zero-time probe are advisory observations, not ownership tokens. The ownership-state transition under the lock is the sole linearization point. Exactly one side is permitted to access operation resources after that point.
- Do not call `GetOverlappedResult`, `CancelIoEx`, a blocking wait, `CloseHandle`, or any allocation/free routine while holding the subsystem lock. The sole permitted wait API call in the handoff critical section is `WaitForSingleObject(event, 0)` on that operation's event; it must be nonblocking. Capture its result and (if needed) `GetLastError()` immediately, then make the ownership decision before unlocking.
- Add deterministic barrier tests for both sides of the race: (a) completion is signaled immediately before the caller acquires the handoff lock, so caller retains and collects; (b) completion is signaled immediately after the caller's zero-time probe but before enqueue completes, so caller transfers and worker collects. In both cases assert one collector, one cleanup owner, no caller access after handoff, and exactly-once accounting.
- Signaling the worker must not be the only record of ownership. If signaling fails after insertion, the item remains discoverable in the protected queue; the caller records a sticky worker-wakeup-failure state under the lock and must not return ownership to itself. To ensure the worker eventually observes such a failure even when the wake signal is lost, the worker must use a finite maximum wait interval (for example 100 ms) even when the queue is empty, then re-check state and queue predicates under the lock. The finite interval is a recovery mechanism, not the primary wake path.
- If `WaitForMultipleObjects` fails, do not free any active operation. Record the exact wait error and transition the subsystem to a sticky degraded state. A bounded diagnostic recovery pass may probe each still-owned event with `WaitForSingleObject(event, 0)`; only events positively observed signaled may proceed to `GetOverlappedResult(..., FALSE)`. Unsignaled or invalid/unprobeable events remain owned and accounted for. Do not use a blocking per-operation thread fallback, do not close worker synchronization handles, and do not report successful shutdown while any ownership remains. If recovery cannot restore multiplexed waiting, the subsystem must report incomplete cleanup and remain non-restartable; no fallback may free kernel-visible state.
- The worker must not hold the queue lock while waiting for I/O completion or closing/freeing operation resources.
- The 63-operation window, rotation interval, fairness, wake races, wait-failure behavior, cancellation-result matrix, and polling-only wake-event recovery require deterministic tests before acceptance.

## Rotating wait-window primitive — isolated implementation (2026-11-07)

Files: `reaper-window-v2.h`, `reaper-window-v2.c`, `reaper-window-v2-test.c`, `build-reaper-window-v2-test.cmd`.

The pure helper builds a bounded index window of at most 63 entries over a caller-supplied stable operation array. It normalizes an arbitrary cursor, wraps at the end, returns the next cursor, performs no allocation or Win32 calls, and does not mutate the operation array. Its contract explicitly requires the caller to hold the lock that stabilizes the array and independently validate every selected operation/event before constructing a Win32 wait set.

Coverage includes empty/singleton sets, exact capacity, 64-item rotation, wraparound, 130-item coverage across three rotations, cursor normalization, uniqueness/range checks, and null arguments. MSVC `/W4 /WX` build passed; 14 assertions passed on the initial run and 25/25 additional process runs passed. The helper's own executable/object files were removed.

A follow-up property pass made cursor advancement overflow-safe even for a synthetic `SIZE_MAX` total and exhaustively checked at-least-once coverage for every stable set size from 1 through 256. Each individual window is unique and bounded; successive windows may intentionally overlap (for example, 64 items with a 63-slot window), so the guarantee is coverage, not exactly-once observation across rotations. The expanded suite passed 16 assertions and 25/25 repeated process runs.

## Reaper core accounting primitives — isolated implementation (2026-11-07)

Files: `reaper-core-v2.h`, `reaper-core-v2.c`, `reaper-core-v2-test.c`, `build-reaper-core-v2-test.cmd`.

Implemented a lock-held, allocation-free intrusive FIFO/accounting core:
- lifecycle: UNINITIALIZED -> ACCEPTING -> STOPPING -> STOPPED, with sticky DEGRADED state;
- admission lease acquire/release;
- atomic-in-the-caller's-critical-section handoff accounting (caller_active--, outstanding++, queue append);
- FIFO dequeue and worker ownership transition;
- terminal-accounting release, guarded to worker-owned operations only;
- shutdown exit predicate requiring caller_active, queued, outstanding and queue links all drained;
- pre-admitted callers may complete handoff after STOPPING, while new admission is rejected.

All functions explicitly require the caller to hold one external lock. The core does not create or own that lock, signal a wake event, allocate/free operation records, inspect handles, collect I/O completion, or implement a worker. `raya_reaper_worker_terminal` must only be called after the worker has independently established terminal completion with the exact operation event and issuing HANDLE/OVERLAPPED pair; the primitive itself cannot prove that. The core object must be zero-initialized before its first start call.

MSVC `/W4 /WX` build passed; 33 assertions passed and 25/25 additional process runs passed. Test binaries/objects were removed. The test covers shutdown racing an admitted caller, duplicate enqueue/release rejection, FIFO transfer, and the no-exit-while-outstanding invariant. It is a deterministic state/accounting unit test, not a concurrent Win32 runtime test.

## Win32 wake/queue synchronization wrapper — isolated implementation (2026-11-07)

Files: `reaper-sync-v2.h`, `reaper-sync-v2.c`, `reaper-sync-v2-test.c`, `build-reaper-sync-v2-test.cmd`.

Added a wrapper that owns a `CRITICAL_SECTION`, manual-reset wake event, and the accounting core. All core/queue state transitions occur under the same critical section. Producers append and signal while holding that lock. The consumer waits outside the lock, then locks, resets the manual-reset event, inspects/dequeues the queue, and re-signals before unlock when additional queue entries remain. Shutdown and final-drain transitions also signal the waiter. Initialization uses `InitializeCriticalSectionEx` with checked failure; event creation failure tears down the lock. Destroy is refused unless lifecycle is STOPPED and all accounting/queue fields are drained.

The test exercises two-entry FIFO wake/reset behavior, re-signal after reset, ordinary shutdown wake, pre-admitted caller handoff after STOPPING, final drain notification, and destruction only after STOPPED. MSVC `/W4 /WX` passed; 17 assertions passed and 25/25 additional process runs passed. This is a sequential API-level Win32 event/lock test, not a stress test with competing threads or injected Win32 API failures. Own build artifacts were removed.

Important boundaries:
- There is still no worker thread. A caller must run the take loop and must not call terminal accounting until exact-event/exact-handle completion collection has been independently proven terminal.
- `TAKE_TIMEOUT` also represents a wake with no queued operation and no exit predicate (for example, shutdown while a previously admitted caller has not yet handed off). The caller must continue waiting; it is not evidence of worker exit.
- A failed SetEvent/ResetEvent or wait marks the core degraded and retains outstanding accounting. No attempt is made to free pending operation resources.
- The wrapper requires external lifecycle discipline: no concurrent API entry may race with destroy.

## Rotating Win32 wait-set builder — isolated implementation (2026-11-07)

Files: `reaper-waitset-v2.h`, `reaper-waitset-v2.c`, `reaper-waitset-v2-test.c`, `build-reaper-waitset-v2-test.cmd`.

The builder composes the manual-reset wake event with up to 63 operation events selected by the existing rotating-window primitive. It records source indices for wait-result mapping, rejects NULL/invalid handles, duplicate operation handles within the selected wait set, and an operation event equal to the wake event. The caller must hold the lock that stabilizes the event array and ownership while building the snapshot. The builder does not retain handles or call WaitForMultipleObjects itself.

MSVC `/W4 /WX` passed. 13 assertions passed, including actual creation of 130 distinct Windows events, 64-handle maximum, three rotations covering all 130 operations, duplicate rejection, and a live WaitForMultipleObjects slot-identification check. 25/25 repeated process runs passed; build artifacts were removed.

This is not yet a worker integration or a fairness runtime proof under concurrent mutation. The stable event-array lifetime, active-set admission/removal, wake-event races during rotation, WAIT_FAILED handling, and terminal collection remain unimplemented at the worker layer.

## Single worker-thread shell — synthetic-only implementation (2026-11-07)

Files: `reaper-worker-v2.h`, `reaper-worker-v2.c`, `reaper-worker-v2-test.c`, `build-reaper-worker-v2-test.cmd`.

Added one Windows worker thread that repeatedly consumes the synchronized queue, invokes a caller-supplied collection callback outside the lock, and updates accounting only if that callback reports terminal completion. Shutdown transitions ACCEPTING to STOPPING, wakes the worker, waits with a caller-supplied finite timeout, and can be retried after timeout without attempting the STOPPING transition twice. A nonterminal callback result marks the core DEGRADED; the operation remains worker-owned and outstanding, and clean shutdown/destroy are refused.

Tests use only synthetic callbacks; they do not submit kernel I/O and do not prove any OVERLAPPED completion. MSVC `/W4 /WX` passed. 16 assertions passed, including a 10ms join timeout followed by successful retry after a delayed callback, and retention of accounting after callback failure. 25/25 repeated process runs passed. The test deliberately retains synchronization resources in its degraded failure case; only its terminated thread handle is closed. Own build artifacts were removed.

Critical limitations:
- The callback contract is trusted by this shell. It must not return true unless it has independently collected terminal completion for the exact operation, using the issuing HANDLE and OVERLAPPED, with the context-aware classification rules. The current test callback merely simulates this.
- The worker does not yet monitor operation events. A separate wait-set builder has now been implemented and tested, but it is not yet connected to the worker's active-operation registry.
- No competing-thread stress, API failure injection, cancellation, runtime I/O, or full 28-group acceptance has been completed. The worker shell is not integrated with `pipe-io-v2.c` and is not production-ready.

## Initialization and admission

- Initialization is a one-time state transition: `UNINITIALIZED -> INITIALIZING -> ACCEPTING` or `FAILED`.
- Any failure to create synchronization objects or the worker moves the subsystem to `FAILED`; pipe I/O is rejected before submitting an operation.
- Concurrent initialization calls must be serialized and observe the same terminal initialization result.
- The implementation must define whether repeated initialization is idempotent; it must not create multiple workers accidentally.
- Every admitted `one_io` operation obtains an admission lease under the subsystem lock before allocating/submitting kernel I/O. Admission increments `caller_active`; all normal return paths release that lease exactly once.
- The lease spans submission, synchronous waiting/cancellation, and any ownership handoff. On handoff, increment reaper-owned accounting and decrement `caller_active` atomically under the same lock.
- Shutdown changes state to `STOPPING` under the lock and rejects new leases. It must not conclude the reaper can exit while `caller_active` is nonzero: an already-admitted caller may still need to enqueue an operation.
- The worker exit predicate is: state is `STOPPING`, `caller_active == 0`, queue empty, and reaper-owned outstanding count zero. Check this predicate under the lock.
- If allocation or submission fails after lease acquisition, release the lease through one centralized cleanup path. No early return may leak an admission count.
- Do not hold the subsystem lock during allocation, DuplicateHandle, CreateEvent, ReadFile/WriteFile, waits, or resource cleanup. Use a short lease acquisition and a short state/accounting transition only.

## Shutdown contract

- Shutdown first changes `ACCEPTING -> STOPPING` under the subsystem lock. New operations are rejected before submission.
- Existing caller-owned operations may finish normally; operations already transferred to the queue remain reaper-owned.
- The worker exits only when state is `STOPPING`, `caller_active == 0`, the queue is empty, and the outstanding reaper-owned count is zero; this predicate is checked under the subsystem lock.
- A shutdown deadline expiring returns an explicit incomplete-shutdown result. It must not free queued/in-flight state, close worker synchronization handles, or report successful shutdown.
- After incomplete shutdown, the subsystem remains in a non-restartable stopping/failed state until the worker actually exits and all owned operations are released. Restart semantics require a separate design.
- Successful shutdown joins the worker, verifies zero queued and outstanding operations, then closes synchronization resources.

## Proposed public lifecycle API (contract draft)

The implementation should expose an explicit subsystem lifecycle rather than hiding worker creation inside each transfer. The API must distinguish I/O completion deadlines from cleanup/ownership-transfer budgets:

- `raya_pipe_io_initialize(void)`: initialize synchronization state and start exactly one worker. Return success only after the worker is running and admission is enabled. Repeated calls after success are idempotent; concurrent calls wait for and observe the same initialization result. Calls after a terminal initialization failure return failure without retrying partial initialization.
- `raya_pipe_io_shutdown(DWORD deadline_ms)`: stop admission, wait for admitted callers and reaper-owned operations to drain, then join the worker and release subsystem synchronization handles. The deadline bounds the shutdown wait policy, not forced reclamation. Return a distinct incomplete/degraded result on deadline expiry or worker failure. It must never imply that pending operations were cancelled successfully or that uncertain handle cleanup succeeded.
- Transfer entry points: acquire an admission lease before allocating or submitting I/O. If the subsystem is not accepting, return a distinct unavailable/not-initialized status without touching the pipe. Every exit path releases its lease exactly once, except the atomic accounting transfer that accompanies ownership handoff.
- `deadline_ms` on `raya_pipe_read_exact` and `raya_pipe_write_all` is the **I/O completion budget** measured from entry using a monotonic clock. It controls how long the caller waits for successful transfer before requesting cancellation. It is not a strict wall-clock upper bound on wrapper return.
- `cleanup_wait_ms` is a separately named, bounded **synchronous cancellation-drain budget** applied after cancellation is requested. If terminal completion is collected within this budget, the caller owns and releases the operation. If it expires, the caller must atomically transfer ownership to the initialized reaper and may then return without waiting for kernel completion. This budget must not be silently added to or subtracted from the I/O completion budget.
- The total wrapper return time may include the I/O completion budget, the bounded cleanup-drain budget, setup/copy overhead, and scheduler/API latency. The API must not promise a hard real-time return bound. The implementation must report whether the result is a timeout after a completed cancellation or a timeout after successful ownership handoff; both require connection disposal, but diagnostics differ.
- If the reaper is unavailable or cannot accept ownership, the implementation must not submit new I/O. Since a pending kernel operation cannot be safely abandoned or freed, any post-submission handoff failure is a sticky subsystem-fatal condition: preserve ownership/accounting, reject future admission, and report incomplete cleanup. Never fall back to a per-operation thread or untracked leak path.

Suggested public function shape (names and types are provisional; finalize them in the header before implementation). Initialization, shutdown, and transfer have different result domains and should not share one overloaded enum:

```c
typedef enum raya_pipe_io_init_result {
    RAYA_PIPE_IO_INIT_READY,
    RAYA_PIPE_IO_INIT_FAILED
} raya_pipe_io_init_result;

typedef enum raya_pipe_io_shutdown_result {
    RAYA_PIPE_IO_SHUTDOWN_SUCCESS,
    RAYA_PIPE_IO_SHUTDOWN_INCOMPLETE,
    RAYA_PIPE_IO_SHUTDOWN_DEGRADED,
    RAYA_PIPE_IO_SHUTDOWN_INVALID_STATE
} raya_pipe_io_shutdown_result;

typedef enum raya_pipe_io_status {
    RAYA_PIPE_IO_OK,
    RAYA_PIPE_IO_TIMEOUT_DRAINED,
    RAYA_PIPE_IO_TIMEOUT_HANDED_OFF,
    RAYA_PIPE_IO_DISCONNECTED,
    RAYA_PIPE_IO_UNAVAILABLE,
    RAYA_PIPE_IO_DEGRADED,
    RAYA_PIPE_IO_ERROR,
    RAYA_PIPE_IO_INVALID
} raya_pipe_io_status;

raya_pipe_io_init_result raya_pipe_io_initialize(void);
raya_pipe_io_shutdown_result raya_pipe_io_shutdown(DWORD shutdown_wait_ms);
raya_pipe_io_status raya_pipe_write_all(
    HANDLE pipe, const unsigned char *data, size_t length,
    DWORD io_deadline_ms, DWORD cleanup_wait_ms);
raya_pipe_io_status raya_pipe_read_exact(
    HANDLE pipe, unsigned char *data, size_t length,
    DWORD io_deadline_ms, DWORD cleanup_wait_ms);
```

The names above are semantic placeholders; exact ABI values and any diagnostic-output mechanism must be settled in the header before implementation. `TIMEOUT_HANDED_OFF` means ownership transfer to the reaper was completed under the lock, not that the worker has already collected completion. A failed wake signal after queue insertion does not undo handoff: return the handed-off operation outcome while recording sticky subsystem degradation separately. If ownership transfer itself cannot be completed, do not return `TIMEOUT_HANDED_OFF`; preserve ownership and report the fatal/incomplete condition through the explicitly designed failure path. Status design must never imply whether a timed-out write reached the peer.

Transfer statuses should include `OK`, `TIMEOUT_DRAINED`, `TIMEOUT_HANDED_OFF`, `DISCONNECTED`, `UNAVAILABLE`, `DEGRADED`, `ERROR`, and `INVALID`. A single generic TIMEOUT status may be retained only if an independent diagnostic result reliably exposes whether cleanup drained or was handed off. Shutdown results are `SUCCESS`, `INCOMPLETE`, `DEGRADED`, and `INVALID_STATE`; initialization outcomes are `READY` and `FAILED`. Tests must exercise every public transition.

Required call ordering for a future client/server harness:
1. Initialize Pipe I/O subsystem once before opening/admitting transport sessions.
2. Open byte-type named-pipe handles with overlapped I/O and validate those properties at the transport boundary.
3. Serialize frame-level transfers per connection; invoke exact-read/write wrappers only while the connection is exclusively owned by that caller.
4. On any timeout, partial transfer, uncertain write, or transport error, discard the connection. Do not retry the frame.
5. Stop accepting new sessions and finish/close all connection owners before subsystem shutdown.
6. Require successful shutdown and zero ownership/handle-cleanup diagnostics before reporting a clean transport teardown. An incomplete/degraded shutdown blocks restart in the same process unless a separately reviewed recovery contract is implemented.

## Accounting and diagnostics

Maintain bounded counters under the subsystem lock:
- operations submitted;
- operations completed by caller;
- operations transferred to reaper;
- operations completed/released by reaper;
- current queued count;
- current reaper-owned outstanding count;
- initialization/worker failure count.

At successful shutdown, queued and outstanding counts must both be zero. Counters are diagnostic only and do not replace ownership-state checks. Do not log pipe contents, tokens, or other sensitive data.

## ReadFile/WriteFile submission and completion contract

The implementation must classify the initiating API result immediately, before any other Win32 call can overwrite last-error state. The three outcomes are distinct ownership transitions:

1. **Immediate completion (`ReadFile`/`WriteFile` returns TRUE):** the operation is already complete when the API returns. It must not enter the pending state, be cancelled, or be handed to the reaper. The caller may release kernel-visible operation resources after it has captured the immediate transfer count and finalized its result. Do not assume `GetOverlappedResult` is the byte-count mechanism for this branch: Microsoft's named-pipe overlapped-I/O documentation says that `GetOverlappedResult` reports pending operations and does not report operations completed before the initiating call returned. The immediate byte-count contract therefore needs its own verified path.

   **Named-pipe-specific immediate byte-count rule:** the Microsoft *Named Pipe Server Using Overlapped I/O* example passes a non-NULL byte-count pointer to both `ReadFile` and `WriteFile`. It consumes that count only when the initiating call returns TRUE; when the call returns FALSE with `ERROR_IO_PENDING`, it marks the operation pending and later obtains the count with `GetOverlappedResult`. The same page explicitly states that the byte count is returned for read/write operations that have already completed and that `GetOverlappedResult` reports only operations that were pending. Therefore, for this broker's named-pipe-only transport, adopt that documented split as the implementation candidate: pass a valid `DWORD* transferred_immediate` output pointer, initialize it to zero before submission, and read it only on TRUE. On FALSE, capture `GetLastError()` immediately; if it is `ERROR_IO_PENDING`, do not consume the output pointer and collect the terminal result with `GetOverlappedResult`; for any other error, treat submission as failed. Do not call `GetOverlappedResult` on the TRUE branch and do not inspect undocumented `OVERLAPPED` fields. The generic ReadFile/WriteFile pages still recommend NULL for asynchronous file/device handles and warn that Windows 7 does not permit NULL; the named-pipe sample is the specific documented pattern relevant here, not a general rule for arbitrary overlapped handles. Keep the exact Windows baseline and runtime behavior subject to the dedicated byte-type named-pipe tests below. If the supported target matrix cannot pass those tests, stop and redesign rather than silently switching to a different byte-count mechanism.

2. **Pending (`FALSE`, immediately captured last error is `ERROR_IO_PENDING`):** the request was successfully initiated and kernel-visible resources remain owned until the event indicates completion and `GetOverlappedResult(issuing_handle, &op->ov, &transferred, FALSE)` collects the terminal result, or ownership is atomically transferred to the reaper. A timeout or successful `CancelIoEx` call does not shorten this lifetime. The byte-count output pointer, if non-NULL for the investigated compatibility strategy, must not be consumed in this branch.

3. **Submission failure (`FALSE`, last error is anything other than `ERROR_IO_PENDING`):** the overlapped request was not successfully initiated and no later completion is expected. This is a terminal submission failure, not a pending I/O operation. Record the exact error immediately, classify known disconnect/EOF errors as disconnected, and release the unsubmitted operation resources through the normal exactly-once cleanup path. Do not call cancellation or enqueue it as a pending operation. If deterministic testing ever shows a completion can arrive for this branch, treat that as a contract violation and stop acceptance rather than guessing.

For every branch, initialize the `OVERLAPPED` structure to zero, provide a unique manual-reset event, keep the issuing handle and buffers valid for the documented lifetime, and never reuse the structure while an operation is pending. The pipe handle must be opened for overlapped I/O and use byte-type semantics. Calls on one stream remain externally serialized. The previous unconditional Windows 8+ minimum was tied to the proposed NULL byte-count pointer; remove that minimum until the immediate-success byte-count strategy and supported OS baseline are explicitly decided.

**Completion-port interaction:** the subsystem's completion mechanism is the per-operation manual-reset event plus `GetOverlappedResult` for operations that were pending. It must not also leave an unconsumed I/O completion packet referring to a freed `OVERLAPPED`. Set `OVERLAPPED.hEvent` to the valid event handle with its low-order bit set (retain the unmodified event handle separately for wait/close) so Windows suppresses completion-port packet enqueueing even if the underlying pipe handle has been associated with an I/O completion port. The worker waits on the unmodified event handle. This low-bit suppression is part of the transport contract and must be tested; do not assume that duplicating a handle detaches it from an existing completion-port association.

## Failure policy

- `CancelIoEx` is a cancellation request, not a completion notification. A successful return means cancellation was accepted for matching I/O; it does not mean the kernel has stopped using the `OVERLAPPED`, event, buffer, or duplicated pipe handle. The caller must still wait for and collect the terminal completion result before cleanup, or transfer ownership using the handoff protocol.
- Capture `GetLastError()` immediately when `CancelIoEx` fails. `ERROR_NOT_FOUND` is a completion race or a request that no longer matches a pending operation; it is not proof that the operation is still pending or that it is safe to free. Probe/wait for the operation event and collect the result under the normal ownership protocol.
- Any `CancelIoEx` failure other than `ERROR_NOT_FOUND` means cancellation was not confirmed. It does not establish that the I/O operation has terminated. Record the exact error, continue the bounded drain using the operation event, and if the drain expires transfer the still-owned operation to the reaper. The reaper must continue waiting for actual terminal completion; it must not assume cancellation occurred. If completion never arrives, outstanding ownership remains and shutdown is incomplete.
- The cancellation call must occur only while the caller still owns the operation and outside the subsystem lock. It must target the exact pipe handle value used to submit that operation, together with that operation's exact `OVERLAPPED` address; do not substitute the caller's original handle or another duplicate based on an assumption that cancellation targets are interchangeable. Store the issuing handle in the operation record and keep it valid through terminal completion collection. Once ownership has transitioned to `REAPER_QUEUED`, the caller must not call `CancelIoEx` or access any operation resource. The reaper worker is a completion collector, not a second cancellation issuer.
- A failed queue signal after ownership transfer must not cause caller cleanup. Preserve the queued item, set the sticky worker-wakeup-failure state under the lock, capture the `SetEvent` error immediately, and surface subsystem degradation; the worker's finite polling interval must discover the condition.
- **Wake-event failure recovery:** the worker must distinguish a lost notification from an unusable wake-event handle. If the wake event remains valid, bounded periodic wake timeouts allow the worker to re-check the queue. If a wait fails and diagnostics establish that the wake-event handle itself is invalid/unusable, enter sticky polling-only mode and permanently omit that wake handle from subsequent wait sets. In polling-only mode, wait only on validated operation events for at most the finite poll interval; when there are no operation events, sleep for that bounded interval, then reacquire the lock and inspect queue/lifecycle predicates. Do not pass a known-invalid wake handle back to `WaitForMultipleObjects`. If the failed wait cannot be attributed safely to the wake event versus an operation event, retain all ownership and remain degraded; do not guess which handle is safe.
- In polling-only mode, queue insertion remains serialized under the same lock and remains discoverable by periodic predicate checks. Shutdown and queue progress must not depend on a wake event that has been declared unusable. Polling-only recovery does not clear the sticky diagnostic or qualify as a clean shutdown by itself; clean shutdown still requires all operations and handle cleanup to be accounted for.
- A worker wait/completion failure must not free memory unless terminal kernel completion is established. Keep ownership accounted and report incomplete cleanup.
- A worker that remains blocked indefinitely prevents successful shutdown. Do not terminate it or free its pending operation state as a workaround.
- Resource cleanup errors must be recorded separately from I/O completion. Once terminal I/O completion has been collected, it is safe to release the OVERLAPPED and buffer even if a subsequent `CloseHandle` reports failure; however, record the handle-cleanup failure, increment a leaked/uncertain-handle diagnostic, and do not claim clean shutdown or complete resource reclamation. Each handle has exactly one close attempt by its owner; do not blindly retry a failed close because handle validity may be uncertain.

## Required tests before acceptance

The following 28 test groups are required. Tests must use disposable named-pipe endpoints and deterministic synchronization barriers, not timing-only sleeps where a barrier can be used:
1. Worker initialization success and each initialization failure point.
2. Rejection of I/O after initialization failure.
3. Cancellation completion before the bounded drain expires.
4. Cancellation still pending at drain expiry, followed by queue handoff.
5. Completion racing exactly with queue handoff.
6. Worker wakeup with one and multiple queued operations.
7. Queue signal failure after ownership transfer.
8. Shutdown with empty queue.
9. Shutdown while an operation is queued and pending.
10. Shutdown deadline expiry followed by eventual completion and successful subsequent join.
11. Exactly-once cleanup accounting across all paths.
12. Repeated stress cycles with handle-count baseline comparison and no growth.
13. Caller closes its original pipe handle immediately after wrapper return while reaper still owns the duplicate.
14. Peer disconnect during pending read and pending write.
15. More than 63 simultaneously reaper-owned operations, proving rotating wait-window fairness and bounded discovery of completions outside the current window.
16. Wake-event races while the worker rotates wait windows, including queue insertion at each boundary.
17. WaitForMultipleObjects failure injection, proving active operations remain owned and are never prematurely freed.
18. Shutdown racing with an admitted caller before submission, during pending I/O, and immediately before queue handoff; prove the worker cannot exit while `caller_active` is nonzero.
19. Admission rejection after `STOPPING`, and exactly-once lease release on every allocation/submission/error return path.
20. Wake-signal failure with an empty-queue worker wait, proving finite polling discovers the sticky failure and queued work without premature cleanup.
21. Manual-reset wake-event reset racing with enqueue, proving the worker cannot lose a queue notification between reset and predicate inspection.
22. `WaitForMultipleObjects` failure followed by zero-time event probes, proving only positively signaled operations are collected and all others remain owned.
23. Injected `CloseHandle` failure for event and duplicated pipe handle independently, proving terminal memory is not mistaken for complete handle reclamation and shutdown reports degraded/incomplete cleanup.
24. `CancelIoEx` result matrix: success while completion remains pending, `ERROR_NOT_FOUND` racing with completion, and a non-`ERROR_NOT_FOUND` failure. Prove every case still requires terminal event/result collection, non-confirmed cancellation is handed to the reaper when the drain expires, and the worker never assumes cancellation succeeded.
25. Immediate-success ReadFile/WriteFile submission: validate the immediate byte-count output strategy on every supported Windows version; prove exact byte counts and exactly-once cleanup, and prove this path never calls CancelIoEx or enters the reaper.
26. ERROR_IO_PENDING submission: prove the operation remains kernel-owned until terminal event/result collection, including timeout and cancellation races.
27. Immediate non-pending submission failure: inject representative errors and prove no completion is expected, no cancellation/handoff occurs, and cleanup/accounting is exactly once.
28. Completion-port suppression: associate a disposable pipe handle with an I/O completion port, submit an operation whose OVERLAPPED.hEvent uses the low-order-bit event form, and prove the event/GetOverlappedResult path works without leaving a completion packet referencing the released OVERLAPPED.

The runtime harness must be independently reviewed. If creation of a specific test artifact is blocked by a safety control, stop and report the block; do not recreate it through another tool or path.

## Existing implementation audit (source review; not runtime evidence)

The current `pipe-io-v2.c` was reviewed against this design. The following gaps must be closed during replacement:

1. **Deadline semantics:** `transfer` computes one absolute deadline and checks it after completed chunks, but `one_io` may then wait an additional fixed 2,000 ms while draining cancellation. If drain does not complete, the current per-operation reaper can keep running after the wrapper returns. Therefore `deadline_ms` currently bounds the I/O completion decision, not the wall-clock duration until the wrapper returns. The replacement API must explicitly preserve a bounded cleanup handoff while documenting that the caller's return can include the bounded drain interval and scheduler delay; alternatively, it must use a separate cleanup budget and name/document the two budgets distinctly. Never claim a strict return-time deadline unless the implementation and tests prove it.

2. **Per-operation thread fallback:** the current cancellation path calls `CreateThread` for an individual pending operation. This violates the lifecycle-managed single-worker design and has no admission, queue, shutdown, or global outstanding-operation accounting.

3. **Untracked fallback failure:** if `CreateThread` fails, the current code intentionally leaks the pending operation, event, duplicated pipe handle, and buffer. This avoids freeing kernel-visible memory but does not provide bounded resource accounting, recovery, or a clean shutdown state. The replacement must transfer ownership only to an initialized reaper; otherwise it must fail admission before submitting I/O.

4. **Ignored handle-close results:** `free_pending` ignores the return values of `CloseHandle` for the event and duplicated pipe handle. The current caller also ignores the result of closing the per-operation reaper thread handle. The replacement must record close failures separately from I/O completion, avoid double-close retries, and report degraded/incomplete reclamation where appropriate.

5. **No subsystem lifecycle:** the current source has no global initialization/shutdown state and no admission leases. It cannot prevent new I/O while shutdown is in progress or prove that all caller-owned and reaper-owned operations have drained.

6. **Immediate-success result mismatch — source-level fix applied 2026-11-07:** `pipe-io-v2.c` now passes a non-NULL byte-count output pointer to named-pipe ReadFile/WriteFile calls, consumes it only when the initiating call returns TRUE, and does not call `GetOverlappedResult` on that branch. On FALSE it captures `GetLastError()` immediately; FALSE + `ERROR_IO_PENDING` ignores the direct output and later collects the result with `GetOverlappedResult`. The named-pipe-specific convention passed an isolated harness on Windows 11 Pro build 26300 for immediate ReadFile/WriteFile, pending ReadFile/WriteFile, accepted CancelIoEx cancellation, completion-wins ERROR_NOT_FOUND, invalid-handle CancelIoEx error classification, and immediate non-pending failure across 10 process runs. The modified source compiles under MSVC /W4 /WX. This is not runtime testing of `pipe-io-v2.c`; the cancellation cases cover only an isolated harness, not the implementation's cancellation/drain/reaper behavior. It does not cover non-ERROR_NOT_FOUND CancelIoEx failure against a still-pending operation, cancellation drain expiry, ownership handoff, IOCP association, or cross-version behavior, and does not close the 28-group acceptance gate.

7. **Completion collection can release a nonterminal operation — source-level blocker:** in the normal pending path, `GetOverlappedResult(..., FALSE)` failure jumps to `failed`, which calls `free_pending` for every error. In the cancellation-drain path, a signaled event followed by any failed `GetOverlappedResult(..., FALSE)` also calls `free_pending`. Both paths can release the OVERLAPPED, event, duplicated pipe handle, and buffer without proving terminal completion if the result is `ERROR_IO_INCOMPLETE`; the normal path also does not distinguish documented terminal I/O errors from API/handle misuse. This conflicts with the completion-consistency invariant above. The current source must not be accepted or incrementally treated as safe on these paths; the replacement must retain ownership on `ERROR_IO_INCOMPLETE`, classify terminal I/O errors explicitly, and retain/report uncertain ownership on API misuse. A 2026-11-07 documentation review found that GetOverlappedResult's generic API page documents ERROR_IO_INCOMPLETE for a still-pending operation but does not supply a complete operation-independent terminal-error allowlist. The earlier generic classifier allowlist was therefore withdrawn: the current isolated classifier treats only API success as terminal, ERROR_IO_INCOMPLETE as incomplete, and every other failure as uncertain. Live named-pipe probes verified pending -> ERROR_IO_INCOMPLETE -> event -> successful collection, and native cancellation -> event -> ERROR_OPERATION_ABORTED collection. A wrong-handle probe during a pending operation returned ERROR_IO_INCOMPLETE on the tested Windows host; it did not establish completion. See GETOVERLAPPEDRESULT-LIVE-PROBES-20261107.md. These probes do not exercise pipe-io-v2.c and do not close this blocker.

8. **Per-operation fallback thread also frees after an uncollected result — source-level blocker:** `reap_cancelled_io` calls `GetOverlappedResult(p->pipe, &p->ov, &transferred, TRUE)`, explicitly discards the BOOL result, and unconditionally calls `free_pending(p)`. A failed blocking collection is not proof that the kernel has stopped referencing the operation. This fallback therefore repeats the same ownership defect even when the caller successfully creates the thread. The worker must classify and account for the collection result; only a positively collected terminal result permits operation-memory cleanup. If collection is incomplete or uncertain, the worker must retain ownership and report degraded/incomplete state rather than freeing. This reinforces the decision to remove the per-operation fallback entirely, not merely replace its thread creation mechanism.

9. **Runtime evidence gap:** these findings are static source observations only. They do not establish how Windows behaves under cancellation races, injected API failures, or handle pressure. The 28-group deterministic runtime matrix remains mandatory.

A small source-regression audit was added as `reaper-source-audit-test.cjs`. It checks for the per-operation `CreateThread` fallback, ignored blocking `GetOverlappedResult`, unconditional cleanup after blocking collection, and cleanup reachable near a failed nonblocking collection. On 2026-11-07 it executed against the current `pipe-io-v2.c` and correctly failed all four invariants (0/4 satisfied). This is intentionally a failing guard until the managed reaper replacement lands; it is a source-pattern regression check, not a semantic proof or runtime acceptance test.

Do not treat this audit as acceptance. It is a migration checklist for the managed-reaper implementation.

## Active-operation registry primitive (2026-11-07)

Files: `reaper-registry-v2.h/.c`, `reaper-registry-v2-test.c`, and `build-reaper-registry-v2-test.cmd`.

A bounded, allocation-free registry primitive was added for up to 256 reaper-owned operation records. Each entry pairs an operation pointer with its unique event handle. Add rejects null/invalid handles, duplicate operation pointers, duplicate event handles, and capacity overflow. Remove requires the exact operation pointer and compacts the array while preserving operation/event pairing. Snapshot copies a compact, validated view into caller-provided arrays and reports the current cursor.

All calls require the same external lock that serializes operation ownership and event lifetime. The registry does not own or close handles, does not prove completion, and does not itself connect to the Worker or Wait Set. The caller must register before publishing a handoff and remove only after terminal completion has been collected and ownership is still exclusive. Snapshot arrays and the Wait Set must be built while holding the lock; after releasing it, the worker must retain ownership of every referenced operation/event until the wait result has been processed. The current registry is a primitive only, not evidence of an integrated active-operation worker.

Validation on 2026-11-07: MSVC `/W4 /WX` build passed; 12/12 assertions passed; 25/25 repeated process runs passed. Test handles are synthetic non-null values and are not passed to Win32. This validates registry bookkeeping only, not real event lifetime or concurrent behavior.

## Registry-to-WaitSet planning adapter (2026-11-07)

Files: `reaper-waitplan-v2.h/.c`, `reaper-waitplan-v2-test.c`, and `build-reaper-waitplan-v2-test.cmd`.

The adapter composes the bounded registry snapshot with the existing rotating Wait Set. It retains the complete operation-pointer snapshot (capacity 256), constructs at most 63 operation slots plus the wake event, advances the registry cursor, and resolves a signaled WaitForMultipleObjects slot back to the exact operation/event pair using the original registry index. Resolution revalidates that the exact pair is still present; a removed/replaced pair is reported STALE, not collected. WAIT_TIMEOUT and WAIT_FAILED remain distinct non-operation outcomes.

Build and resolve require the shared registry/ownership lock. The intended worker sequence is: lock -> build plan -> unlock -> wait -> lock -> resolve/revalidate -> claim the still-owned operation -> unlock -> collect completion. Producers may append under the same lock while the worker waits; the plan's copied pointers/handles remain valid only because worker-owned entries cannot be removed or freed by producers. A single worker must be the sole remover. Any future concurrent remover or shutdown cleanup path must preserve this lifetime contract or replace the adapter.

Validation on 2026-11-07: MSVC `/W4 /WX` build passed; 14/14 assertions passed using 70 real manual-reset event handles; actual WaitForMultipleObjects detected events in two successive overlapping windows; exact mapping of rotated index 63 passed; stale-result rejection passed; 25/25 repeated process runs passed. This is a wait-planning adapter test, not a multi-thread stress test, not a completion-collection test, and not integrated into the existing Worker Thread.

## Acceptance gate

This document is not implementation evidence. Pipe I/O remains compile-only until the managed reaper is implemented, the above tests run successfully, resource accounting is verified, and the transport integration gate is separately passed. Broker Production status remains NO-GO.

## Pipe I/O ownership primitive — test-only implementation (2026-11-08)

Files: `pipe-io-owner-v2.h/.c`, deterministic and native tests, and their build scripts. The primitive models caller pending/uncertain ownership, explicit queue handoff, reaper pending/uncertain ownership, terminal collection, and one release commit. It uses the existing context-aware completion classifier: terminal success is accepted only with a signaled dedicated event and exact issuing HANDLE/OVERLAPPED context; ERROR_OPERATION_ABORTED is accepted only in that same narrow context; ERROR_IO_INCOMPLETE and all other errors retain ownership. Duplicate transitions and caller access after handoff are rejected.

The native named-pipe harness exercised three audited sites: (1) injected ERROR_IO_INCOMPLETE after a signaled event in the normal collection path, (2) injected uncertain result in the synchronous cancellation-drain path followed by real ERROR_OPERATION_ABORTED collection, and (3) injected failure in the former blocking per-operation reaper collection followed by a later real successful collection. MSVC x64 `/W4 /WX`: 51 assertions x25 deterministic runs and 400 assertions x10 native runs, all passed (5,275 total assertions).

This primitive is not thread-safe; every transition must be protected by the future lifecycle Reaper lock. It does not own/allocate the intrusive queue, registry, wake event, worker, or lifecycle, and it does not close/free resources or model partial HANDLE-close failure. The native harness injects collection outcomes at the ownership boundary and does not prove that each injected API failure is natively inducible. Crucially, `pipe-io-v2.c` is unchanged and is not wired to this helper: its unsafe free paths and per-operation CreateThread fallback remain blockers. Do not integrate until the lifecycle-managed worker, resource finalizer/close accounting, and all 28 acceptance groups are implemented and passed. Production remains NO-GO.

## Resource finalizer primitive � isolated test implementation (2026-11-08)

Files: `pipe-io-finalizer-v2.h/.c`, `pipe-io-finalizer-v2-test.c`, and `build-pipe-io-finalizer-v2-test.cmd`.

The finalizer models ordered closure of the operation-owned duplicated issuing pipe HANDLE and dedicated completion-event HANDLE. It must only be initialized after exact terminal I/O collection. Each successful close is recorded independently; a failed close enters sticky CLOSE_UNCERTAIN, retains the failed HANDLE value, prohibits retry and release, and requires the integrating lifecycle to record `RAYA_RECOVERY_CAUSE_HANDLE_CLOSE` and refuse clean shutdown. The operation record/OVERLAPPED/private buffer may be freed only after both HANDLEs are positively closed and one release commit is recorded. The callback is injectable for tests and must execute outside the Reaper lock.

MSVC x64 `/W4 /WX`: 30 assertions per process, 25/25 runs passed (750 assertions). Synthetic HANDLE values were used; the test did not close real OS handles. The primitive is not thread-safe, is not connected to the Worker, does not free memory, and is not wired into `pipe-io-v2.c`. The 28-group native acceptance matrix remains mandatory. Production remains NO-GO.

## Two-phase terminal/release accounting (2026-11-08)

A source audit found that the legacy core terminal transition immediately decremented outstanding ownership and marked an operation RELEASED before the new post-collection HANDLE finalizer could run. Added a distinct TERMINAL_HELD state and staged APIs: `raya_reaper_worker_terminal_hold` / `raya_reaper_integrated_terminal_hold` preserve outstanding and reservation accounting after terminal I/O collection; `raya_reaper_worker_release` / `raya_reaper_integrated_release` perform the final decrement only after successful resource closure. A sticky HANDLE_CLOSE cause blocks release and clean exit. The legacy immediate-release APIs remain for compatibility tests only and must not be used by the future resource-owning Worker.

The required runtime sequence is: exact terminal collection -> terminal hold under lock -> close pipe/event outside lock -> record HANDLE_CLOSE and retain everything on failure, or commit finalizer then release accounting under lock on success. See `REAPER-TWO-PHASE-RELEASE-20261108.md`.

MSVC x64 `/W4 /WX`: new two-phase test 43 assertions/run x25 (1,075 total), all passed. Regression checks: Core 34; Integrated 25; concurrency 541 (256/256); live named pipe 545 (70 success + 1 aborted); Worker 16; Sync 17. The new path is not yet wired into the real worker; legacy harnesses continue to exercise compatibility paths. Full 28-group acceptance remains outstanding. Production remains NO-GO.
