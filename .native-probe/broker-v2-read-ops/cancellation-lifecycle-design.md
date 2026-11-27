# Broker v2 overlapped-I/O cancellation lifetime design

Status: research proposal with a passing disposable named-pipe prototype. No production code or service integration.

## Problem statement

The pinned-read probe's `wait_io` calls `CancelIoEx` after timeout or stop, then waits indefinitely for the event before allowing the stack-owned `OVERLAPPED`, event, pipe handle, and buffers to leave scope. This preserves the important lifetime invariant, but the wait is not bounded.

Replacing that wait with a timeout followed by `CloseHandle(event)`, stack return, buffer release, or OVERLAPPED reuse is unsafe. A successful cancellation request does not mean the operation has completed. The OVERLAPPED and buffer must remain valid until completion is observed.

## Non-negotiable invariants

1. Every pending I/O has exactly one lifetime owner.
2. The OVERLAPPED, event/completion state, data buffer, and pipe/file HANDLE remain valid until terminal completion.
3. A cancellation request is not a completion notification.
4. `ERROR_NOT_FOUND` from `CancelIoEx` is treated as a race requiring completion-state inspection, not proof that memory is safe to release.
5. Completion may be normal, canceled (`ERROR_OPERATION_ABORTED`), or failed for another reason. All terminal outcomes release ownership exactly once.
6. No stack-owned OVERLAPPED may escape the function that declared it.
7. No new operation is admitted after shutdown begins; outstanding operations are strictly bounded.
8. If completion is not observed by a shutdown deadline, the system must not fake completion, free the operation, or report a clean shutdown.

## Tested prototype: per-operation events and a single reaper

The disposable experiment now provides two layers of evidence:

- A named-pipe test issues a real overlapped `ReadFile`, requests targeted `CancelIoEx`, and waits for `ERROR_OPERATION_ABORTED`. A second test observes normal payload completion and then verifies a late cancellation request reports `ERROR_NOT_FOUND`. A third test races a writer against cancellation and accepts only a terminal success or cancellation outcome.
- A separate reaper test allocates four independent operation records on the heap. Each record owns its OVERLAPPED, event, buffer, server pipe handle, and client pipe handle. The request side requests cancellation and returns without freeing or touching those resources. A single reaper thread waits for all four completion events, checks terminal status, closes the handles, frees each record once, and signals a separate caller-owned completion event.

Local MSVC x64 results:
- Named-pipe cancellation/completion experiment: 17 assertions passed.
- Heap-owned reaper experiment: 42 assertions passed.
- Both executables passed five consecutive local repetitions (295 assertions total across five repetitions (59 per repetition)).

The reaper prototype uses a bounded four-operation batch and per-operation events. It demonstrates a practical ownership pattern for the research harness; it does not establish a final production concurrency cap or prove that per-operation events are preferable to IOCP at higher scale.

## Reaper timeout behavior

The reaper's five-second grace wait is an observation deadline, not permission to release resources. If it expires, the test records failure and continues waiting for terminal completion while retaining the operation records and their handles. This deliberately demonstrates safe ownership over a guaranteed bounded cleanup time. If an underlying driver never completes cancellation, this in-process design cannot both reclaim resources safely and guarantee an unconditional bounded stop. A worker-isolation fallback would need a separate Windows teardown design and validation.

## Required next tests

1. Repeat the native pipe tests on supported Windows versions and under CI.
2. Exercise synchronous issue failure, partial transfers, broken pipe, peer close, and shutdown with a mixture of normal and canceled operations.
3. Test cancellation races repeatedly and with multiple concurrent operations.
4. Add deterministic fault injection for a completion that is delayed beyond the request deadline.
5. Choose and document the production admission cap and the event-versus-IOCP decision.
6. Only then implement the production operation registry and shutdown path in an isolated Broker prototype.

## Out of scope

- No named-pipe frame codec or transport integration.
- No filesystem operation integration.
- No changes to the existing pinned-read service.
- No production service installation, persistent service, ACL migration, or workspace deployment.
- No resolution of the separate hard-link mutation acceptance question.

## Microsoft references

- [CancelIoEx](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex)
- [Canceling Pending I/O Operations](https://learn.microsoft.com/en-us/windows/win32/fileio/canceling-pending-i-o-operations)
- [Synchronous and Asynchronous I/O](https://learn.microsoft.com/en-us/windows/win32/fileio/synchronous-and-asynchronous-i-o)
