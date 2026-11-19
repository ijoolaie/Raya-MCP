# Lifecycle-managed Reaper Worker v2 — implementation checkpoint (2026-11-08)

## Scope

Implemented an independent reaper-managed-worker-v2.c/.h over the existing Integrated Reaper, Registry, WaitPlan, pipe completion classifier, owner primitive, and two-phase finalizer. The legacy FIFO Worker and pipe-io-v2.c remain unchanged. This is a prototype milestone, not Production authorization.

## Runtime model

- Exactly one lifecycle-managed worker thread is created at worker startup. No per-operation thread is created.
- One manual-reset wake event and one CRITICAL_SECTION serialize lifecycle, admissions, reservations, queue, registry, wait-plan snapshots, and wake reset/signaling.
- Producer handoff and SetEvent occur while holding the same lock.
- Worker resets wake and inspects/promotes predicates while holding that lock, then builds the WaitPlan under the same lock and waits outside it.
- WaitPlan uses the existing 63-operation window plus wake handle; the registry cursor advances across windows.
- Registry entries point to the first member (raya_integrated_op) of a caller-owned, stable-address raya_managed_operation.
- Each operation carries the exact issuing pipe HANDLE, exact OVERLAPPED, dedicated completion event, retained buffer reference, owner state, finalizer state, and release callback.
- Callers MUST issue the original overlapped I/O using op->issuing_pipe and &op->overlapped. The worker collects with that exact HANDLE/OVERLAPPED pair. The event must be op->overlapped.hEvent and op->integrated.completion_event.
- The supplied pipe and event HANDLEs are owned duplicates transferred to the operation. The caller must not close/reuse them after initialization/handoff.
- Caller-owned operation and buffer storage must remain stable until the release callback.
- Admission and reservation precede handoff. An admission acquired before shutdown can still hand off while the lifecycle is STOPPING; new admissions/reservations are rejected after shutdown begins.
- raya_managed_worker_finish closes an admission lease when no handoff will occur. There is intentionally no anonymous reservation decrement.

## Collection and release

1. Worker promotes queued operations into the Registry and marks the owner as Reaper-pending.
2. WaitPlan resolution and Integrated claim happen under the shared lock.
3. Event signal is confirmed, then GetOverlappedResult(FALSE) runs outside the lock against the exact issuing pipe and exact OVERLAPPED.
4. ERROR_IO_INCOMPLETE restores the Integrated collect state to pending and retains owner/resources. No release occurs. The worker backs off outside the lock with a bounded 1, 2, 4, 8, 16, then 32 ms delay progression, preventing a permanently signaled event from causing a tight retry loop.
5. Only API success or the narrowly permitted signaled/exact-pair ERROR_OPERATION_ABORTED is terminal. Other errors mark COMPLETION_UNCERTAIN and retain ownership; automatic polling recovery is disabled.
6. Terminal results transition through Integrated terminal-hold. outstanding and reservation remain held.
7. Outside the lock, the finalizer closes the duplicated issuing pipe, then the completion event. Any close failure is sticky, records HANDLE_CLOSE, retains outstanding/reservation, and blocks release.
8. After both handles close, finalizer commit and owner release are recorded; Integrated release then decrements outstanding/reservation under the lock.
9. Only after all commits does the void release callback run outside the lock. It is called exactly once; it may free the operation. It must not fail and the worker does not touch the operation afterward.

## Public API usage

Typical success path:

1. Start worker.
2. Initialize operation with owned duplicate pipe/event handles and stable caller buffer.
3. Acquire admission.
4. Issue overlapped I/O using the operation's exact issuing handle and embedded OVERLAPPED.
5. Reserve the operation.
6. Handoff. TRANSFERRED_DEGRADED still means ownership transferred and the caller must not reclaim it.
7. On completion, wait for the release callback.
8. Request shutdown, join, and destroy only after a clean STOPPED result.

If I/O issuance fails synchronously or reservation fails after I/O has been issued, the caller still owns the pending/terminal I/O and must not free its resources. The current API has no cancellation/unreserve convenience path for an unhanded reserved operation; handle this explicitly at the caller and treat this as a remaining API gap.

## Native acceptance completed

MSVC x64, /W4 /WX, actual byte-mode overlapped named pipes:

| Harness | Repetitions | Assertions per run | Result |
|---|---:|---:|---|
| Single live operation + barrier-observed spurious-signaled/incomplete retry + admission finish + repeated shutdown | 10 | 35 | 10/10 pass |
| 70 concurrent operations, >63-event WaitPlan windows; original pipe handles closed after handoff | 3 | 987 | 3/3 pass; 210/210 operations collected |
| Exact-pair cancellation / ERROR_OPERATION_ABORTED | 10 | 26 | 10/10 pass |
| CancelIoEx ERROR_NOT_FOUND completion-wins race | 10 | 30 | 10/10 pass; terminal success collected by Worker |
| Injected completion-event close failure after pipe close | 10 | 34 | 10/10 pass; ownership retained and callback not invoked |
| Injected issuing-pipe close failure before event close | 10 | 36 | 10/10 pass; ownership retained and callback not invoked |
| Four concurrent producers; 70 pre-reserved handoffs after STOPPING; new admission rejected | 10 | 810 | 10/10 pass; 700/700 handoffs and completions |
| Injected worker-thread creation failure | 10 | 6 | 10/10 pass; unstarted lock/event state cleaned |

Total across these harnesses: 12,731 assertions. The 70-operation harness hands off all operations before completing any I/O, closes each caller's original pipe handle immediately after handoff, then exercises three runs of the 63-event window boundary. The four-producer harness creates 70 pending operations, reserves all 70 across four concurrent producer threads, requests shutdown, verifies new admission is rejected, then permits all pre-admitted handoffs to drain. The single-operation harness deliberately signals the completion event while the read is still pending, proves ERROR_IO_INCOMPLETE is retained/retried with bounded backoff, and then completes normally. The two close-failure harnesses independently inject completion-event failure after pipe close and issuing-pipe failure before event close; both verify sticky HANDLE_CLOSE, outstanding == 1, reservation retained, no release commit, and no callback. The start-failure harness injects CreateThread failure and verifies the unstarted worker's wake event and lock are cleaned before the failed start returns.

## Remaining blockers / not yet accepted

- This is not the 28-group full native acceptance matrix. It does not yet prove all producer races, wake/reset faults, invalid WaitPlan handles, all initialization failure points, wake/reset faults, or all shutdown/cancellation interleavings.
- No lifecycle-managed recovery worker exists. WAIT_FAILED, wake signal failure, uncertain completion, invalid waitset, ownership mismatch, or handle-close failure leave the system degraded and retained.
- Worker-thread creation failure now has a deterministic factory seam and cleans the unstarted wake event/critical section when CloseHandle succeeds. InitializeCriticalSectionEx, CreateEvent, integrated-init, and cleanup-CloseHandle failure points are not all injected or runtime-tested.
- There is no safe cancel/unreserve API for an operation reserved but not handed off. The caller must not abandon such a reservation.
- ERROR_IO_INCOMPLETE after an observed signaled event is retained and retried with a bounded 1–32 ms backoff outside the lock; broader fairness under many simultaneous spurious signals remains unproven.
- Release callback is a void contract. Callback implementations must be non-failing and must not retain/use the operation after returning if they free it.
- The worker has not been wired into pipe-io-v2.c, the broker transport, service identity, protected roots, Production, tunnel, or any unrelated service.
- pipe-io-v2.c still has the previously documented unsafe completion-resource free paths and remains blocked.
- Production status remains NO-GO.

## Files

- reaper-managed-worker-v2.h/.c
- reaper-managed-worker-v2-native-test.c
- reaper-managed-worker-v2-multi-native-test.c
- reaper-managed-worker-v2-abort-native-test.c
- reaper-managed-worker-v2-cancel-notfound-native-test.c
- reaper-managed-worker-v2-closefail-native-test.c
- reaper-managed-worker-v2-pipeclosefail-native-test.c
- reaper-managed-worker-v2-producer-native-test.c
- reaper-managed-worker-v2-startfail-test.c
- matching build-reaper-managed-worker-v2-*-test.cmd scripts, including both independent HANDLE-close failure harnesses
- build-reaper-managed-worker-v2-compile.cmd
