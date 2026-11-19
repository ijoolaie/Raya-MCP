# STAGE-5-21-MULTI-ADAPTER-RUNTIME-READINESS-AND-BASELINE-20261118

## Scope
Continue Stage 5.16 multi-Adapter fault-injection work without manufacturing retained records or changing production behavior.

## Verified baseline
- Re-read the current API test harness and build script in the Working Copy.
- Re-ran `build-integrated-submit-adapter-v2-api-test.cmd` with Visual Studio 2026 BuildTools 18.7.3.
- Build completed with `/W4 /WX`.
- 10 consecutive executions passed.
- Each execution reported 821 assertions across 66 cases; total 8,210 assertion executions.
- All 15 EXE/OBJ outputs declared by the script were absent after its own cleanup.
- No manual deletion was performed.
- Free space before build: C: 13,233,872,896 bytes; D: 4,810,444,800 bytes.
- The script's exact declared outputs were checked after execution; none remained.

## Runtime multi-Adapter test boundary
The current harness now has a multi-Submitter ACK event dispatcher and a passing dispatcher-isolation unit test. That test proves event-handle routing between two registered Submitter states only. It does not exercise two Adapters borrowing one Worker.

The real ACK-retention path still configures one Worker close callback with one `api_state *`. The callback arms a single Submitter's ACK failure after its second close. A real shared-Worker test therefore still requires a Worker-wide callback context that routes each close by exact resource ownership to the correct Adapter state; close ordering cannot be used as an ownership key.

The adapter API callbacks remain per-Adapter, while the Worker close callback is configured once for the shared Worker. The multi-Submitter dispatcher does not solve this Worker callback-context boundary. No two-Adapter runtime test was attempted in this pass, and no second retained operation was fabricated.

## Changes
- No source, header, test, build-script, README, TLA+ model, or configuration files changed in this pass.
- This report is the only new file.

## Status
- 5.16: multi-Adapter retained runtime test remains BLOCKED / NOT COMPLETE.
- 5.17: direct managed-pipe retained identity runtime assertions remain PASS from the earlier execution record.
- 5.18: first-retain immutability remains PARTIAL.
- 5.19: snapshot consistency remains PARTIAL; no two independent Adapter-associated retained records proven.
- 5.20: production retained enumeration/reconciliation contract remains draft-only; implementation and acceptance NOT COMPLETE.
- Stage 5 review/reporting checklist 5.1–5.15 remains complete. Stage 5 implementation/recovery acceptance remains BLOCKED / NOT ACCEPTED.
- Phase 1: NOT ACCEPTED.
- Production: NO-GO.

## Next exact work
1. Add per-`api_state` tracking for the exact event and duplicated pipe handles created for an operation.
2. Implement and unit-test a Worker-wide close callback dispatcher that routes by exact tracked handle identity, not callback order.
3. Prove the dispatcher is safe for concurrent callback invocation and that state registration/unregistration cannot race active Worker callbacks.
4. Only then construct a two-Adapter/two-Submitter/one-Worker named-pipe READ test and independently verify each Adapter retained snapshot plus the shared Worker inventory.
5. If teardown cannot safely preserve both quarantined operations until Worker thread exit, stop and record the precise blocker; do not add test-only release behavior.
