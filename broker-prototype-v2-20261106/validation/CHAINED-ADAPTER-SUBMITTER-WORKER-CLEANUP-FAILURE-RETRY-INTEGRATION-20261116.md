# Chained Adapter–Submitter–Worker Cleanup Failure/Retry Integration — 2026-11-16

## Result
PASS — 100 Owned and 100 Borrowed iterations; 2,600 counted checks/assertions. Actual Adapter, Submitter and Managed Worker APIs were built with x64 MSVC `/W4 /WX /TC` in a disposable copy.

## Tested staged teardown
Cursor stages: 0 Adapter, 1 Submitter, 2 Owned Worker, 3 complete.

Each iteration:
1. Start actual Worker and actual Adapter+Submitter integration.
2. Adapter Destroy succeeds; cursor advances exactly once to Submitter.
3. Actual Submitter Shutdown and Join each run once. Inject two sequential failures while closing `changed_event`: initial Submitter Destroy fails after its wake event has closed; first exact-state retry helper also fails. Cursor remains at Submitter and the partially initialized Submitter object/remaining handle are retained.
4. Second retry closes the remaining Submitter handle and finalizes it. Cursor advances to Worker only for Owned mode.
5. Owned: actual Worker Shutdown and Join each run once. A disposable-only patch routes Worker Destroy's `wake_event` close through the existing close callback. First close fails; Destroy preserves initialized/joined state and the same handle. Next call retries only Worker Destroy, succeeds, and cursor reaches complete.
6. Borrowed: Worker is not shut down, joined, or destroyed by the orchestrator. It remains initialized/running after cursor completion and is subsequently stopped by the external test owner.

## Counts
- Owned: 100/100 passed.
- Borrowed: 100/100 passed.
- Total explicit CHECK macro assertions: 2,600.
- Build: Visual Studio 2026 Build Tools 18.7.3 x64, `/W4 /WX /TC`.

## Important boundaries
- The staged orchestrator wrapper and Submitter partial-Destroy retry helper are disposable test harness code, not checked-in production APIs.
- Worker Destroy close callback routing required a one-line patch to the disposable copy. Working Copy Worker Destroy still directly calls `CloseHandle` for `wake_event`.
- No concurrent Cleanup Owner race, Adapter active-entrant contention, partial startup rollback, or restart-after-rollback was tested here.
- This is chained component cleanup evidence, not Phase 1 acceptance or Production readiness.

## Integrity
- All source edits/test seams were limited to the disposable copy.
- Working Copy source/header/test/build files were not changed; six source/header SHA-256 values were rechecked separately.
- Temporary copy and all compiler outputs inspected then removed.
- No Production change, Commit, or Reset.
