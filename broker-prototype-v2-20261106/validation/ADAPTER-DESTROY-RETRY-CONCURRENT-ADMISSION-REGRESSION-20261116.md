# Adapter Destroy Retry and Concurrent Admission Regression — 2026-11-16

## Scope
Read-only audit of the current Adapter Destroy implementation, followed by an isolated-copy execution of the existing integrated Adapter API regression suite. No project source, header, test, or build script was modified.

## Code-path findings
`raya_integrated_adapter_destroy` atomically sets the closing bit, rejects destruction while API entrants remain, then checks `retained_count`, `retained_head`, and `active_count` under the Adapter lock. If any operation remains active it returns failure without reopening admission or deleting the lock. A later retry is possible after the admitted callers unwind and retained/active state drains. On success it deletes the critical section and zeroes the Adapter.

The existing `test_destroy_waits_for_concurrent_submit_entrants` exercises eight concurrent submitters held inside the adapter's CreateEvent callback. The first Destroy attempt fails while the entrant leases and active operations exist; a new Submit is rejected after closure; after the eight callers unwind, a second Destroy succeeds. This is a real API test, not a synthetic state-machine harness.

## Execution
- Toolchain: Visual Studio 2026 Build Tools 18.7.3 x64, `/W4 /WX /TC` via the checked-in build script.
- Isolated copy of project `.c` and `.h` files in a unique Temp directory.
- Existing `build-integrated-submit-adapter-v2-api-test.cmd` executed successfully.
- 10/10 executions passed.
- Each execution: 724 assertions across 66 cases.
- Total: 7,240 passing assertions.

## Limitations
- This confirms Adapter admission closure and retry after active Submit callers drain; it does not inject failure into `DeleteCriticalSection` (which has no failure return) or prove safety against callers arriving after successful destruction and object-storage release.
- It does not test a full Orchestrator's rollback after a Submitter/Worker startup failure, nor multi-component cleanup failure ordering. Owned/Borrowed lifecycle tests from the prior report remain normal-path evidence only.
- It does not validate production readiness or authorize integration of prototype cleanup APIs.

## Cleanup and integrity
- Isolated Temp copy removed: 131 files, 609,555 bytes.
- No relevant compiler, linker, or test process remained.
- Working-copy source hashes unchanged:
  - `dedicated-io-submitter-v2.c`: `D8ABAFD8D3B731DA141E8B1792534A00EB888055D0F15EB0EDFCCE2D01843941`
  - `reaper-managed-worker-v2.c`: `FDCD768B31CD6FF30F435A5588D6E1000394A7B355A2E37C1A8117D6C78225E4`
  - `integrated-submit-adapter-v2.c`: `6E2427C09974EE47B382F80CD11B21FEAAC16D12ECABC6A6155A11E9E8F62FBE`
  - `integrated-submit-adapter-v2-api-test.c`: `888567FF2381399A11A9B4070A1BABCF2CA4D55E4CEDEA62A95E256931B8C0F0`
- No Production changes, commits, or resets.
