# STARTUP ORCHESTRATOR OWNERSHIP POLICY — TEMPORARY STATE-MACHINE TEST — 2026-11-16

## Result
Designed and exercised a temporary startup/teardown ownership policy for Adapter + Submitter + Managed Worker. The isolated policy harness passed 24 assertions. No project source/header/test/build file was changed. No Production change, commit, or reset.

## Contract under test
Ownership is explicit and immutable for an orchestrator instance:
- BORROWED Worker: orchestrator may use it, but must never shut down, join, or destroy it. On Adapter/Submitter startup failure, the Worker remains running for its external owner.
- OWNED Worker: ownership has been transferred to the orchestrator. If later Adapter/Submitter startup fails, the orchestrator rolls back the Worker in order: shutdown, join, destroy.
- Teardown order after successful startup: Adapter admission close/destroy first; Submitter shutdown, join, destroy second; owned Worker shutdown, join, destroy last.
- A failed cleanup step halts teardown immediately. Do not continue destroying dependencies or clear ownership flags. Retry begins at the failed component; already completed steps remain complete.
- A failed Adapter startup may leave a partially initialized Submitter. That Submitter must be explicitly cleaned before the Worker is touched.
- A Worker startup failure is earlier than Adapter startup; clean up only the partially initialized Worker. Do not construct Adapter/Submitter.
- The Adapter API currently borrows Worker and Submitter. The orchestrator is a separate owner layer; do not silently change Adapter ownership semantics.

## Harness
Temporary C state-machine policy harness, compiled with Visual Studio 2026 Build Tools 18.7.3 x64 and `/W4 /WX /TC`.
Scenarios:
1. Successful startup + teardown with BORROWED Worker: Worker remains running; no Worker stop/join/destroy call.
2. Successful startup + teardown with OWNED Worker: Adapter then Submitter then Worker teardown order.
3. Adapter startup failure with BORROWED Worker: no Worker cleanup.
4. Adapter startup failure with OWNED Worker: Worker shutdown/join/destroy rollback.
5. Submitter cleanup failure: teardown stops immediately and retains Worker/Submitter ownership; retry completes in dependency order.
6. Explicit ordering assertions for Adapter → Submitter → Worker.

Result: 24/24 assertions passed.

## Scope and limitations
This is a policy/state-machine harness, not a production implementation and not an integration test of actual Adapter/Submitter/Worker handles or their thread races. It proves the intended ordering and ownership decision table only. Actual partial-handle cleanup, retry tokens, object-storage lifetime, direct API quiescence, and callbacks still require a concrete orchestrator integration test before merge.

One compile attempt initially failed on an unused local under /WX; the temporary harness was corrected and the final run passed. The temporary directory was subsequently removed. No active compiler/linker/orchestrator-test process remained.

## Next work
Implement the orchestrator in a disposable copy around the actual component APIs, with an explicit ownership enum and per-component cleanup progress flags. Test real startup failure injection, repeated cleanup failures, retry idempotency, and both ownership modes. Do not modify Working Copy or Production until separately authorized and reviewed.

Production remains NO-GO; Phase 1 remains NOT ACCEPTED.
