# Native wake-failure ownership-retention test — 2026-11-07

## Result

- MSVC x64 build: PASS with `/W4 /WX`.
- Independent process runs: 10/10 PASS.
- Assertions: 380 per run; 3,800 total; zero test failures.
- Four concurrent producer threads acquired 70 admission leases and reservations.
- Shutdown began while all 70 callers were admitted/reserved; new admission was rejected.
- 70/70 real overlapped named-pipe reads were submitted and ownership was handed off.
- A real `SetEvent(INVALID_HANDLE_VALUE)` failure was injected after all 70 handoffs.
- The harness marked the integrated lifecycle DEGRADED and verified that all 70 queued operations, outstanding counts, and reservations remained retained.
- Integrated promotion returned `RAYA_INTEGRATED_DEGRADED`; no operation was released and the clean-exit predicate remained false.

## Important finding

The current Integrated Reaper has no recovery path from this wake-failure state:

1. `raya_reaper_handoff` transfers each operation to the queue and decrements caller_active.
2. The wake notification fails.
3. Marking the core DEGRADED is the safe response because the worker may not be awake.
4. `raya_reaper_integrated_promote_one` explicitly refuses promotion whenever lifecycle is DEGRADED.
5. Therefore the 70 transferred operations remain queued with their reservations/outstanding ownership retained. There is no integrated API to safely resume draining or prove shutdown.

This is a demonstrated liveness/recovery gap, not a memory-safety failure in this harness. The test deliberately does not free operation storage, OVERLAPPED structures, events, or pipe handles after the failure. The test process teardown reclaims these disposable resources.

## Limits

- This is a test-only native harness, not a production worker or recovery implementation.
- The failed SetEvent is injected after all operations have been handed off; it isolates post-transfer wake failure rather than racing the worker's wait call.
- The test verifies retention and refusal to promote, not successful recovery.
- It does not link/runtime-test `pipe-io-v2.c`, test worker wait failure, lost-wake recovery, cancellation, disconnect, shutdown timeout, IOCP, or the full 28-group acceptance matrix.

## Required design work before implementation

A recovery design must define an explicit DEGRADED drain protocol that can safely wake or poll the worker, continue exact Registry promotion/collection without reopening admission, preserve queue/Registry/OVERLAPPED ownership, and only reach STOPPED after all caller, queue, outstanding, and reservation predicates are zero. It must also handle failed wake-event recreation and concurrent shutdown without freeing kernel-visible state. Do not change production code until this protocol and its fault-injection tests are specified.

Production remains **NO-GO**. No service, protected root, tunnel, Production handler, or IranPriceScout resource was changed.
