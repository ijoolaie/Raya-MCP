# Submitter Partial Destroy Failure and Serialized Retry — 2026-11-16

## Result
PASS — 100/100 disposable x64 Windows runs; 1,000 explicit checks. The tested implementation was a temporary patch over a copy of the actual Submitter source; it is not checked into the Working Copy and is not Phase 1 acceptance.

## Test setup
- Copied only `dedicated-io-submitter-v2.c/.h` into a disposable directory.
- Added a temporary `cleanup_owner` Interlocked gate to serialize destruction callers.
- Added a narrowly-scoped retry branch for the exact partial state: initialized + joined + DEGRADED + thread NULL + wake_event NULL + changed_event non-NULL + empty queue + zero active producers/issue/leases.
- Used the actual `raya_submitter_start_with_api` and actual shutdown/join/destroy entry points with a custom Win32 API table. Event/thread creation, wait, exit-code query and successful closes used real Win32 calls.
- The close callback failed exactly once on the second event close (after the thread handle and wake event had closed); the changed_event close failed. The first Destroy returned failure and preserved the remaining handle and initialized object. Retry closed only the remaining changed_event, then deleted the critical section and marked the object uninitialized.

## Evidence
- 100 iterations; 1,000 CHECK assertions passed.
- MSVC x64 `/W4 /WX /TC /O2` build succeeded.
- Output: `PASS iterations=100 checks=1000 close_calls_per_iteration=3` (three close attempts on initial path including thread handle, then one successful retry close; the test verifies the exact count/state transitions).
- The cleanup owner field returned to zero after every failure and success.

## Scope and limitations
- This validates the temporary retry algorithm against the actual Submitter lifecycle APIs, not the unmodified checked-in implementation. The current source's Destroy rejects DEGRADED lifecycle, so the retry branch is a prototype proposal, not a current capability.
- No concurrent cleanup-thread contention was exercised in this run; serialization was checked through the atomic owner protocol and per-run owner state, not a two-thread barrier test.
- No Adapter or Worker Orchestrator cursor integration was included. The next integration must preserve the Submitter substep cursor at Destroy after Join has succeeded, and must not re-run Join on retry.
- External callers must be quiesced and storage kept alive; an embedded cleanup token cannot make stale raw pointers safe.

## Cleanup and integrity
- Temporary directory contained $($items.Count) files and $bytes bytes; copied source, harness, object files and executable were inspected and removed.
- No Working Copy source/header/test/build/model/archive/Production file was modified. No Commit or Reset; no process was terminated.
