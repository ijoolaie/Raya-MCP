# Submitter Startup / Destroy First-Failure Diagnostics — 2026-11-17

## Scope
Working Copy only:
`D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`

This slice closes two direct-transition gaps in the provisional Submitter first-failure diagnostics. It does not implement recovery or claim complete classification of every DEGRADED cause.

## Changes
- Introduced `capture_degraded_locked`, which captures the first reason, pre-transition lifecycle, explicit error value, and aggregate queue/producer/issue/lease counters, then sets lifecycle to DEGRADED.
- Kept `degrade_locked` as the existing generic path; it still records UNCLASSIFIED and currently obtains its error from `GetLastError()`. This remains provisional and is not proof that all generic call sites preserve the originating native error.
- Startup rollback now records `STARTUP_ROLLBACK` when cleanup CloseHandle fails:
  - Failure closing the wake event after changed-event creation fails.
  - Failure closing either event after thread creation fails.
  - The first cleanup error is retained when both closes fail.
- Destroy now records `DESTROY_CLOSE` for both wake-event and changed-event CloseHandle failures, preserving first-failure-wins across a later retry.
- No Destroy preconditions were relaxed. STOPPED/joined/empty-work requirements remain in force.

## Verification
Build/test command:
`cmd /c build-dedicated-io-submitter-v2-failure-test.cmd`

Result:
- MSVC x64 build succeeded.
- 239 assertions per run, 25 consecutive runs.
- **5,975 assertions passed; 0 failed.**
- New assertions verify Startup Rollback reason, captured ERROR_INVALID_HANDLE, lifecycle before degradation (including the pre-start zero lifecycle state), and Destroy Close reason/error/origin lifecycle.
- A first run exposed an incorrect test expectation for the changed-event-creation failure path: lifecycle is still zero before RUNNING is assigned. The test expectation was corrected to the actual pre-transition state; final 25-run suite passed.

## Files and SHA-256
- `dedicated-io-submitter-v2.h` — unchanged; `27A58EA6C214371D3F9EF1A2E1C56F809D63E5CDD2DE6B03981E26EBD50BC4E2`
- `dedicated-io-submitter-v2.c` — `DAA8FC20E6F7D8B680AA5597C97557EEC2B3F9E63F9A10B2C07590E64DBAF77D`
- `dedicated-io-submitter-v2-failure-test.c` — `CCB8DB125DD23851A7300E7C6F0BD97D75B6922B5997448B5E6690A3CB3A8F62`

## Artifact / environment check
- No target `dedicated-io-submitter-v2-failure-test.exe` or `.obj` remained in the working-copy root after the build script.
- No target test process, compiler, or linker process remained. The only process matching the broad inspection expression was the inspection PowerShell itself.
- No files were deleted; pre-existing unrelated artifacts were left untouched.
- Free space after test: C: 16,537,903,104 bytes; D: 2,111,053,824 bytes.

## Explicit limitations / remaining work
- Generic `degrade_locked` call sites remain UNCLASSIFIED. Their error is still read inside the helper, so explicit per-callsite error capture is not yet complete.
- Queue invariants, issue uncertainty, publish failure, worker reset/wait failures, and operational notification failures are not yet assigned distinct reason codes.
- The diagnostic record still lacks stable request identity and an explicit request-effect snapshot.
- No `reconcile_degraded_ex`, recovery authorization, DEGRADED-to-STOPPED transition, or cleanup-cursor integration was added.
- Startup rollback with retained native handles still has no dedicated recovery API; it must not be freed or retried through a generic path.
- Production remains NO-GO; Phase 1 remains NOT ACCEPTED.
