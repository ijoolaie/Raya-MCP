# Broker v2 overlapped-I/O cancellation lifetime design

Status: design review proposal only. No production code or service integration.

## Problem statement

The pinned-read probe's `wait_io` calls `CancelIoEx` after timeout or stop, then waits indefinitely for the event before allowing the stack-owned `OVERLAPPED`, event, pipe handle, and buffers to leave scope. This preserves the important lifetime invariant, but the wait is not bounded.

Replacing that wait with a timeout followed by `CloseHandle(event)`, stack return, buffer release, or OVERLAPPED reuse is unsafe. Microsoft documents that a successful `CancelIoEx` request does not mean the operation has completed; the OVERLAPPED must not be freed or reused until completion is observed. Cancellation may also fail to stop an operation if the underlying driver does not support it or cannot cancel it in its current state.

## Non-negotiable invariants

1. Every pending I/O has exactly one lifetime owner.
2. The OVERLAPPED, event/completion state, data buffer, and file/pipe HANDLE remain valid until the operation reaches a terminal completion state.
3. A cancellation request is not a completion notification.
4. `ERROR_NOT_FOUND` from `CancelIoEx` is treated as a race requiring completion-state inspection, not proof that memory is safe to release.
5. Completion may be normal, canceled (`ERROR_OPERATION_ABORTED`), or failed for another reason. All terminal outcomes release ownership exactly once.
6. No stack-owned OVERLAPPED may escape the function that declared it.
7. No new operation is admitted after shutdown begins; the number of outstanding operations is strictly bounded.
8. If completion is not observed by a shutdown deadline, the system must not fake completion, free the operation, or report a clean shutdown.

## Candidate ownership model for an isolated test harness

Use a heap-allocated operation record with a single owner/reaper reference. The record contains:

- OVERLAPPED and its event (or a completion-queue token);
- the exact pipe/file HANDLE used to issue the I/O;
- the read/write buffer and its capacity;
- operation kind and immutable request identifier;
- state, terminal status, and transferred byte count;
- cancellation-requested and completion-observed flags;
- a reference-counted lifetime or an equivalent single-owner transfer protocol.

The operation is registered before issuing I/O. If the call completes synchronously, the owner records the terminal result and releases it. If it returns `ERROR_IO_PENDING`, ownership transfers to the completion owner. The request thread may wait only until its deadline; on timeout or stop it requests targeted cancellation and returns a timeout/canceled result to its caller, but it does not destroy the operation record.

A dedicated reaper/completion owner observes the terminal completion, validates the final status, closes the event and pipe HANDLE when no other operation references them, frees the buffer, and releases the operation record. The operation record must not be freed by both the request thread and the reaper.

This proposal deliberately does not prescribe IOCP versus per-operation events. Select one after a minimal prototype measures complexity and proves single-owner completion behavior.

## Shutdown and boundedness

- Stop admission first and signal all active request owners.
- Request cancellation for each registered pending operation by exact HANDLE + OVERLAPPED.
- Wait only for a configured shutdown grace period.
- If all operations complete, drain the registry and shut down normally.
- If the grace period expires, mark the worker unhealthy and keep all unresolved operation records and referenced resources owned. Do not recycle them or claim a clean stop.
- Enforce a small hard cap on active operations and a separate cap on unresolved/quarantined operations. Reaching either cap fails new requests closed.
- A process-isolation/worker-termination fallback is a separate design decision. It must be validated against Windows I/O teardown semantics and tested before adoption; this document does not claim that terminating a worker is already a proven safe cleanup mechanism.

This means the current in-process probe cannot promise both an unconditional bounded stop and safe reclamation if a driver never completes cancellation. The safe behavior is to preserve ownership and fail closed; a stronger availability guarantee requires a separately validated isolation boundary.

## State machine

- CREATED: record and resources allocated; I/O not issued.
- PENDING: I/O issued and completion not yet observed.
- CANCEL_REQUESTED: targeted cancellation requested; operation remains pending.
- COMPLETED: terminal completion observed (success, canceled, or failed).
- REAPED: resources released exactly once.

Allowed transitions:
- CREATED -> COMPLETED for synchronous completion or immediate issue failure.
- CREATED -> PENDING for `ERROR_IO_PENDING`.
- PENDING -> COMPLETED for ordinary completion.
- PENDING -> CANCEL_REQUESTED after timeout/stop.
- CANCEL_REQUESTED -> COMPLETED for normal, canceled, or failed completion.
- COMPLETED -> REAPED after sole owner performs cleanup.

No transition from CANCEL_REQUESTED directly to REAPED is allowed.

## Required deterministic tests before native I/O prototype

1. Synchronous success and synchronous issue failure.
2. Pending operation followed by normal completion before timeout.
3. Pending operation followed by cancellation completion.
4. Cancellation request races with normal completion.
5. `CancelIoEx` returns `ERROR_NOT_FOUND`; completion is still independently observed before cleanup.
6. Shutdown with multiple operations; every record reaped exactly once.
7. Shutdown deadline expires; unresolved records remain owned and are not freed/reused.
8. Admission stops at configured cap; no unbounded queue growth.
9. Duplicate completion notification cannot double-free resources.
10. Reaper receives completion after caller has returned; buffers and OVERLAPPED remain valid.
11. Completion failure other than cancellation still reaps exactly once.
12. Injected delayed/no completion demonstrates that request timeout is bounded while cleanup ownership remains retained.

The first implementation should be a pure state-machine harness with synthetic completion events. It must not issue real pipe I/O. Only after those tests pass should a small disposable named-pipe experiment validate actual Windows completion/cancellation behavior.

## Out of scope

- No named-pipe frame codec or transport integration.
- No filesystem operation integration.
- No changes to the existing pinned-read service.
- No production service installation, persistent service, ACL migration, or workspace deployment.
- No resolution of the separate hard-link mutation acceptance question.

## Microsoft references

- [CancelIoEx](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex): cancellation is a request; the OVERLAPPED must not be freed/reused until completion is observed.
- [Canceling Pending I/O Operations](https://learn.microsoft.com/en-us/windows/win32/fileio/canceling-pending-i-o-operations): cancellation is not guaranteed for every underlying driver/operation.
- [Synchronous and Asynchronous I/O](https://learn.microsoft.com/en-us/windows/win32/fileio/synchronous-and-asynchronous-i-o): OVERLAPPED and buffers must remain valid until asynchronous I/O completes.
