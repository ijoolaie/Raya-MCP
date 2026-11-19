# Submitter Partial Cleanup Retry — Extended Fault Matrix — 2026-11-16

## Scope
Temporary-copy prototype only. No project source/header/test files were modified.

## Prototype
- Added test-only `raya_submitter_cleanup_partial` in the temporary copy; kept existing `raya_submitter_destroy` behavior unchanged.
- Added NULL guards in `degrade_locked`, so it does not call SetEvent for a handle already closed and nulled.
- Cleanup requires STOPPED or DEGRADED, empty queue, zero active producers/issues/issuer leases, and no thread or a joined thread.
- Closes only non-NULL handles; successful close is immediately recorded as NULL.
- If close fails, pointer remains owned and a later call may retry.
- Fake close API contract in tests: a FALSE return leaves the handle open and owned. This is a test seam guarantee, not proof for arbitrary Win32/custom API failures.
- Concurrent cleanup versus shutdown/join/destroy is NOT supported by this prototype; caller serialization is assumed and not enforced. No concurrency test is claimed.

## Tests
Toolchain: Visual Studio 2026 Build Tools 18.7.3 x64, `/W4 /WX /TC`.

1. Win32 failure matrix: 182 assertions/run × 10 runs = 1,820 assertions, all PASS.
   - Startup failure after second Event creation fails and first Event close fails; cleanup_partial retry closes the remaining owned handle.
   - Thread creation failure with close failure; cleanup_partial retry closes remaining handle.
   - Destroy first-close failure.
   - Destroy where wake close succeeds and changed close fails; confirms wake_event is NULL, only changed_event remains owned, NULL handle is not signaled, retry closes only changed_event.
   - Existing injected startup, wake, changed and lifecycle failure cases.
2. Native Submitter core: 18 assertions/run × 10 = 180, all PASS.
3. Multi-producer regression: 137 assertions/run × 10 = 1,370, all PASS.

Total: 3,370 assertions across 30 executions.

## Limits / decision
- No concurrent lifecycle serialization gate exists. This remains a blocking design gap; do not integrate this prototype into the main source.
- Retry safety depends on the injected close failure contract. The general API contract must state whether FALSE guarantees the handle remains valid and owned. If not, retry must be forbidden for ambiguous handles.
- The prototype does not yet include an enforced lifecycle mutex/gate or a race test for cleanup against shutdown/join/destroy.
- No Integrated Adapter regression was run against this modified Submitter.
- Result: useful bounded evidence for sequential retry and NULL-handle guarding only. Not production-ready.

## Cleanup and safety
- Work was confined to a unique temporary directory under %TEMP%.
- No project source/header/test files changed; no commits/resets, Production changes, source-project changes or TLC.
