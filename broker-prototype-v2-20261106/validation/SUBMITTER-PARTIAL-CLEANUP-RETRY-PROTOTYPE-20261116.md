# Submitter Partial Cleanup Retry Prototype — 2026-11-16

## Scope
Implemented and tested a temporary-copy prototype only. The working-copy source and headers were not modified.

Temporary source directory:
`%TEMP%\RayaSubmitterCleanup_49918c1df3dd466a8ad2f91f79eb1198`

## Prototype changes
- Guarded degraded signaling so SetEvent is called only for non-NULL event handles.
- Added `raya_submitter_cleanup_partial`, callable for initialized partial-startup objects with no worker thread, or for a joined worker.
- Cleanup accepts STOPPED or DEGRADED only after queue, producer, active issue, issuer lease and thread conditions are quiescent.
- Closes only handles still present in the object; NULL is recorded immediately after successful close.
- On close failure, preserves the handle pointer and DEGRADED state; retry closes only resources still owned.
- Destroy delegates to partial cleanup.
- Explicit prototype assumption: injected `close_handle(FALSE)` means the handle remains owned and is safe to retry. This assumption is not established for arbitrary API implementations and must be part of any future API contract.
- Caller must serialize cleanup against shutdown/join/destroy and all other lifecycle calls. No concurrent cleanup protocol was implemented.

## Test evidence
Toolchain: Visual Studio 2026 Build Tools 18.7.3 x64, `/W4 /WX /TC`.

1. Existing Win32 failure matrix, adapted only in the temporary copy to exercise cleanup retry:
   - 181 assertions per run
   - 10/10 PASS
   - 1,810 assertions
   - Includes startup/thread-create failure retention checks and destroy close-failure retry. The critical case where wake close succeeds but changed close fails was directly exercised: wake_event becomes NULL, no subsequent SetEvent is made, and retry closes only changed_event. The public cleanup retry API for startup-only partial objects was not fully asserted in this run and remains an open test.
2. Existing native core regression:
   - 18 assertions per run
   - 10/10 PASS
   - 180 assertions
3. Existing multi-producer regression:
   - 137 assertions per run
   - 10/10 PASS
   - 1,370 assertions

Total: 3,360 assertions across 30 executions, all passing.

## Important limits
- This is a temporary prototype, not a production patch.
- The close-failure ownership assumption is injected by the fake API: FALSE leaves the underlying handle open. It does not prove that every possible Win32 close failure or custom API implementation has that property.
- Cleanup serialization is a caller contract, not enforced by a new lifecycle gate.
- The prototype does not yet model per-handle states beyond pointer ownership/NULL, nor does it address ambiguous handle validity after arbitrary close failures.
- No full integrated Adapter regression was run against this modified temporary Submitter.
- No Production approval or Phase 1 acceptance follows from these tests.

## Safety and cleanup
- Only a unique temporary directory was used.
- No project source/header/test/report was modified during prototype implementation.
- No commits, resets, Production changes, source-project changes or TLC runs.
- Temporary build artifacts and copied source/test files were removed after the run.
