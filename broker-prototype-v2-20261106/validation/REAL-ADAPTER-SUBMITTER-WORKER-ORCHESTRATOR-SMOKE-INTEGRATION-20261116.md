# Real Adapter–Submitter–Worker Orchestrator Smoke Integration — 2026-11-16

## Result
PASS — disposable-copy integration of the actual component APIs, 100 Owned iterations and 100 Borrowed iterations, 2,900 counted assertions/checks. MSVC x64 build passed with `/W4 /WX /TC`.

## Tested path
- Start actual Managed Worker.
- Start actual Adapter with actual Submitter through `raya_integrated_adapter_start_with_apis`; Adapter borrows Worker and Submitter.
- Staged teardown cursor: Adapter -> Submitter -> (Owned Worker | Done).
- Actual Adapter Destroy succeeds once and closes Adapter admission/state.
- Actual Submitter Shutdown and Join succeed. Submitter API close callback injects failure on `changed_event` close after the thread and `wake_event` have been closed.
- Actual Submitter Destroy returns failure and leaves the documented partial state: initialized, joined, DEGRADED, `wake_event == NULL`, `changed_event != NULL`.
- First call to a disposable-only exact-state retry helper also fails; cursor remains at Submitter. Next retry closes the remaining handle and finalizes Submitter.
- Successful Adapter cleanup is not repeated; Submitter shutdown/join are not repeated after their successful completion.
- Owned mode shuts down, joins, and destroys Worker. Borrowed mode leaves Worker initialized/running and the external owner then shuts down, joins, and destroys it.

## Counts
- Owned: 100 iterations.
- Borrowed: 100 iterations.
- Counted checks/assertions: 2,900.
- Build: Visual Studio 2026 Build Tools 18.7.3 x64, `/W4 /WX /TC`.

## Boundaries / not tested
- This is not full production-ready Orchestrator code; the cursor wrapper and Submitter partial-Destroy retry helper exist only in the disposable harness.
- Worker wake-event `CloseHandle` failure during Worker Destroy is not injected. The current Worker Destroy directly calls `CloseHandle`; its configured close callback applies to operation finalization, not wake-event destruction.
- Adapter Destroy active-entrant contention, concurrent orchestrator cleanup owners, rollback after partial startup, and repeated full restart cycles are not covered by this run.
- Therefore this is a meaningful actual-component lifecycle smoke integration, but not the complete chained-failure acceptance matrix and not Phase 1 acceptance.

## Integrity
- All work occurred in a disposable copy under the user's temp directory.
- Working Copy source/header/test/build files unchanged; six source/header SHA-256 values rechecked.
- Temporary source, copied headers/sources, executable and compiler outputs inspected then removed.
- No Production change, Commit, or Reset.
