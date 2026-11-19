# Reaper cause-callsite review and Pipe I/O lifetime audit — 2026-11-08

## Generic degradation callsite review

A recursive source scan of C/H files found the remaining calls to
`raya_reaper_mark_degraded()` in:
- `reaper-core-v2.c`: implementation of the legacy API; deliberately maps
  unspecified failures to OWNERSHIP_INVARIANT.
- `reaper-core-v2.h`: public declaration.
- `reaper-core-v2-test.c`: verifies sticky degraded behavior and that a
  stopped lifecycle cannot be degraded.
- `reaper-core-cause-tracking-test.c`: verifies the legacy fail-closed
  default maps to OWNERSHIP_INVARIANT.
- `reaper-integrated-fault-injection-test.c`: two cases had represented
  known causes (WAIT_FAILED and WAKE_SIGNAL) with the generic default. Those
  calls were replaced with cause-aware transitions and exact bitset checks.

No remaining generic callsite in a production-path source file was found.
The remaining test uses are intentional tests of the conservative default.

## HANDLE_CLOSE cause coverage

The core cause-tracking test now starts a fresh active core, records
HANDLE_CLOSE, verifies the lifecycle becomes DEGRADED, verifies the exact
cause bit, verifies recovery policy remains BLOCKED, and verifies the core
cannot be marked STOPPED. This tests classification only. It does not add a
new close transition or relax STOPPED-only destruction preconditions.
A live HANDLE_CLOSE transition remains unimplemented until teardown ownership
and partial-destruction semantics have a separately reviewed design.

## Pipe I/O lifetime blockers confirmed by source review

The existing `pipe-io-v2.c` remains compile-only and must not be integrated
with the Reaper. The following source paths can release kernel-visible
resources without positively proving that the overlapped operation is
terminal:

1. Normal completion collection, around lines 84-86: after the event is
   signaled, `GetOverlappedResult(..., FALSE)` can fail with
   `ERROR_IO_INCOMPLETE` or another uncertain API/handle error. Control
   reaches `failed:`, which frees the pending object and its OVERLAPPED,
   event, duplicated pipe handle, and private buffer.
2. Cancellation drain, around lines 100-112: after the event is signaled,
   a failed `GetOverlappedResult(..., FALSE)` is followed by unconditional
   `free_pending`, including when the result is ERROR_IO_INCOMPLETE or
   otherwise uncertain.
3. Per-operation cancellation thread, around lines 28-34: the blocking
   `GetOverlappedResult(..., TRUE)` result is discarded, and the thread
   unconditionally frees the pending object. A failed wait/collection does
   not prove that the kernel has stopped referencing the OVERLAPPED or buffer.

For lifetime decisions, the conservative classifier is:
- API success: terminal completion positively collected.
- ERROR_IO_INCOMPLETE: operation remains pending; retain all resources.
- Any other failure: uncertain; retain all resources unless a separately
  specified and tested API contract proves terminality for that exact handle
  and OVERLAPPED.

A signaled event alone is not completion proof. Cancellation request success
or failure is not completion proof either. The exact issuing HANDLE and
OVERLAPPED must remain valid until terminal collection is established.

## Required redesign before implementation

- Replace the per-operation blocking thread with a single lifecycle-managed
  completion reaper that owns pending operation records.
- Model explicit ownership states for caller, registry/queue, worker, and
  terminally collected operation. Every transition must be serialized and
  tested, including shutdown admission races.
- Keep operation resources (OVERLAPPED, event, duplicated pipe handle,
  private buffer, and registry entry) alive on incomplete or uncertain
  collection. Uncertain state must be sticky and must block clean shutdown.
- A reaper worker must multiplex bounded completion-event wait sets, preserve
  ownership across WAIT_FAILED and wake failures, and use only verified
  terminal collection to reclaim an operation.
- Define a safe process-lifetime policy for irrecoverably uncertain operations;
  intentional retention is preferable to use-after-free, but must be surfaced
  as incomplete shutdown rather than reported as successful cleanup.
- Add deterministic fault injection for ERROR_IO_INCOMPLETE after a signaled
  event, arbitrary GetOverlappedResult failures, cancellation races, failed
  blocking collection, handle closure, and shutdown while operations remain
  outstanding. Tests must assert resource ownership and no premature free.
- Run the full 28-group acceptance matrix against the actual Pipe I/O
  implementation, not merely a model or harness that duplicates its logic.

## Verification performed in this step

- Core cause tracking: MSVC x64 /W4 /WX; 27 assertions, 0 failures.
- Integrated fault injection: MSVC x64 /W4 /WX; 56 assertions per run,
  25/25 runs passed (1,400 assertions).
- The two known-cause fault injections now assert exact WAIT_FAILED and
  WAKE_SIGNAL bitsets.
- HANDLE_CLOSE classification is covered without changing teardown behavior.
- Generated EXE/OBJ artifacts from the focused core test were removed.
- No service, production, tunnel, or IranPriceScout changes were made.
- No recovery promotion was enabled. Production remains NO-GO.
