# Submitter Structured Lifecycle Result Implementation — 2026-11-16

## Scope and safety
Implemented only in the Raya MCP Working Copy:
D:\Work\project\Raya-MCP\broker-prototype-v2-20261106

No commits, resets, Production changes, Source Project changes, installs, downloads, or IranPriceScout/tunnel changes were performed. Existing uncommitted work was preserved. No broad cleanup was performed.

## Changed files
1. dedicated-io-submitter-v2.h
   - Added structured operation status, side-effect, and retry-class enums.
   - Added raya_submitter_op_result with output struct size, step, Win32 error, exit code, and resource-presence booleans.
   - Added raya_submitter_shutdown_ex, raya_submitter_join_ex, and raya_submitter_destroy_ex; retained all legacy declarations.
   - Added persistent destroy_progress to the public Submitter struct.
   - Documented that caller must quiesce all Submitter API users and serialize teardown/retries.

2. dedicated-io-submitter-v2.c
   - Implemented the structured _ex operations; legacy APIs delegate to them and preserve their prior Boolean success semantics.
   - Join reports the distinction between a retryable wait/get-exit/CloseHandle failure and an abnormal exit after the thread HANDLE was successfully reaped. A repeated Join on an already reaped abnormal thread reports the committed effect again, without trying to close it twice.
   - Shutdown reports reconciliation-required failure when signaling fails; it does not claim that DEGRADED shutdown is generically retryable. If wake signaling succeeded but changed-event signaling failed, the result records the committed signaling side effect.
   - Destroy records cleanup progress. If the first HANDLE close fails, progress state 3 permits retry of that exact still-open HANDLE. If wake_event closes and changed_event close fails, progress state 1 records the partial effect; a later Destroy retry skips wake_event and retries only changed_event.
   - The final successful Destroy closes changed_event, deletes the critical section, and marks the object uninitialized.
   - No thread-safe/concurrent Destroy guarantee is added. The caller must externally quiesce all other Submitter API users and serialize cleanup attempts.

3. dedicated-io-submitter-v2-failure-test.c
   - Added exact assertions for first-HANDLE close retry, second-HANDLE partial-destroy retry, and abnormal Join result/effect reporting.
   - Existing injectable Win32 API table reused; no additional production test seam was added.

## Verification
All commands were run from the Working Copy. Builds used the existing scripts and MSVC /W4 /WX.

| Check | Actual result |
|---|---:|
| Submitter Win32 failure matrix | 223 assertions/run × 25 consecutive runs = 5,575 passed |
| Submitter native regression | 18 assertions/run × 25 consecutive runs = 450 passed |
| Integrated Adapter API regression | 724 assertions/run × 10 consecutive runs = 7,240 passed across 66 cases/run |
| Total verified assertions across repetitions | 13,265 passed |

The failure matrix included first-close retry, second-close partial retry, Join thread-close retry, wait/get-exit failures, and abnormal exit after committed Join. The adapter regression confirms the changed Submitter public struct/API remains source-compatible with this checked-in Adapter test build.

## Deliberate limitations / remaining gates
- Public Submitter struct layout changed; consumers must be rebuilt against the updated header. No binary ABI compatibility is claimed.
- struct_size is populated in returned result objects; this implementation does not yet provide caller-sized/version-negotiated result writes.
- _ex operations are not a replacement for the stable-handle registry, admission leases, cleanup-owner token, or orchestrator durable cleanup cursor.
- Shutdown failure from DEGRADED remains reconciliation-required; no generic signal-retry protocol is asserted.
- The API cannot protect against callers concurrently invoking Submitter methods while Destroy runs. External quiescence and serialized teardown are mandatory.
- Submitter startup rollback partial cleanup remains a separate lifecycle gap.
- No Worker _ex conversion or Adapter/orchestrator integration was attempted in this step.
- Phase 1 remains NOT ACCEPTED; Production remains NO-GO.

## Build artifact handling
The three existing build scripts completed their own cleanup. A post-run inspection found all five relevant executable/object artifacts absent:
- dedicated-io-submitter-v2-failure-test.exe
- dedicated-io-submitter-v2-native-test.exe
- dedicated-io-submitter-v2.obj
- dedicated-io-submitter-v2-failure-test.obj
- dedicated-io-submitter-v2-native-test.obj

No unrelated files were deleted. Post-run process inspection found no cl.exe, link.exe, java.exe, or tlc.exe process.

## Disk
Post-run free space:
- C: 13,562,793,984 bytes
- D: 2,110,173,184 bytes

## SHA-256
Hashes recorded after the final code/test changes:
- dedicated-io-submitter-v2.h: ECC41A171E8D394CF39A09C8CB1740EE278EF19DAA65F793CDCE2870E0981628
- dedicated-io-submitter-v2.c: C721465F491CAA1CE742554E57044431D34D9A203F746B20EB723D231864A5E3
- dedicated-io-submitter-v2-failure-test.c: 26A825C24276359E85EABE795E2981AE92118D9700C2D157E9915DA69CDE3D07
