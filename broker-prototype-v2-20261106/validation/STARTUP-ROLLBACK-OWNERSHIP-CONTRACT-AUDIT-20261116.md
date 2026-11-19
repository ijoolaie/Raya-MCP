# STARTUP ROLLBACK OWNERSHIP CONTRACT AUDIT — 2026-11-16

## Scope
Read-only inspection of the current Working Copy startup paths. No project source, header, test, build, Production, or source-project files were modified. No process was started or stopped. This report is a design gate; it does not claim a fix or a passing startup-recovery test.

## Exact startup ownership matrix

| Component / path | State after failure | Handle / lock ownership | Existing retry path |
|---|---|---|---|
| Adapter lock initialization fails | Adapter remains zeroed | No Adapter lock acquired | Retry full start is possible if caller serialized |
| Submitter first event creation fails | Submitter lock deleted and struct zeroed | No retained handle | Retry full start |
| Submitter second event creation fails; wake close succeeds | Submitter lock deleted and struct zeroed | No retained handle | Retry full start |
| Submitter second event creation fails; wake close fails | Submitter initialized, lifecycle DEGRADED, no thread, wake_event retained, lock retained | Submitter still owns wake_event and critical section | No public failed-start cleanup/retry API |
| Submitter thread creation fails; both closes succeed | Submitter lock deleted and struct zeroed | No retained handles | Retry full start |
| Submitter thread creation fails; either close fails | Submitter initialized, lifecycle DEGRADED, thread NULL; each successfully closed handle is nulled, failed handle remains | Submitter owns remaining non-NULL handles and lock | No public failed-start cleanup/retry API |
| Adapter startup receives Submitter failure | Adapter initialized flag cleared, Adapter lock deleted, Adapter zeroed; borrowed Submitter object is not zeroed | Submitter remains independently owned by caller; Worker remains borrowed | Adapter has no rollback helper; caller must separately recover/destroy Submitter, but public API does not expose failed-start cleanup |
| Worker wake-event creation fails | Worker lock deleted; worker struct remains zeroed except no guaranteed post-memset cleanup state | No wake handle | Retry full start |
| Worker integrated-init fails; wake close succeeds | wake_event nulled, lock deleted | No retained handle | Retry full start |
| Worker integrated-init fails; wake close fails | Current code ignores CloseHandle result, sets wake_event=NULL, deletes lock and returns failure | Potential leaked OS handle whose value is discarded | No retry possible through Worker object; this is a handle-loss defect in the failure branch |
| Worker thread factory fails; wake close succeeds | Worker struct zeroed after deleting lock | No retained handle | Retry full start |
| Worker thread factory fails; wake close fails | Worker initialized, started false, thread NULL, wake_event retained, core marked DEGRADED/HANDLE_CLOSE, lock retained | Worker owns wake_event and lock | No startup-cleanup retry API; ordinary Destroy requires joined and is not a valid path |

## Key findings

1. Adapter and its components have distinct ownership. Adapter borrows Submitter and Worker. A failed Adapter start may safely tear down its own lock, but must not silently destroy or zero either borrowed component.
2. In the current Adapter rollback, the Submitter struct itself is not erased. The caller still has its pointer and can inspect initialized, lifecycle, and handle fields. The practical gap is the lack of a supported, narrow cleanup/retry API for the DEGRADED/no-thread startup state.
3. Submitter already has enough state to distinguish partial startup: initialized, lifecycle, started, thread, wake_event, and changed_event. A retry helper must only accept the no-thread failed-start state, require queue/counters empty, close only non-NULL handles, null a handle only after successful close, and keep the object/lock intact after any close failure.
4. The Submitter lifecycle-gate prototype from prior work is not part of the Working Copy. Any real integration would need to combine failed-start cleanup with the atomic admission gate; a helper that bypasses the gate would reintroduce a cleanup/API race.
5. Worker has a close callback setter, but startup's immediate cleanup calls raw CloseHandle directly. The setter is applied to post-start operation finalization, not startup rollback. It cannot inject the failed-start close path because managed_worker_start_internal zeroes the object before configuring it.
6. Worker integrated-init failure is more severe than the thread-factory failure: it ignores CloseHandle's result and unconditionally loses the handle value. The correct contract is to preserve the handle and initialized synchronization state when close fails, mark a distinct failed-start cleanup state, and expose an exclusive retry cleanup operation.
7. raya_managed_worker_destroy requires joined state. A Worker whose thread was never created cannot truthfully be marked joined merely to make Destroy accept it. Startup cleanup must be a separate operation with strict preconditions.
8. No API should attempt to recover a Worker/Submitter after a thread was successfully created or after any request/operation was admitted. This gate is only for pre-thread startup rollback.

## Required safe contract before implementation

- Explicit states: UNINITIALIZED, STARTING, RUNNING, FAILED_START_CLEANUP, CLEANED; do not overload operational DEGRADED for a never-started object.
- Component ownership is explicit: Adapter owns only its own lock/state; caller owns the Submitter and Worker objects; each component owns its own handles.
- Failed-start cleanup is exclusive and retryable. It requires thread == NULL, no started/joined thread, empty queues, zero producer/issue/issuer leases, no reservations/registry entries, and no operation ownership.
- Close only non-NULL handles. Set each field to NULL only after successful close. On any failure retain object, lock, exact handle value, cleanup state, and retry eligibility.
- Delete a critical section and zero an object only after all owned handles are closed and no API entrants can still access it.
- Do not call operational Shutdown/Join/Destroy as substitutes for failed-start cleanup.
- Adapter start failure should return failure while leaving borrowed component state intact and diagnostically inspectable. The caller must be able to call the component's failed-start cleanup explicitly.
- Worker wake-event cleanup and Submitter event cleanup should use dedicated startup-close injection seams configured before initialization (or passed as factory arguments), not post-start setters.
- Add negative controls: close fails repeatedly; first close succeeds and second fails; retry closes only the remaining handle; double cleanup is serialized; normal API calls are rejected during cleanup; successful cleanup permits a fresh start only after the previous object is fully cleaned.

## Verification status
- Read-only source-path inspection: COMPLETE.
- Existing test evidence from earlier reports: Submitter failed-start cleanup retry was demonstrated only in a temporary prototype; Adapter startup rollback demonstrated the retained Submitter failure state. Those results do not verify a supported cleanup API in current project source.
- This turn: no executable tests run because the current Worker startup path has no pre-start close-failure injection seam, and adding one to project source is outside the approved temporary-only scope. A temp-only implementation and test harness should be the next bounded task.
- No claims of production readiness, complete recovery, or Phase 1 acceptance.

## Decision
Stage 5 remains open. Prioritize a temporary-copy end-to-end prototype that adds (a) atomic-gated Submitter failed-start cleanup, (b) Worker failed-start cleanup preserving handles on close failure, including the integrated-init branch, and (c) Adapter-level regression asserting borrowed objects remain intact and explicitly recoverable. Do not merge to Working Copy or Production without a separate authorization and review.
