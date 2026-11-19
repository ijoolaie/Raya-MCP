# Submitter DEGRADED Operational Cause Classification — 2026-11-17

## Scope
Working Copy only:
`D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`

This slice completes classification of the previously remaining operational Submitter call sites. It does not add recovery or make every failure recoverable.

## Implemented
- Enqueue: split wake and changed-event signaling so the exact first failing SetEvent error is captured; request remains retained and lifecycle becomes DEGRADED.
- Abort Admission: split changed and wake signaling; capture the first failed SetEvent error.
- Acknowledge:
  - zero issuer leases while request is ISSUED is classified as ACK_INVARIANT / ERROR_INVALID_DATA;
  - changed-event and wake-event notification failures are separately evaluated and classified NOTIFICATION with the failing native error.
- Acknowledge wait loop:
  - ResetEvent failure is CHANGED_RESET with immediate GetLastError capture;
  - WaitForSingleObject failure captures GetLastError before entering the critical section and is CHANGED_WAIT.
- Removed the now-unused generic `degrade_locked` helper. All remaining DEGRADED transitions pass through the explicit reason/error capture helper or the explicit Startup/Destroy capture path.
- First-failure-wins and the existing aggregate diagnostic snapshot remain intact.
- No Destroy preconditions or generic retry behavior were relaxed.

## Operational call-site audit
The previously unclassified operational call sites are now explicitly classified:
- WAKE_RESET
- WAKE_WAIT
- QUEUE_INVARIANT
- ISSUE_UNCERTAIN
- PUBLISH_UNCERTAIN
- NOTIFICATION
- ACK_INVARIANT
- CHANGED_RESET
- CHANGED_WAIT

Startup Rollback and Destroy Close remain classified separately. A source search found no remaining `degrade_locked(` references and only the central `capture_degraded_locked` implementation assigns the DEGRADED lifecycle value.

## Verification
Command:
`cmd /c build-dedicated-io-submitter-v2-failure-test.cmd`

- MSVC x64 build succeeded.
- 256 assertions per run × 25 consecutive runs.
- **6,400 assertions passed; 0 failed.**
- Added assertions cover the ACK invariant and exact reason/error snapshots for enqueue, abort, acknowledge notification, changed-event reset and changed-event wait failures.

## Final file SHA-256
- `dedicated-io-submitter-v2.h` — `0B9BAB6B82846469007BCCA4D88DD086B4B43005829D4CE41CC10EE3CCC45399`
- `dedicated-io-submitter-v2.c` — `4F1243B81250B115AAE271044471DFE9375146231AA012336FFC814B1330530D`
- `dedicated-io-submitter-v2-failure-test.c` — `B70B3E8922B8D4A07C1AAB825AD65D0C1B22967E11D5A8E519393F7BB40920F4`

## Artifact / environment inspection
- No target failure-test EXE or OBJ remained in the working-copy root.
- No target test, compiler or linker process remained; the only match in the process query was the inspection PowerShell process itself.
- No files were deleted.
- Free space after build: C: 15,111,954,432 bytes; D: 2,077,188,096 bytes.

## Important limitations
- This is diagnostic classification, not a recovery protocol.
- The diagnostic record still contains aggregate counters, not stable request identity or a durable per-request effect ledger.
- ERROR_INVALID_DATA identifies semantic invariant/uncertainty classes; it is not a native OS failure code.
- No `reconcile_degraded_ex`, external evidence contract, DEGRADED-to-STOPPED transition, or cursor integration was added.
- A failure after an external side effect may remain permanently blocked without authoritative evidence.
- The Submitter diagnostics do not imply equivalent Worker diagnostics or Worker lifecycle integration.
- Production remains NO-GO; Phase 1 remains NOT ACCEPTED.
