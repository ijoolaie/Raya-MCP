# Orchestrator Submitter Admission Gate + Cleanup Cursor
Date: 2026-11-17
Scope: Working Copy only — `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`

## Summary

Implemented an atomic admission gate for the Submitter's five operational public APIs and integrated the close-and-drain operation into the Orchestrator cleanup cursor. The gate uses the high bit of a `LONG` to mark closed admission and the lower 31 bits to count in-flight operational API entrants. A caller increments with an Interlocked compare/exchange only while open; leave decrements the count. Closing admission atomically sets the high bit, prevents subsequent entries, and reports the remaining entrant count. Repeated close calls are idempotent.

Operational APIs covered:
- `raya_submitter_admit`
- `raya_submitter_enqueue`
- `raya_submitter_abort_admission`
- `raya_submitter_acknowledge`
- `raya_submitter_snapshot`

The API implementations were split into private unchecked bodies and public gate-enter/gate-leave wrappers. The gate is initialized by the existing zero-initialization in Submitter startup.

## Orchestrator integration

Added cleanup step:
- `RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_ADMISSION_CLOSE = 1`

Added `raya_orchestrator_close_submitter_admission_owned`:
1. Requires exact cleanup-owner Context access and zero Registry leases.
2. Requires Adapter Destroy cursor to be committed with SUCCESS.
3. Marks the Submitter admission-close cursor IN_PROGRESS.
4. Closes the operational API gate.
5. If entrants remain, records NOT_ATTEMPTED + RETRYABLE_FAILURE and the active entrant count in `last_error`; the gate remains closed and the same operation may be retried after drain.
6. When the entrant count reaches zero, commits the cursor with SUCCESS.
7. Unknown/blocked/in-progress cursor states are never blindly retried.

No Submitter Shutdown, Join, or Destroy call is performed by this new operation.

## Important boundary / limitations

This is an operational API admission gate, not a blanket gate around every exported function:
- `start` / `start_with_api` require exclusive unpublished storage and must not race with callers.
- Lifecycle functions (`shutdown[_ex]`, `join[_ex]`, `destroy[_ex]`) deliberately bypass the operational gate. They remain cleanup-owner operations and require one serialized lifecycle caller after admission is closed and the operational entrant count is zero.
- The legacy direct lifecycle functions remain for compatibility; they are not safe to race with other lifecycle calls.
- The gate cannot make an arbitrary raw pointer safe after the caller has freed/reused its storage. The owner must prevent new raw-pointer invocations and keep Submitter storage alive through all possible invocations.
- Callback-initiated synchronous destruction remains disallowed.
- No ABI compatibility claim is made for the public struct layout change.

Therefore this slice proves close-and-drain for the five operational APIs under the documented storage-lifetime and lifecycle-owner contract. It does not prove safety against an external caller that violates that contract by racing direct lifecycle calls or using a stale pointer after reclamation.

## Tests executed

All commands ran in the Working Copy. No install/download, process termination, Production operation, or TLA+ rerun occurred.

1. `build-raya-orchestrator-v1-test.cmd`
   - MSVC x64, `/W4 /WX`
   - 65 assertions per run × 10 = 650 passed, 0 failed.
   - Includes a real Submitter instance, a blocked Snapshot entrant, owner-only close, retry after drain, cursor commit, and denial of new Snapshot/Admit/Enqueue/Abort/Acknowledge calls after close.
2. `build-dedicated-io-submitter-admission-gate-test.cmd`
   - MSVC x64, `/W4 /WX`
   - 15 assertions per run × 10 = 150 passed, 0 failed.
3. `build-integrated-submit-adapter-v2-api-test.cmd`
   - MSVC x64, `/W4 /WX`
   - 751 assertions per run × 10 = 7,510 passed, 0 failed.
4. `build-raya-orchestrator-registry-v1-test.cmd`
   - MSVC x64, `/W4 /WX`
   - 50 passed, 0 failed.

Total reported assertions/checks: 8,360 passed; 0 failed. Counts are the sum of the independently executed suites, not unique test cases.

## Build artifact and environment inspection

- Build scripts removed their owned EXE/OBJ outputs.
- Final targeted check found none of the following build-owned outputs: Submitter gate test EXE/OBJ, Orchestrator test EXE/OBJ, Adapter API test OBJ, Registry test OBJ.
- Several unrelated pre-existing OBJ files remain in the directory; they were not modified or deleted because their ownership/purpose was not established.
- No active `cl.exe`, `link.exe`, `java.exe`, or `tlc.exe` process was present in the final inventory.
- Final free space: C: 13,915,156,480 bytes; D: 2,111,660,032 bytes.

## Changed files and SHA-256

- `dedicated-io-submitter-v2.h`
  `7474625AE0A23DAD2029877914A3964DE2CF6DC92283F0D938FE28D7A0A13A15`
- `dedicated-io-submitter-v2.c`
  `593C5C09D7B5B31B57CAF22F559B2CB3E209C1A70D01276412B61D82EBEA64EB`
- `dedicated-io-submitter-admission-gate-test.c` (new)
  `368D7DB5BDC7BC4418C0B1D6161BFAED194D55D3B66E399DA928B4E19C693CE4`
- `build-dedicated-io-submitter-admission-gate-test.cmd` (new)
  `0F913A2F23D2EA6C485CA36213BDAE8CCAA2EF0C6CF2157399A372A62DE6EB71`
- `raya-orchestrator-v1.h`
  `3ED5E7F1F42E6D51679455B13FE630DAAAE258140FAB9D470BDA1861E2F7C176`
- `raya-orchestrator-v1.c`
  `4566101B22674BAE13422CB509AA118419CE61D074A620B7D25B49063E63F8AE`
- `raya-orchestrator-v1-test.c`
  `B4E6DBA65A676ACBC24B49A7FFF604ECA5ECA34B63CEAB84B3181797D9B7BB90`

## Next work

1. Add Submitter Shutdown as an owner-only cursor step, preserving the structured result/effect distinction.
2. Add Join as a separate cursor step, distinguishing retryable wait/query/CloseHandle failure from abnormal exit after the thread handle has already been reaped.
3. Add Destroy as a separate cursor step, preserving its partial-close progress and retry semantics.
4. Add chained failure-injection tests across close-admission → Shutdown → Join → Destroy.
5. Audit lifecycle serialization and external raw-pointer caller quiescence before claiming complete Submitter teardown integration.

No claim is made that Submitter lifecycle teardown is now fully orchestrated.
