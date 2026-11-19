# Managed Reaper / Pipe I/O — 28-group acceptance matrix (2026-11-08)

## Decision

**NOT ACCEPTED FOR PIPE I/O INTEGRATION. Production remains NO-GO.**

The independent managed worker has meaningful native evidence, but it is not a complete replacement for pipe-io-v2.c. This matrix tracks the 28 acceptance groups in CANCELLATION-REAPER-DESIGN-20261106.md. PASS means the named scope is covered by a native test; PARTIAL means only a narrower Worker primitive or adjacent scenario was exercised; NOT RUN means no qualifying test was run.

| # | Required group | Status | Evidence / remaining gap |
|---:|---|---|---|
| 1 | Worker initialization success and each initialization failure point | PARTIAL | Normal startup tested repeatedly; CreateThread failure injected and unstarted state cleaned. InitializeCriticalSectionEx, CreateEvent, integrated-init, and cleanup-close failure injection remain. |
| 2 | Reject I/O after initialization failure | PARTIAL | Admission rejected after injected thread-start failure. No public Pipe I/O admission wrapper exists yet. |
| 3 | Cancellation completes before bounded drain expires | PARTIAL | Real CancelIoEx + exact-pair ERROR_OPERATION_ABORTED collection tested. No bounded synchronous drain API exists in this Worker. |
| 4 | Cancellation remains pending at drain expiry, then queue handoff | NOT RUN | No caller cancellation/drain wrapper or timeout-to-handoff path. |
| 5 | Completion races exactly with queue handoff | NOT RUN | No deterministic completion-vs-handoff barrier test. |
| 6 | Worker wake with one and multiple queued operations | PASS | One-operation live-pipe test and 70-operation/four-producer tests. |
| 7 | Queue signal failure after ownership transfer | NOT RUN | SetEvent failure injection is not wired into the Worker. |
| 8 | Shutdown with empty queue | PASS | Empty-queue shutdown with a pre-admitted lease, finish(), join, and destroy tested. |
| 9 | Shutdown while an operation is queued and pending | PARTIAL | Shutdown is requested while 70 operations are pending and pre-reserved; producers then hand off during STOPPING and all drain. Queue-already-populated shutdown interleaving is not separately isolated. |
| 10 | Shutdown deadline expiry, eventual completion, successful later join | NOT RUN | Shutdown API has no deadline/timeout state. |
| 11 | Exactly-once cleanup accounting | PARTIAL | Success, cancellation, and close-failure paths verify single terminal collection/release or retained ownership. Full path matrix and handle-baseline audit remain. |
| 12 | Repeated stress cycles with handle-count baseline and no growth | PARTIAL | Repeated process runs passed; no before/after OS handle-count baseline was measured. |
| 13 | Caller closes original pipe handle immediately after wrapper return while reaper owns duplicate | PARTIAL | 70-operation test closes each original server HANDLE immediately after handoff; Worker collects through its owned duplicate. No wrapper-return path exists yet. |
| 14 | Peer disconnect during pending read and pending write | NOT RUN | Disconnect terminal semantics are intentionally not allowlisted; no live disconnect matrix. |
| 15 | More than 63 simultaneous operations; rotating-window fairness | PARTIAL | 70 live operations are collected over the 63-event wait-plan windows. Bounded-discovery timing/fairness under a continuously signaled early window is not measured. |
| 16 | Wake races while rotating windows and queue insertion at boundaries | PARTIAL | Four producers concurrently hand off 70 operations after STOPPING. No deterministic barriers at every rotation boundary. |
| 17 | WaitForMultipleObjects failure injection | NOT RUN | No Win32 wait-function injection seam. |
| 18 | Shutdown racing with admitted caller before submission, during pending I/O, and before handoff | PARTIAL | Admission/reservation held across STOPPING, pending I/O, and post-STOPPING handoff tested; not all three timing windows independently barrier-controlled. |
| 19 | Admission rejection after STOPPING and exactly-once lease release on every path | PARTIAL | New admission rejected after STOPPING; successful finish and handoff lease consumption tested. Error-path lease accounting is not exhaustive. |
| 20 | Wake-signal failure with empty-queue finite polling | NOT RUN | No finite polling recovery path; wake failures remain sticky degraded. |
| 21 | Manual-reset wake reset racing enqueue | NOT RUN | Shared-lock protocol is implemented, but no deterministic ResetEvent/enqueue fault-barrier test for this Worker. |
| 22 | WAIT_FAILED followed by zero-time event probes | NOT RUN | Current Worker marks WAIT_FAILED and retains ownership; automatic diagnostic recovery probes are disabled. |
| 23 | CloseHandle failure for event and issuing pipe independently | PASS | Independent native fault-injection harnesses verify event-close failure after pipe close and issuing-pipe close failure before event close; both retain accounting and suppress release callback. |
| 24 | CancelIoEx result matrix | PARTIAL | Successful CancelIoEx followed by exact-pair ERROR_OPERATION_ABORTED and completion-wins ERROR_NOT_FOUND are both tested. Other CancelIoEx failures are not covered. |
| 25 | Immediate-success ReadFile/WriteFile submission and byte-count contract | NOT RUN | Tests use synchronous peer writes but do not validate the wrapper's immediate-success overlapped ReadFile/WriteFile submission contract. |
| 26 | ERROR_IO_PENDING submission and retention until terminal collection | PARTIAL | Actual pending overlapped reads are exercised through completion, cancellation, and close-failure cases; no complete timeout/cancellation wrapper integration. |
| 27 | Immediate non-pending submission failure | NOT RUN | No submission-failure injection or caller cleanup contract in the Worker-only API. |
| 28 | IOCP association and low-bit event suppression | NOT RUN | No completion-port association/low-bit event harness. |

## Native evidence recorded in the implementation checkpoint

MSVC x64 /W4 /WX, disposable byte-mode named pipes:

- Single operation, barrier-observed spurious signaled event followed by ERROR_IO_INCOMPLETE, bounded backoff, admission finish, and repeated shutdown: 35 assertions × 10 runs.
- 70 operations over rotating 63-event windows, with original server HANDLEs closed after handoff: 987 assertions × 3 runs.
- Exact-pair CancelIoEx / ERROR_OPERATION_ABORTED: 26 assertions × 10 runs.
- CancelIoEx ERROR_NOT_FOUND completion-wins race: 30 assertions × 10 runs.
- Injected completion-event close failure after pipe close: 34 assertions × 10 runs.
- Injected issuing-pipe close failure before event close: 36 assertions × 10 runs.
- Four concurrent producer threads, 70 pre-reserved pending operations handed off after STOPPING, and new admission rejection: 810 assertions × 10 runs.
- Injected CreateThread failure and cleanup of unstarted worker state: 6 assertions × 10 runs.

Total: **12,731 assertions passed** across the listed repeated runs. This total is not a substitute for the 28-group acceptance gate.

## Explicit blockers

1. There is no managed Pipe I/O wrapper implementing submission, cancellation drain, deadline semantics, and ownership handoff.
2. The Worker currently degrades and retains resources on WAIT_FAILED, wake failure, uncertain completion, invalid waitset, or handle-close failure. It does not implement the required diagnostic probe/recovery behavior.
3. The 28-group matrix remains mostly partial or not run, especially completion/handoff races, wake failures, WAIT_FAILED recovery, disconnect classification, cancellation result matrix, immediate submission paths, and IOCP suppression.
4. The known unsafe completion-resource cleanup paths in pipe-io-v2.c remain unchanged.
5. No production service, protected root, broker transport, tunnel, or IranPriceScout integration is authorized.

No commit/reset was performed. The implementation and tests remain prototype-only.
