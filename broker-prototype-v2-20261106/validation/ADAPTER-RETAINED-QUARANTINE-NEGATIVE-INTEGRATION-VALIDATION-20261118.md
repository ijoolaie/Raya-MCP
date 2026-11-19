# Adapter Retained / Quarantine Negative Integration Regression — 2026-11-18

## Scope
Working Copy only:
`D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`

This pass inspected the existing Adapter API regression cases, executed the existing build/test script, and checked temporary build artifacts and source hashes. No production deployment, commit, reset, install, download, or process termination was performed.

## Negative-path cases inspected
The existing `integrated-submit-adapter-v2-api-test.c` includes fail-closed scenarios:
- OS-accepted READ followed by ownership/handoff uncertainty: Adapter and Worker retain the operation, caller ownership is uncertain, integrated reservation remains held, Submitter is DEGRADED with an issuer lease, callback is not invoked, and Adapter Destroy is refused.
- Finalizer handle-close failure: finalizer state remains close-uncertain, Adapter/Worker retained inventories remain populated, reservation remains held, and Adapter Destroy is refused. The test checks Submitter terminal state and joins the worker thread, but deliberately does not release retained resources.
- ACK signal failures after resource release, for both wake-event and changed-event paths.
- Worker wake failure with a transferred but untracked operation (compiled only under `RAYA_ADAPTER_TESTING`; the existing build script does not define that macro).

The retained negative cases intentionally leave exact resources/handles untouched until the test process exits. The build script invokes the executable ten separate times, so each retained fixture is isolated to its own process. No attempt was made to retry uncertain I/O, free retained objects, or convert diagnostic snapshots into a recovery mechanism.

## Build and test
Executed:
`cmd.exe /d /c build-integrated-submit-adapter-v2-api-test.cmd`

Compiler: Visual Studio 2026 Build Tools 18.7.3, x64, `cl.exe /W4 /WX /TC`.

Result: **10 consecutive executable runs; each reported 751 assertions across 66 cases passed** (7,510 passing assertions in total). Command exit code 0; no timeout; stderr empty.

## Post-run checks
- Expected temporary object files/executable remaining: **0**.
- C: free space: **10.17 GB**.
- D: free space: **4.48 GB**.
- No unrelated files were deleted.

## Verified source/build hashes
- `integrated-submit-adapter-v2-api-test.c` — SHA-256 `9F62641155FF413A69DEA3DDDCBD26DF1B062184B6ADE359BF9F42B05C1A142B`
- `integrated-submit-adapter-v2.c` — SHA-256 `A9B39D0D4094E8418F820E53B3E28FEE64FC7B0A9EC417450F25EC422EE8D87F`
- `integrated-submit-adapter-v2.h` — SHA-256 `DB52B1D6723DCA1159AD8737B83E64EFE26FF4D5F4053A32F9D1C8CD6C0AA1DC`
- `build-integrated-submit-adapter-v2-api-test.cmd` — SHA-256 `A3EA896889A3C93813BB953775D90886C32A835DC402A22288474FA14AA603CE`

## Limitations
- This is the existing Adapter API seam suite, not an Orchestrator-owned retained-resource recovery integration test.
- Production Adapter exposes retained count but no production enumerate/reconcile/release API. The test-only snapshot is diagnostic only.
- A passing fail-closed regression does not demonstrate safe recovery or resource reclamation for retained/uncertain operations.
- The optional `RAYA_ADAPTER_TESTING`-only worker-wake test was not part of this build invocation.
- Phase 1 remains **NOT ACCEPTED**. Production remains **NO-GO**.

## Change record
No C/C++ source, header, test, build script, configuration, or README was modified during this validation pass. This report is the only new file created in this pass.
