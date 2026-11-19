# Transferred-Degraded Quarantine Hook and Wake-Race Test — 2026-11-16

## Scope
Working Copy only. Added a test-only handoff publication hook and a deterministic concurrency test. No Production deployment, no source-project edits, no commits/resets, and no TLC run.

## Defect under test
After OS overlapped I/O is accepted, Managed Worker integrated handoff can commit queue ownership and then fail to signal `wake_event`. The prior Adapter path treated `TRANSFERRED_DEGRADED` as transferred but did not register the Adapter envelope. The Worker could remain blocked indefinitely because the failed wake does not wake its infinite wait.

## Test-only implementation
1. `reaper-managed-worker-v2.h/.c`
   - Added `raya_managed_worker_handoff_with_hook` under `RAYA_ADAPTER_TESTING`.
   - Shared internal handoff retains the existing queue/owner/wake ordering.
   - If transfer committed but the handoff is degraded, the hook is invoked before releasing `worker->lock`.
   - The ordinary public `raya_managed_worker_handoff` calls the same helper with no callback. In builds without `RAYA_ADAPTER_TESTING`, the hook type, declaration, and callback path are absent.
2. `integrated-submit-adapter-v2.c/.h`
   - Test-only Adapter callback validates the exact operation/envelope pair and inserts the envelope once into Adapter Retained while Worker lock remains held.
   - It does not insert the pending operation into Worker Retained, dequeue it, collect I/O, close handles, free memory, ACK, replay, or invoke the user callback.
   - The nested lock order is Worker -> Adapter. Existing Adapter paths release Adapter lock before acquiring Worker lock; the Submitter invokes `publish` outside its own lock. No Adapter -> Worker nested path was introduced.
   - A test-only flag can signal the Worker wake event after quarantine publication to force the Worker thread to contend for Worker lock while the hook is still executing.
3. `integrated-submit-adapter-v2-api-test.c`
   - Updated the real named-pipe wake-failure test to force the Worker wake immediately after Adapter quarantine registration and before Worker lock is released.
   - Worker wakes, blocks at the Worker-lock boundary, then observes DEGRADED and exits fail-closed.
   - The test confirms the Adapter Retained entry is present before Worker proceeds, while Worker Retained remains empty and the operation stays queued/owned by Reaper.

## Race-test observations
- OS accepted the overlapped read (`ERROR_IO_PENDING`).
- Integrated handoff committed; operation remains `RAYA_REAPER_OP_QUEUED`, owner `RAYA_PIPE_OWNER_REAPER_QUEUED`, reservation `HANDED_OFF`.
- Injected `SetEvent(wake_event)` failed; core entered DEGRADED with `WAKE_SIGNAL`.
- The test-only hook inserted exactly one Adapter Retained envelope while holding Worker lock.
- The test then signaled the Worker wake event; Worker could not pass the lock boundary until publication completed.
- Worker exited with `ERROR_INVALID_STATE`; it did not collect, close, free, ACK, or callback the operation.
- Snapshot: Adapter Retained count 1, Worker Retained count 0, Adapter active count 1, Worker queue/outstanding/reserved 1/1/1, registry 0, Submitter issuer lease 1, callback count 0. Operation buffer, issuing pipe duplicate, completion event, and OVERLAPPED remained valid.
- The test does not claim a recovery path or prove I/O terminality.

## Validation
Toolchain: Visual Studio 2026 Build Tools 18.7.3 x64, `/W4 /WX /TC`.

- Test-enabled build with `RAYA_ADAPTER_TESTING`: **68 cases / 858 assertions per run**, 10/10 PASS = **8,580 assertions**.
- Baseline build without `RAYA_ADAPTER_TESTING`: **66 cases / 724 assertions per run**, 10/10 PASS = **7,240 assertions**.
- Both builds used unique temporary directories under `%TEMP%`; no project-root `.obj`/`.exe` outputs were created.
- Test-enabled outputs: 15 files, 409,210 bytes; removed.
- Baseline outputs: 15 files, 344,040 bytes; removed.
- Two earlier temporary build/test attempts in this task were also cleaned from their exact unique temp directories (15 files/408,618 bytes and 15 files/409,194 bytes). One initial race variant failed because the operation completion event was not yet part of the wait set; it was corrected to exercise the actual wake-event/Worker-lock race. This failed experiment did not change project artifacts.
- Total temporary build outputs removed in this task: 60 files, 1,571,062 bytes. No pre-existing project evidence or object files were deleted.
- Final temporary-directory audit: no `RayaQuarantine*` directories remain.
- Final process audit: no relevant CL/link/test/TLC process active.

## Important limitations / open work
- This is a **test-only** quarantine hook. It does not add production quarantine behavior.
- The production Worker still has no independent mechanism to observe a failed wake while blocked in `WaitForMultipleObjects(..., INFINITE)`. The test deliberately injects a separate wake after registration; that is a race harness, not a production solution.
- The test's second wake causes Worker to observe DEGRADED at its next Worker-lock boundary. It does not simulate an OS completion event already present in a pre-existing wait set.
- The current Adapter Retained list is reused for test-only quarantine classification; production semantics and recovery reconciliation remain to be designed.
- No generic Retained recovery, list reconciliation, Worker restart, resource release, or Production readiness is claimed.

## Changed Working Copy files and final SHA256
- `reaper-managed-worker-v2.h` — 4,481 bytes — `F45DB7509C2045A37925A86BE06F4871109BA5348A383B75447ACD3C46423818`
- `reaper-managed-worker-v2.c` — 19,975 bytes — `8340C07EC0D4F49E8DC9618152C5E30792B9ACC0DFED3C5E5711F0D3E52C1AE3`
- `integrated-submit-adapter-v2.h` — 4,668 bytes — `4E2CC45DCD9E4EA0EA47441158B40F25CCD900095182CD256A90C285FB8D4882`
- `integrated-submit-adapter-v2.c` — 25,247 bytes — `6E2427C09974EE47B382F80CD11B21FEAAC16D12ECABC6A6155A11E9E8F62FBE`
- `integrated-submit-adapter-v2-api-test.c` — 40,479 bytes — `478DBF4C4B33616D091EAF15924B09DD4CADBA0E478E750A73AB11816791D0E5`

## Environment
- Working Copy: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`
- C: 9,983,270,912 bytes free at final audit.
- D: 2,113,290,240 bytes free at final audit.
- No Production/source project changes; no commits/resets; no TLC run.