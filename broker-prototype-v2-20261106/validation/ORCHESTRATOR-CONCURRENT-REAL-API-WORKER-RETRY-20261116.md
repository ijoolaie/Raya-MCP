# Orchestrator Concurrent Cleanup + Real Component APIs + Worker Failure Retry — 2026-11-16

## Result
PASS — 50/50 disposable x64 runs; 750 explicit CHECK assertions. This is a temporary integration prototype, not a checked-in Orchestrator, not a test of unmodified Worker Destroy, and not Phase 1 acceptance.

## Integration and execution
- Copied actual Adapter, Submitter, and Worker implementation sources and required dependencies into a disposable directory.
- Built with MSVC x64 `/W4 /WX /TC /O2`.
- Started actual Worker; configured its existing close callback; started actual Adapter, which initialized Submitter according to implementation behavior.
- Temporary Orchestrator used one Interlocked cleanup owner and a seven-substep cursor: Adapter Destroy; Submitter Shutdown; Submitter Join; Submitter Destroy; Worker Shutdown; Worker Join; Worker Destroy.
- Cleanup thread 1 acquired owner and reached Worker Destroy. The Worker close callback signaled a barrier and blocked.
- Cleanup thread 2 attempted cleanup while thread 1 held ownership; it returned BUSY (2) in all 50 iterations and did not call any component API.
- The callback then injected one Worker close failure. Thread 1 returned failure with cursor still at Worker Destroy (6), and released owner.
- A subsequent retry resumed at Worker Destroy and succeeded. All successful earlier substeps were called exactly once; Worker Destroy was called twice (one failure, one success).

## Evidence
- 50 iterations; 750 explicit checks passed.
- Output: `PASS iterations=50 checks=750 concurrent_contender_busy=50 worker_close_fail_retry=50`.
- Per iteration: Adapter Destroy=1; Submitter Shutdown=1, Join=1, Destroy=1; Worker Shutdown=1, Join=1, Destroy=2.

## Critical limitations
- The Worker close seam required a temporary one-line source-copy adaptation: current Worker Destroy directly calls `CloseHandle` rather than using the configured close callback. The tested callback therefore does not prove the unmodified Worker implementation is failure-injectable through its public seam.
- Submitter partial Destroy failure/retry was not included in this combined concurrency run. It remains a separate 100-iteration temporary prototype with a proposed retry branch and owner token.
- Borrowed Worker mode was not exercised here. The standalone owner/cursor policy harness covered Borrowed mode with modeled callbacks, not actual component APIs.
- This harness did not test late raw-pointer callers or free storage while unknown callers might remain. External caller quiescence and storage lifetime are mandatory; the embedded owner token cannot make stale pointers safe.
- No checked-in Orchestrator exists. The current Submitter/Worker Destroy APIs are not independently safe for concurrent callers; the tested wrapper serializes them.
- The Adapter header's borrowed-Submitter wording remains inconsistent with the implementation, which initializes Submitter itself.

## Cleanup and integrity
- Temporary directory contained $($items.Count) files and $bytes bytes; copied sources, harness, object files, and executable were inventoried and removed.
- No Working Copy source/header/test/build/model/archive/Production file was modified. No Commit or Reset; no process was terminated.
