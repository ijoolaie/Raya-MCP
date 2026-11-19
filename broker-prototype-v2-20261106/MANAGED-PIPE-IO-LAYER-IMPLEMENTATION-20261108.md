# Managed Pipe I/O Layer — 2026-11-08

## Scope

A new prototype-only asynchronous byte-pipe I/O submission layer was added in
`managed-pipe-io-v2.h/.c`. It is layered over the lifecycle-managed Reaper
Worker and deliberately does not modify the legacy `pipe-io-v2.c`.

No Windows service, protected root, production broker, tunnel, or
IranPriceScout process/configuration was changed.

## API contract

- `raya_managed_pipe_submit` is the normal entry point.
- `raya_managed_pipe_submit_with_cancel` exposes an injected cancellation
  callback for deterministic tests; production callers should use the normal
  entry point.
- Input must be a connected, overlapped, byte-mode pipe handle.
- Length must be 1..1 MiB. Infinite caller/drain waits are rejected.
- The layer acquires an admission lease before allocating OS resources,
  duplicates the pipe handle, creates a dedicated manual-reset event, allocates
  a private operation record and private I/O buffer, then reserves the exact
  operation before submitting ReadFile/WriteFile. A failed reservation closes
  unissued resources and finishes the admission lease.
- Write input is copied before submission. Read data is never written into the
  caller's buffer.
- Completion callback may execute inline on the submitting thread or later on
  the Reaper Worker thread. The callback context must remain valid until it
  runs. Read data is valid only during the callback; callers must copy it if
  they need to retain it.
- **Critical issuer-thread lifetime constraint:** native testing confirmed that
  if a thread exits after handing off a still-pending overlapped operation, the
  operation completes with `ERROR_OPERATION_ABORTED`. The issuing thread must
  therefore remain alive until terminal completion. A producer thread must
  not simply return after `HANDED_OFF`. This makes the current wrapper unsuitable
  for arbitrary short-lived thread-pool callers; a dedicated long-lived I/O
  submission thread or equivalent architecture is required before production.
- A terminal local result closes the duplicated pipe/event, commits owner
  release, unreserves the exact operation, finishes the admission lease, then
  invokes the callback.
- If the caller wait expires, CancelIoEx is attempted and a bounded drain is
  performed. A still-pending or uncertain operation is handed to the Worker.
  The Worker owns the private buffer and operation record until exact terminal
  collection and resource finalization.
- A non-pending submission failure is treated as terminal because the OS did
  not accept an overlapped operation; it follows the same local finalization
  path and reports the submission error in the callback.
- A close/finalizer failure enters a retained list owned by the Worker object,
  marks a sticky HANDLE_CLOSE cause, suppresses the callback, and prevents
  Worker destruction. This is an intentional safety-over-liveness outcome.

## Ownership changes

- Added caller-local terminal state and operation-bound unreserve:
  `raya_reaper_integrated_caller_terminal` followed by
  `raya_reaper_integrated_unreserve`. Unreserve is rejected until the exact
  operation has reached caller-terminal state.
- Added explicit owner transitions for immediate completion, submission
  failure, and caller-side retry after an uncertain observation.
- Managed Worker operation records now carry completion error, direction, and a
  separate retained-list link. Worker destruction refuses while retained
  operations exist.

## Native verification

MSVC x64, `/W4 /WX`:

- Managed Pipe I/O native harness: 88 assertions per process × 10 = 880,
  passed. Covers local read, local write, immediate submission failure,
  bounded cancellation/abort, deterministic ERROR_NOT_FOUND cancellation
  race and Worker handoff, callback data lifetime, and close-failure retention.
- Caller-local unreserve: 31 assertions × 25 = 775, passed.
- Pipe owner transitions: 69 assertions × 25 = 1,725, passed.
- Existing managed Worker regression harnesses passed after the struct/API
  changes: single-op, 70-op multi-window, exact abort, close-failure retention,
  pipe-close failure, CancelIoEx ERROR_NOT_FOUND race, concurrent producers,
  and thread-start failure.
- Integrated Reaper regression passed: two-phase release, 256-operation
  concurrency, live named-pipe collection, reset/enqueue race, and concurrent
  shutdown/admission.

## Remaining gates / limitations

This is not production-ready and does not change the broker's NO-GO status.

- The complete 28-group Managed Worker acceptance matrix remains open.
- CreateEvent/DuplicateHandle/InitializeCriticalSection failure injection is
  not complete for this wrapper; only thread-start failure is covered in the
  Worker module.
- No OS handle-count baseline or long-duration leak/stress run was added.
- The deterministic 70-producer shutdown/handoff race passed, but randomized
  high-contention scheduling, peer-disconnect, and full timeout/handoff stress
  matrices remain open.
- Issuer-thread exit is a confirmed blocker: pending overlapped I/O consistently
  completes with `ERROR_OPERATION_ABORTED` if the issuing thread exits after
  handoff. See `MANAGED-PIPE-IO-CONCURRENCY-STRESS-20261108.md`.
- The injected cancel callback is a test seam, not a production policy API.
- No IOCP association or event-low-bit suppression policy is implemented.
- No public operation-cancellation token or result-polling handle is exposed.
- Read result data is callback-scoped, not returned synchronously.
- WAIT_FAILED/wake recovery remains disabled; degraded states retain ownership.
- Legacy `pipe-io-v2.c` still has the previously identified unsafe cleanup
  paths and remains unchanged.

## Production status

**NO-GO.** This layer is isolated in the v2 prototype and must not be wired into
the production broker or used to provision a service until the remaining
acceptance matrix and the protected-filesystem broker gates are independently
completed.
