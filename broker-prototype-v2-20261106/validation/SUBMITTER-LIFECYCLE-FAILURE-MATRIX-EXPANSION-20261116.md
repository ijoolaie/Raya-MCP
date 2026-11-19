# Submitter Lifecycle Failure Matrix Expansion — 2026-11-16

Status: test-only coverage expansion in Working Copy. Submitter production source and public header unchanged.

## Discovery

The Submitter already has a complete injectable Win32 API table in its production API boundary and a failure-matrix test. The prior gap was not absence of callback seams: it was incomplete deterministic handle-targeted coverage of the Shutdown/Join/Destroy branches. Therefore this step did not add redundant production callback fields or alter ABI.

## Changes

Only `dedicated-io-submitter-v2-failure-test.c` changed.

- Added one-shot exact-HANDLE failure targeting for SetEvent.
- Added one-shot exact-HANDLE failure targeting for CloseHandle.
- Added test for Shutdown where wake_event signal succeeds but changed_event signal fails; the failure is consumed once and subsequent degradation notifications are allowed to proceed.
- Added test for Join thread-HANDLE CloseHandle failure; verifies joined remains false and thread HANDLE remains available, then retry succeeds.
- Added test for Destroy's second event close failure; verifies wake_event was closed, changed_event remains, and lifecycle becomes DEGRADED. This explicitly demonstrates the current partial-destroy retry blocker rather than claiming retry succeeds.

The existing call-index injection for WaitForSingleObject, GetExitCodeThread, ResetEvent, and SetEvent remains in place.

## Verification

### Expanded failure matrix
- Existing build script: `build-dedicated-io-submitter-v2-failure-test.cmd`.
- 25 consecutive successful executions.
- 193 assertions per execution = 4,825 assertions passed.
- Includes existing create-event/thread failures, SetEvent/ResetEvent/wait failures, Join wait/GetExitCode failures, Destroy first-close failure, queue/ACK failure branches, plus the three new exact-handle cases.

### Existing native regression
- Existing build script: `build-dedicated-io-submitter-v2-native-test.cmd`.
- 25 consecutive successful executions.
- 18 assertions per execution = 450 assertions passed.

### Source integrity
- `dedicated-io-submitter-v2.c` SHA-256 unchanged: `D8ABAFD8D3B731DA141E8B1792534A00EB888055D0F15EB0EDFCCE2D01843941`.
- `dedicated-io-submitter-v2.h` SHA-256 unchanged: `2D4CBB91D3400AC5416FC1DE25D03012AC2D2644D6F848A42D278F38D5191655`.
- No production source, public API, adapter, or Worker file changed in this step.

## Remaining lifecycle gaps

- Submitter Destroy second-close partial state remains DEGRADED and ordinary Destroy precondition blocks retry. The new test records this limitation; no recovery behavior was added.
- Shutdown DEGRADED recovery remains undefined.
- Structured Lifecycle Result API is not implemented.
- Orchestrator cleanup owner/cursor and stable registry remain unimplemented.
- No full chained Owned/Borrowed cleanup matrix yet.

## Cleanup and environment

- Both build scripts removed their named executables and object files; explicit final check found none remaining.
- No cl.exe, link.exe, java.exe, or tlc.exe processes remained.
- C free space: 13,584,539,648 bytes.
- D free space: 2,110,410,752 bytes.
- No files were manually deleted; only exact generated build outputs were removed by the existing build scripts.
- No commits/resets, Production changes, Source Project changes, or IranPriceScout changes.
