# Orchestrator Real-I/O and Borrowed-Worker Integration Validation — 2026-11-18

## Scope
Working Copy only:
`D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`

This validation pass reconnected to Raya MCP, verified the expected Working Copy and existing integration-test additions, then built and executed the existing Orchestrator test target. No production deployment, commit, reset, install, download, or process termination was performed.

## Existing test coverage verified
The current `raya-orchestrator-v1-test.c` contains:
- `test_real_io_orchestrator_owned_lifecycle` (line 586): real named-pipe READ and WRITE through Adapter/Submitter/Worker, bounded callback waits, payload/byte verification, Adapter/Worker drained-inventory checks, Submitter snapshot checks, then the ordered Orchestrator cleanup cursor (Adapter Destroy, Submitter admission close/Shutdown/Join/Destroy, OWNED Worker Shutdown/Join/Destroy) and strict Context reclamation.
- `test_borrowed_worker_lifecycle_boundary` (line 766): BORROWED Worker is not torn down by Orchestrator; Context is reclaimed after the common five steps, the external Worker remains alive/accepting, and its external owner performs shutdown/join/destroy.
- Existing failure-path and cursor tests remain part of the same executable.

The borrowed-worker scenario validates the ownership boundary and lifecycle, not a second real named-pipe I/O transfer. The real READ/WRITE scenario is the OWNED Worker path.

## Build and test
Executed the existing script:
`cmd.exe /d /c build-raya-orchestrator-v1-test.cmd`

Compiler: Visual Studio 2026 Build Tools 18.7.3, x64, `cl.exe /W4 /WX /TC`.

Result: **10 consecutive runs; each reported 216 passed, 0 failed** (2,160 passing assertions across the ten runs). The command returned exit code 0 and did not time out.

## Post-run artifact and disk checks
The build script's explicit temporary-artifact allowlist was checked after execution:
- Expected object files/executable remaining: **0**.
- No unrelated files were deleted.
- C: free space after run: **10.17 GB**.
- D: free space after run: **4.48 GB**.

## Verified hashes
- `raya-orchestrator-v1-test.c` — 55,221 bytes — SHA-256 `3748264F1D6FE56894C13C7852606581A44A3AF54D82E6184093DE9C633125D8`
- `raya-orchestrator-v1.c` — 27,655 bytes — SHA-256 `8B5FB4BFCB890E76EBA55649B1BFA16B648B96C48DA2469671E07C248D220B24`
- `raya-orchestrator-v1.h` — 5,915 bytes — SHA-256 `240A4C58F9D1F5B59787B4441CF008FB6B931C0334F6DD377E9F6A9821562363`
- `build-raya-orchestrator-v1-test.cmd` — 1,272 bytes — SHA-256 `A8505C9A391964CF8FE1134E3F9FA335355A86252EA06AF9086DC6569D57D767`

## Limitations and acceptance
- This confirms the focused Orchestrator test executable and its included scenarios only; it is not a complete product-level acceptance run.
- The retained/quarantine failure paths remain fail-closed and are not converted into resource-recovery tests.
- No full product entrypoint/lifecycle integration, durable WAL/file persistence, recipient-side idempotency, or full Raft implementation is established by this result.
- Phase 1 remains **NOT ACCEPTED**. Production remains **NO-GO**.

## Change record for this pass
No C/C++ source, header, test, build script, configuration, or README was modified during this validation pass. This report is the only new file created in this pass.
