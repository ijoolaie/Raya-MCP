# Orchestrator Chained Cleanup Failure Gate Audit — 2026-11-16

## Scope and decision
This step audited the real three-component teardown and failure transitions before attempting a chained-failure test. The integration gate remains BLOCKED. No production or working-copy source was changed. No synthetic test result is presented as an implementation pass.

## Actual teardown behavior
1. Adapter Destroy permanently closes admission. It returns false while entrant leases, active operations, or retained envelopes remain. After they drain, a later Destroy can succeed. Successful Destroy deletes the Adapter critical section and zeroes the object. The Adapter does not own or destroy Submitter/Worker.
2. Submitter Shutdown/Join must complete before Destroy. Destroy requires joined + STOPPED + empty queues/counters. It closes wake_event and nulls it, then closes changed_event and nulls it. If the second close fails, it marks the Submitter DEGRADED while retaining changed_event. The public Destroy retry then fails its STOPPED precondition. A dedicated post-Join partial-destroy cleanup transition is still required; the failed-Startup cleanup prototype does not cover this state.
3. Worker Shutdown/Join precedes Destroy. Destroy requires joined + STOPPED and empty operation/registry/retained state. Its wake_event CloseHandle failure returns false without changing STOPPED or nulling the handle, so the existing Destroy call can be retried. This path has no injected close seam in the current public test API; no failure-injection pass is claimed here.

## Chained rollback implication
- Adapter must be destroyed before Submitter/Worker because it borrows both.
- Submitter must be fully cleaned before proceeding to an Owned Worker teardown.
- Borrowed Worker must never be shut down or destroyed by the orchestrator.
- On any cleanup failure, orchestrator must retain its progress cursor and stop; retry must resume at the same component without repeating successful releases.
- The current public APIs do not yet expose a complete post-Join Submitter partial-destroy retry path, nor an orchestrator-owned API admission/lifetime contract covering late callers and object storage.

## Validation performed in this step
- Read-only inspection of the actual Adapter Destroy, Submitter Shutdown/Join/Destroy, and Worker Shutdown/Join/Destroy implementations.
- No new chained-failure harness was run because the Submitter public lifecycle has no valid retry transition after the second Event close failure. Faking progress at the orchestrator layer would hide rather than test the actual blocker.
- Existing prior evidence for Adapter concurrent admission/Destroy retry remains in `ADAPTER-DESTROY-RETRY-CONCURRENT-ADMISSION-REGRESSION-20261116.md`; it is not counted as a new run in this report.

## Required work before a valid chained-failure test
1. Add a Submitter post-Join cleanup retry contract that accepts the precise partial-destroy state, preserves failed handles, and does not reopen API admission.
2. Integrate the atomic API gate and cleanup-owner token with both failed-start and post-Join cleanup paths; document external storage lifetime and caller quiescence.
3. Add deterministic per-handle close injection for Submitter and Worker cleanup, without affecting thread-handle Join closes.
4. Implement an Orchestrator with explicit Owned/Borrowed Worker mode and a durable-in-memory teardown cursor. Stop on first failure and resume on retry.
5. Test repeated close failures, Owned/Borrowed rollback, caller races, and fresh startup after complete cleanup in a disposable copy.

## Integrity and environment
- Working-copy files were inspected read-only; no source, header, test, build script, or report was changed by this audit.
- No Commit, Reset, Production modification, or destructive cleanup.
