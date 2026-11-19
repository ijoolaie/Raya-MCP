# Submitter DEGRADED Cause Classification — Slice 2026-11-17

## Scope
Working Copy only:
`D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`

This slice extends provisional first-failure diagnostics. It does not provide recovery authorization, request identity, or a complete classification of every DEGRADED transition.

## Implemented classifications
New reason values:
- `WAKE_RESET`
- `WAKE_WAIT`
- `QUEUE_INVARIANT`
- `ISSUE_UNCERTAIN`
- `PUBLISH_UNCERTAIN`
- `NOTIFICATION`
- Reserved for subsequent call-site wiring: `ACK_INVARIANT`, `CHANGED_RESET`, `CHANGED_WAIT`.

Wired call sites:
- Worker wake-event ResetEvent failure: capture `GetLastError()` immediately and classify WAKE_RESET.
- Worker wake-event WaitForSingleObject failure: capture `GetLastError()` before entering the critical section and classify WAKE_WAIT.
- Submitter queue head/state/active-issue mismatch: classify QUEUE_INVARIANT with semantic `ERROR_INVALID_DATA`.
- Issue callback state/lease inconsistency and unexpected issue result: classify ISSUE_UNCERTAIN with `ERROR_INVALID_DATA`.
- Publish callback failure after issue acceptance: mark request UNCERTAIN and classify PUBLISH_UNCERTAIN with `ERROR_INVALID_DATA`.
- Changed-event notification failures in worker, not-accepted, accepted/published, and Shutdown paths: classify NOTIFICATION; native errors are captured at the failing Win32 call or preserved by Shutdown's existing local error variable.
- First-failure-wins and aggregate counter capture remain unchanged.

## Verification
Command:
`cmd /c build-dedicated-io-submitter-v2-failure-test.cmd`

- MSVC x64 build succeeded.
- 246 assertions per run × 25 consecutive runs.
- **6,150 assertions passed; 0 failed.**
- Added assertions verify WAKE_RESET and WAKE_WAIT reason/error capture and NOTIFICATION reason/error capture for Shutdown and the generic signal-failure path.

## File hashes
- `dedicated-io-submitter-v2.h` — `0B9BAB6B82846469007BCCA4D88DD086B4B43005829D4CE41CC10EE3CCC45399`
- `dedicated-io-submitter-v2.c` — `B5B8F132DA26CA421ECACD35E733F1B59929EF7E88382F443A3EFC3405EEAC70`
- `dedicated-io-submitter-v2-failure-test.c` — `3E5FD2D877D3FC86C4FBB68D612440ED2F44F47EEA41495C2F384F1EADD4E767`

## Artifact and environment inspection
- No target failure-test EXE or OBJ remained in the working-copy root.
- No target test, compiler, or linker process remained. The only match from the broad process filter was the inspection PowerShell process itself.
- No files were deleted.
- Free space after build: C: 16,463,179,776 bytes; D: 2,077,491,200 bytes.

## Remaining generic degradation call sites
The source still contains six generic `degrade_locked` call sites (excluding the helper definition):
1. Enqueue publication signaling failure / retained request.
2. Abort-admission signaling failure.
3. Acknowledge with zero issuer leases (invariant).
4. Acknowledge notification failure.
5. Acknowledge wait/reset path notification failure (reset).
6. Acknowledge changed-event wait failure.

These remain UNCLASSIFIED and require distinct reason/error capture and targeted tests. The reserved ACK_INVARIANT, CHANGED_RESET, and CHANGED_WAIT reason values are not yet wired.

## Limitations
- No stable request identifier or request-effect ledger was added.
- No Reconciliation API or DEGRADED-to-STOPPED transition was added.
- No generic retry was enabled; uncertainty remains blocked.
- This is not complete call-site coverage and must not be described as such.
- Production remains NO-GO; Phase 1 remains NOT ACCEPTED.
