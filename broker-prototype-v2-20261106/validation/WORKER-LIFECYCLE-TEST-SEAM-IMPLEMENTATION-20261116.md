# Worker Lifecycle Test-Seam Implementation and Verification — 2026-11-16

Status: implemented in Working Copy only. Production behavior path compiled without RAYA_ADAPTER_TESTING. No Source Project or Production changes.

## Changes

1. `reaper-managed-worker-v2.h`
   - Added test-only callback types for lifecycle wait, exit-code query, and handle close.
   - Added callback/context fields to the worker struct only under `RAYA_ADAPTER_TESTING`.
   - Added `raya_managed_worker_set_lifecycle_api`, test-only declaration.
2. `reaper-managed-worker-v2.c`
   - Added default wrappers forwarding to WaitForSingleObject, GetExitCodeThread, and CloseHandle.
   - Initialized defaults only in test builds.
   - Added setter guarded by `RAYA_ADAPTER_TESTING`; it requires started/initialized Worker, non-null callbacks, ACCEPTING lifecycle, and zero active/outstanding/queued/reserved/registry/retained state.
   - Routed Join's wait, exit-code query, and thread-handle close through the callbacks in test builds only.
   - Routed Destroy's wake-event close through the callback in test builds only.
   - Non-test builds retain direct Win32 calls.
3. Added `reaper-managed-worker-v2-lifecycle-seam-test.c`.
4. Added `build-reaper-managed-worker-v2-lifecycle-seam-test.cmd` with strict warnings and cleanup of its exact generated objects/executable.

The existing `close_handle` callback for operation finalizer handles was not reused for Worker lifecycle handles.

## Verification

### New lifecycle seam test
- Compiled with `/W4 /WX /DRAYA_ADAPTER_TESTING`.
- 10 consecutive successful executions.
- 23 assertions per execution; 230 assertions total.
- Injected and verified:
  - Join wait timeout;
  - WAIT_FAILED;
  - GetExitCodeThread failure;
  - thread HANDLE CloseHandle failure with HANDLE retained for retry;
  - nonzero reported thread exit code with successful reaping and joined=true;
  - Worker Destroy wake-event CloseHandle failure with object/handle retained;
  - successful retry closes remaining wake event and finalizes Worker.

Note: abnormal exit code is injected by the test callback after the real thread is signaled; this deterministically exercises Join's abnormal-exit reporting branch without making the worker thread itself fail.

### Existing Worker native regression
- Existing `build-reaper-managed-worker-v2-native-test.cmd`.
- 10 consecutive successful executions.
- 35 assertions per execution; 350 assertions total.
- Covers live I/O and admission-lease shutdown drain.

### Non-test / production compile path
- Existing `build-reaper-managed-worker-v2-compile.cmd`.
- Successful compile with `/W4 /WX`, without `RAYA_ADAPTER_TESTING`.
- Confirms production branch still compiles.

## Result

- Total new lifecycle assertions passed: 230.
- Existing native regression assertions passed: 350.
- Production-path compile: PASS.
- No source changes outside the two Worker files; two new test/build files added.
- Build outputs and object files were checked after each run; all expected artifacts were cleaned.
- No cl/link/java/tlc processes remained at final check.
- No files were deleted as cleanup beyond the exact generated artifacts listed by the two build scripts.
- No commits, resets, Production changes, Source Project changes, or IranPriceScout changes.

## Final file hashes

- `reaper-managed-worker-v2.h`: `22E5300A6B47FDB3CF7519ADEEEE4DFD727505514D24C0F6A9361E57CE7F4EA7`
- `reaper-managed-worker-v2.c`: `C6EF1B2101E60D1FEC03D75623256C368CC09C61A304DEBB9342715DE01D121C`
- `reaper-managed-worker-v2-lifecycle-seam-test.c`: `0AD0E67D1EFFE6EF8139E96C6ADBFD58586F5E9C4CC20073A1DEF66D67004EE8`
- `build-reaper-managed-worker-v2-lifecycle-seam-test.cmd`: `287C39E8ADE934D455FCB116FE241C4A15C174234535D398E42ED204A5B26382`

## Remaining limitations

- The seam is test-only and does not make concurrent Join/Destroy safe.
- Shutdown SetEvent injection remains a separate existing wake callback; this step did not alter it.
- Worker startup failure cleanup is not routed through the lifecycle callback.
- Submitter lifecycle seams remain the next target.
- No Structured Lifecycle Result API has been implemented.
- No DEGRADED recovery or Orchestrator cleanup owner/cursor has been implemented.

## Disk and process check

- C: 13,549,838,336 bytes free.
- D: 2,110,455,808 bytes free.
- No cl.exe, link.exe, java.exe, or tlc.exe processes found.
