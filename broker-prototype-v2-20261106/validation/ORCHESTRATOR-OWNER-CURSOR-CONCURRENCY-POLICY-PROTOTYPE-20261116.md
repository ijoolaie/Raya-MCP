# Orchestrator Owner + Cursor Concurrency Policy Prototype — 2026-11-16

## Result
PASS — standalone Windows C policy harness; NOT a real component integration test.

## Scope
A disposable harness modeled one Orchestrator-wide atomic cleanup-owner token and a monotonic 3-stage cursor (Adapter, Submitter, Worker). Two Windows threads attempted cleanup concurrently. The winning thread held ownership at a barrier while the second contender attempted acquisition. The contender was rejected as busy. The owner then exercised deterministic Submitter failure and, in Owned mode, Worker failure; retries resumed from the exact failed cursor. Borrowed mode skipped Worker cleanup.

## Execution
- x64 MSVC, `/W4 /WX /TC /O2`.
- 200 iterations: 100 Owned, 100 Borrowed.
- 3,200 explicit CHECK assertions passed.
- Verified one concurrent contender rejected while owner held token; Adapter called once; Submitter called twice (one injected failure, one success); Owned Worker called twice (one injected failure, one success); Borrowed Worker called zero times; cursor advanced only after success; owner released after success/failure; terminal completion reached.

## Important limitation
This harness uses modeled step callbacks and state. It does not call the actual Adapter, Submitter, or Worker APIs; does not prove API caller lifetime safety; does not test late callers, component-level concurrent Destroy, real OS handle close failures, Startup/Rollback, or restart. It validates only the Orchestrator policy algorithm. Current project has no checked-in Orchestrator implementation. Do not interpret this as Phase 1 acceptance or production readiness.

## Harness correction
The first harness run exposed an incorrect test expectation about cursor position after Adapter succeeded and Submitter failed. The expectation was corrected to cursor=1; the corrected harness then passed. This was a test-harness assertion correction, not a source-code change or product failure.

## Cleanup and integrity
- Temporary harness files inspected: `owner_cursor_race.c`, `owner_cursor_race.exe`.
- Both temporary files and their directory removed after execution; 145,944 bytes reclaimed.
- No project source/header files changed; no project build outputs changed.
- No Commit, Reset, Production change, or process termination.