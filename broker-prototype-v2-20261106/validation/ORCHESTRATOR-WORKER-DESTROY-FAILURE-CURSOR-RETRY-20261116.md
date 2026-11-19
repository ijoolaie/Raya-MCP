# Orchestrator Worker Destroy Failure and Cursor Retry — 2026-11-16

## Result
PASS — 25/25 disposable x64 runs; 375 explicit checks. This is a temporary integration harness, not a checked-in Orchestrator and not Phase 1 acceptance.

## What was exercised
- Copied and compiled the actual Adapter, Submitter, and Worker implementation sources with MSVC x64 `/W4 /WX /TC /O2`.
- Started the actual Worker and initialized the actual Adapter; Adapter initialized its Submitter according to current implementation behavior.
- Used a temporary Orchestrator-wide atomic cleanup owner and a seven-step cursor: Adapter destroy; Submitter shutdown; Submitter join; Submitter destroy; Worker shutdown; Worker join; Worker destroy.
- Adapter and Submitter steps completed successfully on the first cleanup attempt.
- A temporary-only seam routed Worker wake-event destruction through its existing close callback. The callback failed exactly once per iteration, then delegated to real `CloseHandle` on retry.
- First cleanup stopped at cursor 6 (Worker destroy); Adapter and Submitter were already destroyed; Worker remained initialized; owner token was released.
- Second cleanup resumed at cursor 6 without repeating Adapter destroy, Submitter shutdown/join/destroy, or Worker shutdown/join; Worker destroy succeeded and cursor reached 7.

## Evidence
- 25 iterations; 375 explicit CHECK assertions passed.
- Build and executable output: `PASS iterations=25 checks=375`.
- Worker close callback invoked twice per iteration: injected failure, then successful real close.

## Important design finding
A component-level cursor is insufficient for safe retry when shutdown, join, and destroy are separate non-idempotent API calls. The Orchestrator needs a substep cursor (or equivalent explicit lifecycle state) so that a failed Destroy does not repeat an already successful Join. This harness demonstrated that distinction.

## Scope limitations
- The close callback seam is test-only in this disposable copy: the current Worker Destroy implementation uses direct `CloseHandle` and does not route through the configured callback. The temporary source copy was adjusted solely to connect that existing callback for failure injection; Working Copy source was not changed.
- Only Worker Destroy failure was injected. Submitter partial Destroy failure and concurrent cleanup contention were not part of this run; those remain separate prior prototypes/audits.
- This run exercised the Owned Worker path only. Borrowed Worker teardown policy remains covered only by the separate standalone policy harness.
- No storage was freed while arbitrary external raw-pointer callers might still exist. External caller quiescence and pointer lifetime remain mandatory.

## Cleanup and integrity
- Temporary copy contained $($items.Count) files and $bytes bytes; all copied source, harness, object files and executable were removed after inventory.
- No project source/header/test/build/model/archive/Production file was modified. No Commit or Reset; no process was terminated.
