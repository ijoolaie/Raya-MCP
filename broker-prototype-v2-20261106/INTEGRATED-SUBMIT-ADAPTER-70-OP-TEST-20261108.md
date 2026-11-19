# Integrated Submit Adapter 70-Operation Test — 2026-11-08

## Harness added
- `integrated-submit-adapter-v2-multi-native-test.c`
- `build-integrated-submit-adapter-v2-multi-native-test.cmd`

The harness creates 70 independent overlapped byte-mode named pipes, starts one Managed Worker and one Dedicated Submitter through the real integrated adapter, and submits 70 one-byte reads from seven producer threads. It waits for the producers to exit, then checks the Worker's live Registry/outstanding/queued/reservation accounting reaches 70 before writing to any peer. It then writes distinct byte values, checks exactly one successful callback per operation and validates each returned byte. The intended test exercises the Worker's >63-event wait-plan path while all 70 operations are live.

## Build outcome
- MSVC x64 compilation of every translation unit completed with `/W4 /WX`.
- Link failed with `LNK1104: cannot open file 'raya-integrated-70-ops-test.exe'`.
- Retrying with a different output filename produced the same linker error.
- A PowerShell write probe in the same directory succeeded; no target executable or matching running test process was found.
- No executable was produced and the harness was **not run**. No assertion count or runtime acceptance is claimed.
- The cause of the linker output-file failure remains undetermined. No attempt was made to bypass host security controls.

## Integrity
- `git diff --check` passed (only pre-existing LF/CRLF warnings were reported).
- Build script cleanup removed generated object files; no test executable exists.
- No commit/reset. No Production, service, tunnel, or IranPriceScout changes.
- Production remains NO-GO.

## Follow-up diagnosis (17:45 local)
- A source review found the harness was calling `ConnectNamedPipe` with a NULL `OVERLAPPED` pointer on a `FILE_FLAG_OVERLAPPED` server handle. This is invalid for the configured handle mode and could invalidate the setup phase. The harness now creates a dedicated connection event, passes a zeroed `OVERLAPPED`, captures `GetLastError` immediately, and accepts only synchronous success or `ERROR_PIPE_CONNECTED` (the client is opened before the connect call).
- The earlier generated PE executable was run once and remained blocked beyond 90 seconds with three threads and no meaningful CPU progress. Only that exact test process was stopped after verifying its executable path and command line. That run is not accepted as a test result.
- Subsequent build attempts still report `LNK1104` for the output executable. The output file/process observations were inconsistent across overlapping invocations, so the linker/file-lock cause is not established. No security control was changed or bypassed.
- Therefore the corrected harness has **not** obtained a reliable, attributable runtime pass. No assertion count is claimed. The next attempt must be serialized: verify no prior build/test process exists, perform one build, capture the full build log, then run one executable with a bounded test timeout and capture its exit code/output before cleanup.

## Integrated 70-operation run — PASS (2026-11-08)
- Corrected the harness readiness predicate: the Managed Worker promotes submitted requests from the core queue into the Registry before waiting, so the stable all-issued condition is `registry.count == 70`, `core.outstanding == 70`, `core.queued == 0`, and `reserved_slots == 70` (not `core.queued == 70`).
- Kept the corrected overlapped named-pipe connection setup using a dedicated event and `OVERLAPPED`.
- Serialized build/test into one executable name and one execution per build invocation to avoid overlapping linker/output-file races during diagnosis.
- MSVC x64 `/W4 /WX` compile and link succeeded.
- Runtime result: **809 assertions passed**. Seven producer threads submitted 70 independent live pipe reads and exited before peers supplied data. The test waited for all 70 operations to be present in the Worker Registry simultaneously, verified no early callbacks, supplied one distinct byte to each peer, then verified exactly-once callbacks, successful status, one-byte transfer, and exact payload for all 70 operations.
- The Worker had to process more than 63 completion events using its rotating WaitPlan windows. Submitter leases drained to zero; retained envelope count was zero; Submitter and Worker shutdown/join/destroy all succeeded.
- This is one successful integrated 70-operation runtime run, not a multi-run soak or full 28-group acceptance. Immediate synchronous `ReadFile` success, injected failures across the adapter boundary, handle-count soak, and production lifecycle integration remain separate gaps.
