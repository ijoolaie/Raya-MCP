# Submitter Independent Degraded-Wake Design Review — 2026-11-16

## Scope
Read-only review of `dedicated-io-submitter-v2.c/.h` and the existing Managed Worker finite-wait work. No source, header, build, configuration, or test files changed. No build or runtime test performed.

## Current wake topology
- Submitter owns two manual-reset events: `wake_event` and `changed_event`; there is one Submitter thread.
- `submitter_main` waits only on `wake_event` with `INFINITE` after resetting it and rechecking queue/exit predicates under the Submitter lock (`dedicated-io-submitter-v2.c`, lines 50–71).
- ACK waiters wait only on `changed_event` with `INFINITE` after reset-and-recheck (`lines 241–267`).
- `degrade_locked` first publishes `RAYA_SUBMITTER_DEGRADED`, then best-effort signals `changed_event` and `wake_event` (`lines 30–34`). It does not have a third channel.
- Enqueue publishes the queue node/counters before signaling `wake_event`; if signaling fails, it marks degraded and returns RETAINED (`lines 205–224`). The Submitter thread may already be asleep on the failed wake event.
- ACK commits `ACKED` and decrements `issuer_leases` before signaling; a signal failure returns 0 after terminal state is committed (`lines 238–251`). Retrying ACK is not valid.
- The Submitter API table provides create/set/reset/single-wait/thread/close operations only; no independent wait primitive or external monitor is currently part of the object.

## Candidate designs

### A. Dedicated sticky degraded event (preferred first prototype)
Add a third manual-reset `degraded_event`, created before the Submitter thread starts and never reset during the object's lifetime. Every transition to DEGRADED publishes lifecycle under lock, then best-effort signals this event. Both the Submitter thread and ACK waiters wait on their normal event plus `degraded_event` using `WaitForMultipleObjects`; degraded-event selection causes them to reacquire the lock, observe DEGRADED, and exit/return without issuing, ACKing, or releasing request ownership.

Advantages:
- Separate kernel event object and separate wait target; a failure specific to the normal wake/changed handle need not prevent degraded observation.
- Sticky manual-reset semantics avoid reset/recheck races for the terminal DEGRADED condition.
- Does not require a second background thread or periodic idle polling.

Limitations:
- It is not an absolute guarantee if `SetEvent` fails for the degraded event too, if the handle is invalid, or if the process/kernel is failing. It is an independent *target*, not an independent signaling primitive.
- Startup, shutdown, destroy, handle-close failure and all failure-injection tables must account for the third handle.
- Wait ordering must give degraded_event precedence if both normal and degraded events are signaled; code must still recheck lifecycle under lock.
- The ACK waiter must distinguish normal changed notification from degraded wake and must never repeat an ACK whose state is already ACKED.

### B. Finite timeout as a fallback
Use a bounded timeout in the Submitter worker and ACK waiters; on timeout reacquire the lock and inspect lifecycle/request state, then rebuild the wait. This is the only currently demonstrated way to observe a sticky state without a successful event signal. It adds periodic wakeups and CPU cost. The Managed Worker prototype used 100 ms only under test macro; its measurements do not approve a Submitter timeout or a production interval.

### C. Helper/watchdog thread
A second thread could monitor an atomic degraded generation and signal a control event. However, the helper itself needs a reliable notification or must poll; it adds lifecycle, join, ownership and failure modes. It does not eliminate the fundamental issue if all signaling mechanisms fail. Not preferred before A is tested.

### D. Reuse `changed_event` as the worker's emergency wake
Not sufficient: `changed_event` is already the ACK notification target and can itself be the failing API call. It is not an independent target, and it does not currently appear in the Submitter worker's wait set.

## Recommended next experiment (test-only)
1. Add a test-only prototype on a temporary source copy first; do not modify project source yet.
2. Add `degraded_event` as a distinct manual-reset object and include it in both worker and ACK-waiter wait sets.
3. Inject failure selectively for `wake_event` while allowing `degraded_event` signaling; assert the sleeping worker observes DEGRADED promptly and does not dequeue/issue the retained request.
4. Inject failure selectively for `changed_event` while allowing `degraded_event`; assert an ACK waiter wakes and returns failure without re-ACK or freeing request storage.
5. Inject failure of both the normal event and `degraded_event`; demonstrate that this design alone cannot guarantee wake. Evaluate finite timeout only as a separately measured fallback.
6. Race normal enqueue/ACK/shutdown against event reset and degraded publication; include simultaneous normal+degraded signals, event creation/close failures, and exact handle accounting.
7. Preserve the existing reset-under-lock plus predicate-recheck pattern unless a separate race proof and tests justify changing it.

## Gate / conclusion
A third sticky event is the narrowest promising prototype because it separates the degraded notification target from both existing events without adding another thread. It is not a hard guarantee against global signaling failure. A timeout fallback remains the only demonstrated signal-independent observation path, but no Submitter timeout value is approved. Do not move event signaling outside the lock, do not change Production code, and do not claim recovery. ACK state and notification state remain separate; retained ownership must remain intact on degraded wake.

## Environment and safety
- Working copy only: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`.
- No source/header/config/build/test file modified.
- No build, test, TLC, commit, reset, or deletion performed.
- Production remains NO-GO; Phase 1 remains NOT ACCEPTED.