# Reaper ownership handoff model test — 2026-11-07

Status: isolated synchronization-model evidence only. This is not a Reaper implementation and does not exercise `pipe-io-v2.c`, real kernel I/O ownership, cancellation, resource cleanup, or shutdown.

## Purpose

Exercise the lock-protected ownership linearization rule from `CANCELLATION-REAPER-DESIGN-20261106.md` in two deterministic interleavings:

1. **Caller wins:** the completion event is already signaled when the caller acquires the ownership lock and performs its zero-time probe. The caller transitions from `OWNER_CALLER_PENDING` to `OWNER_CALLER_COLLECTING`, collects exactly once, and releases exactly once.
2. **Worker wins:** the caller's zero-time probe observes an unsignaled event. A separate signaler then signals completion while the caller still holds the ownership lock, before the caller commits `OWNER_REAPER_QUEUED`. The worker is released to contend for the same lock and cannot inspect the operation until the caller commits the handoff and unlocks. The worker then transitions through `OWNER_REAPER_PENDING`, collects exactly once, and releases exactly once.

The test asserts one collector, one terminal transition, one cleanup count, and the expected owner in each interleaving.

## Build and observed result

- Source: `reaper-handoff-model-test.c`
- Build/run: `build-reaper-handoff-model-test.cmd`
- Compiler: MSVC x64, `/W4 /WX`
- Initial build and run: exit 0; both interleavings passed.
- Repeated process runs: 100/100 passed.
- Temporary executable and object files were removed after testing.

## Limitations

This test validates only the synchronization model and deterministic lock ordering. It does not prove that a future implementation obeys the model, nor does it test Windows I/O completion semantics, `GetOverlappedResult`, `CancelIoEx`, queue wakeup failure, wait-set rotation, admission leases, shutdown, handle-close failures, or any of the 28 required acceptance groups. In particular, the worker in this model is not a production reaper and no operation resources are kernel-owned.

**Gate remains closed:** `pipe-io-v2.c` is still compile-only and its known unsafe completion-failure cleanup paths remain unchanged. Broker Production status remains NO-GO.
