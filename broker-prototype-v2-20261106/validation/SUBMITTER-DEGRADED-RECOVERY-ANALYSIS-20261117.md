# Submitter DEGRADED Recovery Analysis — 2026-11-17

## Scope and decision

Inspected `dedicated-io-submitter-v2.h/.c`, the Win32 failure-matrix test, and the current Orchestrator lifecycle integration. This is a source-level recovery design review. No production code, tests, build scripts, README, or Production resources were changed in this slice.

**Decision: do not add a generic DEGRADED -> STOPPED transition.** The current object does not persist a degradation cause or a request-effect ledger sufficient to prove that all retained work is safe to discard or retry. A counter snapshot alone is not enough. Keep Destroy's existing STOPPED/joined/empty-work preconditions unchanged.

## Observed DEGRADED entry classes

1. **Startup rollback / resource acquisition failure**
   - Second event creation fails and closing the first event fails; or thread creation fails and one or both event closes fail.
   - Object may be initialized but not started, with a partial set of handles and no worker thread.
   - This is a resource-cleanup state, not a running worker recovery. A separate startup-abort cursor is needed; normal Shutdown/Join assumptions do not apply.

2. **Worker wait/wake infrastructure failure**
   - ResetEvent(wake), WaitForSingleObject(wake), or a changed-event signal fails.
   - Depending on the exact path, admitted producers may still exist. A worker exit does not prove the producer-side admission count is zero.
   - Re-signalling can make a waiting thread exit, but does not establish that outstanding producer/request ownership is terminal.

3. **Queue and accounting invariant failure**
   - Head/state mismatch, unexpected active_issue, request-state mismatch, or invalid callback transition.
   - Treat as BLOCKED. Reconstructing counters or relinking queue nodes from guesses risks double issue, loss, or use-after-free.

4. **Request effect uncertain**
   - Issue callback returns ISSUE_UNCERTAIN; publish callback fails after an accepted issue; inconsistent active_issue/request state.
   - Request may have taken effect externally. Must remain BLOCKED until an external, request-identity-specific reconciliation proves the outcome. Never retry issue from this state.

5. **Known not-issued request with notification failure**
   - ISSUE_NOT_ACCEPTED followed by changed-event signal failure; or abort admission changes request to NOT_ISSUED before a signal fails.
   - The request's issue effect is known not-issued in the implementation, but the notification callback is void and has no durable acknowledgment result. Do not equate request state alone with successful downstream ownership transfer. Reconcile callback/owner disposition before reclaiming storage.

6. **Known issued/acknowledged request with notification failure**
   - Accepted issue + successful publish sets ISSUED before changed-event failure; acknowledge sets ACKED and decrements issuer_leases before signaling fails.
   - ISSUED retains an external issuer lease and request storage. ACKED is terminal in the local state machine, but the caller receives failure and must inspect the request state under the documented ownership protocol. Do not reclaim or reissue based only on the failed API return.

7. **Shutdown signaling failure**
   - Wake signal may have succeeded while changed-event signal failed, or wake signal itself failed.
   - Lifecycle becomes DEGRADED. The thread may still be waiting, or may exit after observing DEGRADED. The exact signal effect is currently returned transiently, not stored as a durable degradation reason.

8. **Destroy partial close**
   - `destroy_progress` records a known partial resource close. This is not general lifecycle recovery.
   - Existing Destroy retry logic can continue the remaining event close only under its strict joined/empty-work checks. Preserve it; do not widen Destroy to arbitrary DEGRADED states.

## Why current evidence is insufficient

- `degrade_locked` overwrites lifecycle with DEGRADED and attempts best-effort signals, but records no cause, originating operation, request identity, or whether the best-effort signals succeeded.
- Multiple unrelated failure paths converge on the same lifecycle value and worker exit codes.
- Snapshot exposes aggregate counters, not a complete enumeration of every request's ownership/effect.
- Request objects are caller-owned; the Submitter has no authoritative registry of all admitted/issued/uncertain request storage after dequeue.
- `not_issued` is a void callback; its completion is not represented as a durable state transition.
- Startup rollback can leave initialized state without a started thread, so a generic recovery API must distinguish that from worker failure.
- Destroy's partial-close progress is a separate, already tracked state and must not be confused with the other DEGRADED causes.

## Recovery classification

| Cause | Classification | Permitted action |
|---|---|---|
| Startup rollback with retained handles | External cleanup/reconciliation | Add a dedicated startup-abort operation with explicit per-handle close progress; no worker Join |
| Wake reset/wait failure, no known request effect | External reconciliation | Verify admission drained, enumerate/resolve admitted request ownership, prove thread reaped |
| Queue/accounting invariant failure | Permanently BLOCKED absent authoritative repair evidence | No counter repair or automatic retry |
| ISSUE_UNCERTAIN / publish failure / request UNCERTAIN | BLOCKED pending per-request external evidence | Reconcile by stable request identity; never reissue blindly |
| NOT_ISSUED + callback notification failure | External reconciliation | Prove callback/owner accepted terminal disposition before releasing request |
| ISSUED + signal failure | External reconciliation | Preserve issuer lease and request storage until explicit acknowledgment |
| ACKED + signal failure | Conditionally terminal request; lifecycle still needs reconciliation | Verify request state and caller ownership; no request reissue |
| Shutdown signal failure | External reconciliation | Inspect durable cause + thread state + all request states; no second Shutdown as a recovery primitive |
| STOPPED notification failure | Potentially recoverable only after proof | Verify zero work/leases, successful Join and exact exit cause; then explicit terminalization may be considered |
| Destroy partial close | Same-operation retry only | Existing tracked Destroy retry; no generic lifecycle transition |

## Required design before implementation

1. Add a first-failure-wins degradation record protected by the Submitter lock:
   - reason enum and originating operation;
   - native error and signal substep;
   - worker exit classification;
   - request identity (or explicit NONE);
   - whether request effect is NOT_ISSUED / ISSUED / ACKED / UNCERTAIN;
   - whether best-effort wake/changed signals succeeded.
2. Add a caller-owned, request-identity-specific reconciliation evidence contract. Evidence must be explicit and monotonic; no boolean "force recover" parameter.
3. Add an owner-only `reconcile_degraded_ex` operation that:
   - requires public admission closed and drained;
   - requires worker thread joined/reaped (or a distinct startup-abort path);
   - refuses unknown/inconsistent queue links, counters, request states, or outstanding issuer leases;
   - refuses UNCERTAIN requests without external terminal evidence;
   - transitions to STOPPED only when every request has a proven terminal disposition and all counters/queue are empty;
   - returns structured BLOCKED/RETRYABLE/COMMITTED results and a precise reason.
4. Wire the operation into the Orchestrator cleanup cursor as a distinct reconciliation step. Do not mutate an earlier Shutdown cursor from uncertain to success without fresh evidence.
5. Keep `raya_submitter_destroy_ex` preconditions unchanged: joined, STOPPED, empty queue/counters/leases, and consistent remaining handles. Partial Destroy retry remains narrowly scoped to `destroy_progress`.
6. Add one injected test per degradation origin, including multi-fault best-effort signalling, request retention, uncertain issue/publish, NOT_ISSUED callback handoff, ACKED state, startup rollback, and partial Destroy. Assert both that unsafe cases remain blocked and that any allowed terminalization requires explicit evidence.

## Test coverage observed (existing tests; not rerun in this slice)

The existing failure matrix contains cases for event/thread creation rollback, SetEvent/ResetEvent/wait failures, enqueue retention, abort notification failure, issue/acknowledgment paths, shutdown signal failure, abnormal worker exit, Join retries, and partial Destroy. These tests establish the observed failure modes but do not establish a production recovery protocol. Test-only direct signalling/handle cleanup is not a supported production recovery path.

## Files and integrity

No source or test file changed. SHA-256 observed:
- `dedicated-io-submitter-v2.c`: `593C5C09D7B5B31B57CAF22F559B2CB3E209C1A70D01276412B61D82EBEA64EB`
- `dedicated-io-submitter-v2.h`: `7474625AE0A23DAD2029877914A3964DE2CF6DC92283F0D938FE28D7A0A13A15`
- `dedicated-io-submitter-v2-failure-test.c`: `26A825C24276359E85EABE795E2981AE92118D9700C2D157E9915DA69CDE3D07`
- `raya-orchestrator-v1.c`: `80073CBFFDCBBF96B01879562506616F0ED2E969081D0F50C32BE56D8E534F25`
- `raya-orchestrator-v1.h`: `A73F9961F7C02EE3F64488DC7920BDD092BA4F5763FF05A74A2B4FBF6B342EC7`

