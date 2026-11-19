# Submitter Degraded Event Lifecycle Fault Matrix — 2026-11-16

## Scope
A disposable copy of `dedicated-io-submitter-v2.c/.h` was used under `%TEMP%\RayaSubmitterLifecycle_cf5eb1812d57413f9f9ed4d33ffb5812`. The working-copy project sources were not edited. The prototype adds a third manual-reset `degraded_event`, signals it from `degrade_locked`, and has the Submitter thread and ACK waiters wait on their normal event plus this independent event. Reset-and-recheck sections remain under the Submitter lock. This is still a best-effort independent notification, not an absolute guarantee if its own SetEvent fails.

## Lifecycle fault matrix
Toolchain: Visual Studio 2026 Build Tools 18.7.3 x64, `/W4 /WX /TC`.

Six deterministic fault cases were run in 10 separate process executions. All 10 executions passed, 25 assertions per execution, 250 assertions total:

1. Third event creation fails; both earlier handles close successfully; startup returns failure and leaves no initialized object or handles.
2. Third event creation fails; closing the first acquired event fails; object remains initialized and DEGRADED with the surviving handle preserved. It is not joinable and has no public cleanup/retry path.
3. Worker thread creation fails; all three events close successfully; startup returns failure and clears object state.
4. Worker thread creation fails; one event close fails; object remains initialized and DEGRADED with surviving handle preserved. It is not joinable and has no public cleanup/retry path.
5. Normal start/shutdown/join succeeds, but first event close during Destroy fails. Destroy returns failure and marks DEGRADED; a second Destroy is refused because its safety predicate requires STOPPED. The still-open event cannot be retried through the public API.
6. Normal start/shutdown/join succeeds, wake-event close succeeds but changed-event close fails. Destroy returns failure and marks DEGRADED; a second Destroy is refused. Remaining changed/degraded event handles cannot be retried through the public API.

These are confirmed lifecycle gaps in this prototype's cleanup design. The correct response is quarantine and retain, not force-close or memset. A retryable, per-handle cleanup state machine (with no use-after-close and no accidental reuse) would be needed before adoption.

## Existing Integrated Adapter API regression
The full existing API suite was compiled against the temporary prototype, using copied dependency sources/headers only:

- With `RAYA_ADAPTER_TESTING`: 10/10 PASS, 858 assertions × 10 = 8,580; 68 cases/run.
- Without the macro (baseline path): 10/10 PASS, 724 assertions × 10 = 7,240; 66 cases/run.

Combined with lifecycle matrix: 16,070 assertions across these separate test executions. Do not interpret the aggregate as a single suite.

## Interpretation / decision
- The third event's common-case wake path remains compatible with the existing API regression suite.
- Event creation failure can be cleaned when all acquired handles close successfully.
- Close failure during partial startup or Destroy leaves a deliberately retained object but no public retry/reconciliation path. This is a blocking lifecycle defect, not a pass.
- Simultaneous failure of the normal signal and `degraded_event` signal remains outside the guarantee.
- No real I/O recovery, request replay, ACK retry, or ownership release is claimed.
- No working-copy or Production source was changed; no commits/resets and no TLC run.

## Decision
**Prototype remains NOT READY.** Do not move this design into the working source. Next engineering gate is a separately specified and tested retryable handle-cleanup/quarantine lifecycle, including per-handle close disposition and idempotent repeated cleanup. Keep Production NO-GO and Phase 1 NOT ACCEPTED.