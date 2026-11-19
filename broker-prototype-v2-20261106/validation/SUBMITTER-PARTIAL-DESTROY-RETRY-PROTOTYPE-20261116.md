# Submitter Partial-Destroy Retry Prototype — 2026-11-16

## Result
PASS — disposable-copy prototype only; not integrated into the working source or public API.

## Failure sequence exercised
- Start Submitter with the real `dedicated-io-submitter-v2.c` implementation copied to a disposable directory.
- Shutdown and Join complete normally.
- Thread-handle close succeeds.
- Destroy closes `wake_event` successfully, then injected close of `changed_event` fails. The real implementation marks the object DEGRADED and leaves `changed_event` intact.
- A temporary helper accepts only the precise partial-close shape: initialized + joined + DEGRADED + `wake_event == NULL` + `changed_event != NULL`, with all queues/counters/leases empty.
- First retry of the remaining event close is injected to fail again; object and handle remain intact.
- Second retry succeeds, closes only `changed_event`, deletes the critical section, and marks the object uninitialized.

## Test evidence
- 100 iterations.
- 1,200 assertions reported by the harness.
- MSVC x64 compile with `/W4 /WX /TC` succeeded.
- All 100 iterations passed, including two consecutive close failures for the same remaining event handle.
- No thread, wake-event, or already-closed resource was closed a second time by the retry helper.

## Limits
- The helper is temporary prototype code and was not added to project headers/source.
- This is a component-level cleanup primitive test, not a chained Adapter→Submitter→Worker Orchestrator test.
- No claim is made for Owned/Borrowed orchestration, concurrent cleanup callers, restart-after-rollback, or Production readiness.
- A production design still needs a serialized cleanup-owner token/state and a public lifecycle contract. The helper's exact partial-state predicate must be preserved and reviewed against every possible lifecycle transition.

## Integrity
- No working-copy source/header/test/build file modified.
- No Production change, Commit, or Reset.
- Disposable temp directory contained only two copied source files, the temporary harness, and compiler outputs; it was inspected and removed after the run.
