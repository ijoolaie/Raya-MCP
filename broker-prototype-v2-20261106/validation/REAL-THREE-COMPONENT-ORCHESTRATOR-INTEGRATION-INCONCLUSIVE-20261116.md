# Real Three-Component Orchestrator Integration Attempt — INCONCLUSIVE — 2026-11-16

## Scope
A disposable-copy harness was assembled to exercise the actual Adapter, Submitter and Managed Worker APIs with a staged teardown cursor, Submitter partial-Destroy retry helper, Worker close-callback seam, Owned/Borrowed modes, and restart.

## Result
**INCONCLUSIVE — no product-code regression is established and no integration pass is claimed.** The temporary harness itself had source-generation/macro-definition errors during iterative construction. A later build reached the runtime but its assertion/control-flow instrumentation was not trustworthy; the harness was stopped rather than accepting misleading output. No valid assertion total or pass rate is reported.

## Useful source/API findings
- `raya_integrated_adapter_start_with_apis` starts Submitter while borrowing Worker.
- Adapter Destroy closes Adapter admission and destroys Adapter state only.
- Submitter Destroy can close `wake_event`, fail on `changed_event`, and move to DEGRADED; a temporary retry helper must be separate from the public API.
- Worker normal Destroy directly calls `CloseHandle(w->wake_event)` in the current source. Its configured `close_handle` callback is not used there. The disposable experiment patched only its copied Worker source to route that close through the callback; this was not a Working Copy change.
- Therefore a deterministic Worker Destroy close-failure integration test currently needs a disposable-only seam or a deliberate temporary source patch. No production seam is inferred.

## Build
The disposable sources were compiled with Visual Studio 2026 Build Tools 18.7.3 x64 and `/W4 /WX /TC`. The harness did not produce trustworthy end-to-end test evidence.

## Integrity and cleanup
- Temporary copy contained actual component sources/headers, a temporary harness, and compiler outputs; all inspected before removal.
- No Working Copy source/header/test/build file changed.
- No Production change, Commit, or Reset.
- No source hashes changed (see separate verification in the work log).
- Temporary test artifacts removed; this report records an inconclusive attempt, not a test result.

## Next required action
Rebuild the harness cleanly from a version-controlled-in-session single source string (one CHECK macro, explicit global declarations, and per-stage assertion counters), then run a single smoke iteration before scaling to 100 Owned and 100 Borrowed iterations. Keep all test-only cleanup helpers and Worker close seam in the disposable copy.
