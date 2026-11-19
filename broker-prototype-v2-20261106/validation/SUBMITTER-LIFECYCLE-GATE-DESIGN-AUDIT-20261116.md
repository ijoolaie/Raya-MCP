# Submitter Lifecycle Gate Design Audit — 2026-11-16

## Scope
Read-only audit of `dedicated-io-submitter-v2.c/.h`, all project call sites, and the sequential partial-cleanup retry prototype. No project source/header/test/build files changed. No prototype was added in this step.

## Current lifecycle/API hazards
- Public APIs `admit`, `enqueue`, `abort_admission`, `acknowledge`, `shutdown`, `join`, `destroy`, and `snapshot` inspect `initialized` and/or use the Submitter critical section and handles. The `initialized` check is an ordinary unsynchronized read and is not a lifetime gate.
- `join` waits on and closes `thread`, then writes `thread=NULL` and `joined=TRUE` without the Submitter lock. Concurrent Join/Destroy or multiple Join calls can race on the same HANDLE.
- `destroy` reads `joined`, enters the critical section, checks counters, leaves it, closes Events, then deletes the critical section and writes `initialized=FALSE`. Another API can pass its initial check before deletion and enter a deleted critical section.
- Partial close is non-transactional: `wake_event` can be closed and nulled before `changed_event` close fails. The object then remains initialized but lifecycle is degraded. The prototype's NULL guards fix signaling a successfully closed handle, but do not serialize other APIs against retry cleanup.
- `start_with_api` begins with `memset` and initializes the lock/handles. Concurrent Start/restart against any API is unsupported and must remain externally serialized.
- `submitter_main` is not a public entrant; its lifetime is instead protected by the thread HANDLE and successful Join. Destroy must continue to require joined/thread NULL before deleting synchronization resources.

## Required gate model (design only; not implemented)
Use an atomic lifecycle word in object storage, independent of the critical section:
- low bits: count of active public API entrants;
- a closing bit: prevents all new ordinary API entries;
- a separate cleanup-owner bit (or equivalent exclusive token) for serialized cleanup retries after the ordinary gate is closed.

Ordinary API entry must use a CAS loop on the atomic word before reading `initialized`, entering the critical section, or touching an Event/thread HANDLE. If closing is set, reject. Successful entry increments the count; every return path decrements it. The close operation atomically sets closing while preserving the current count. This gives the race a defined winner: an entrant either increments first and is included in the drain count, or closing wins and the entrant is rejected.

Destroy/cleanup sequence:
1. Acquire the exclusive cleanup-owner token; reject a concurrent Destroy/Cleanup attempt.
2. Atomically close ordinary API admission.
3. Wait for the ordinary entrant count to reach zero without using the Submitter critical section or any Event being destroyed.
4. Verify lifecycle, queue, producer/issue/issuer counts, thread state, and joined state. For normal Destroy require STOPPED and joined; for failed-start cleanup allow only the narrowly specified no-thread/zero-activity degraded state.
5. Close each non-NULL handle once. On a successful close, immediately set that field NULL. On a failed/ambiguous close, preserve the handle state as uncertain and do not retry unless the API contract guarantees FALSE means the handle remains valid and owned.
6. Delete the critical section only after all ordinary entrants are drained and no worker thread can use it. Mark the object terminal only after all owned resources are resolved.
7. Keep ordinary admission closed after partial cleanup. A retry uses only the exclusive cleanup token and atomic fields; it must not reopen public APIs or touch a deleted critical section.

The gate protects calls already racing with close, not arbitrary use-after-free by a caller that starts after the object storage has been freed. The owner must still guarantee object storage remains alive while any caller could begin an API call, and must quiesce all producers before freeing/reusing storage. Atomic alignment and initialization must be guaranteed by the public struct contract.

## API coverage and reentrancy constraints
- Gate every public API that can touch object fields, lock, Events, or thread HANDLE: admit, enqueue, abort_admission, acknowledge, shutdown, join, snapshot, and cleanup/destroy.
- `start`/`start_with_api` are construction operations and cannot safely use a gate that has not yet been initialized. Require external serialization and a zero/uninitialized object; do not support concurrent start/restart.
- `join` must hold an entrant lease across the wait and thread-handle close. Serialize concurrent Join calls with an atomic lifecycle operation token or a narrowly scoped Join state; do not rely on the critical section alone for the HANDLE close.
- Acknowledge may wait indefinitely while holding an entrant lease. Cleanup must therefore fail/wait boundedly rather than deleting synchronization while that lease exists. Do not silently cancel ACK waits.
- Document that lifecycle destruction/join cannot be called reentrantly from `issue`, `publish`, or `not_issued` callbacks running on the Submitter thread; otherwise self-join or waiting for the thread's own lifetime can deadlock.
- The Submitter's normal data lock remains responsible for queue/request/lifecycle state. The atomic gate is strictly a lifetime/admission mechanism, not a replacement for the lock.

## Required race/fault test matrix before implementation approval
1. Many simultaneous API entrants versus gate close: each call is either admitted and drained or rejected; no entry after close.
2. `acknowledge` blocked on ISSUING while cleanup begins: cleanup cannot delete lock/events; ACK exits or timeout policy is explicit.
3. `shutdown` versus `join`; two simultaneous `join` calls; `join` versus destroy.
4. `snapshot` and every queue API versus destroy/cleanup.
5. Two simultaneous cleanup retries: exactly one owner; no duplicate CloseHandle.
6. First Event close succeeds, second fails; retry closes only the still-owned handle; no API signals NULL/closed handle.
7. Repeated close failure and ambiguous close result: no double-close; uncertain handle remains quarantined.
8. Failed-start cleanup with no thread versus ordinary API entry.
9. Thread creation succeeds but publication/start state races; ensure worker cannot outlive lock deletion.
10. Callback reentrancy and attempted self-join/destroy are rejected/documented.
11. Stress under multi-producer load and Integrated Adapter regression, plus baseline and fault-injection builds.
12. Run under Application Verifier/page heap or equivalent handle/heap diagnostics where available; assertions alone do not prove no use-after-delete.

## Decision
The sequential partial-cleanup prototype is useful but cannot be integrated as-is. A separate atomic entrant gate plus exclusive cleanup owner is the minimum viable design direction. This audit does not prove the proposed gate correct and does not authorize source changes. The next step, if pursued, is a temporary-copy proof-of-concept of the gate and deterministic race harness, followed by fault matrix and Integrated Adapter regression. Production remains NO-GO; Phase 1 remains NOT ACCEPTED.

## Safety
No source/header/test/config/build files modified; no files deleted; no commits/resets; no Production or source-project changes; no TLC run. Disk and process audit performed after inspection.