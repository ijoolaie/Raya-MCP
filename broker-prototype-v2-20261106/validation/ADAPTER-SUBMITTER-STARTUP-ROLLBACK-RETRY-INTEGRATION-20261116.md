# ADAPTER STARTUP ROLLBACK — SUBMITTER RETRY WITH BORROWED WORKER — 2026-11-16

## Result
A temporary-copy integrated regression exercised the actual Adapter startup API with a pre-started Managed Worker and injected Submitter startup/cleanup failures. The final full API regression passed: 872 assertions across 68 cases. No Working Copy source/header/test/build files were modified. No Production changes, commit, or reset.

## Scenario
1. Start a real Managed Worker.
2. Call `raya_integrated_adapter_start_with_apis` with a Submitter Win32 API that:
   - creates the first Submitter event,
   - fails creation of the second event,
   - fails closing the first event during Submitter startup rollback,
   - fails the first explicit Submitter cleanup retry,
   - succeeds on the next cleanup retry.
3. Verify Adapter startup returns false and Adapter object is reset/uninitialized.
4. Verify Submitter remains initialized + DEGRADED, with no thread, and retains the first event handle.
5. Verify the retained event handle is still valid with `GetHandleInformation`.
6. First explicit cleanup retry returns false and preserves the handle.
7. Second explicit cleanup retry closes the handle, deletes Submitter lock, and resets Submitter.
8. Verify the borrowed Worker is still initialized, started, and has a live thread after Adapter/Submitter failure.
9. Explicitly shut down, join, and destroy Worker.

## Temporary-only implementation
Added `raya_submitter_cleanup_failed_start` to the temporary copy:
- Exclusive cleanup owner token acquired using InterlockedCompareExchange.
- Requires initialized + DEGRADED, not started, no thread, not joined, and empty queue/producer/issue/issuer state.
- Closes only non-NULL event handles, nulling each only after successful close.
- Failed close preserves remaining handle and returns ownership token to idle for retry.
- Deletes the critical section and zeroes the object only after all handles are closed.

The helper assumes the startup owner externally prevents new calls and quiesces callers. It is not a full public API lifetime gate and is not merge-ready.

## Results
- Visual Studio 2026 Build Tools 18.7.3 x64.
- Compile flags: `/W4 /WX /TC`, with `RAYA_ADAPTER_TESTING`.
- Final full API regression: 872 assertions across 68 cases, PASS.
- This includes the new Adapter startup rollback scenario and existing API tests.
- No runtime assertion failures in the final run.

## Harness corrections
An initial compile attempt failed because a temporary PowerShell text replacement emitted literal newline escape characters; this was corrected before the successful compile. A subsequent test iteration exposed that the injected Submitter close callback was too broad and affected unrelated tests; it was scoped to the new startup scenario. These failed harness iterations are not counted as passes.

## Ownership finding
The Adapter header explicitly says it borrows both already-started components and they must outlive it. The integrated test confirms the correct rollback boundary: on Submitter startup failure, clean up the partially initialized Submitter, but do not implicitly destroy the borrowed Worker. The caller/orchestrator must retain Worker ownership and decide whether to continue using it or explicitly shut it down.

This combined test covers Submitter partial startup rollback while Worker is healthy. It does not combine a Worker failed-start cleanup with a Submitter failure in one Adapter call: Adapter startup cannot begin without an already-started Worker. Worker failed-start retry and its 100-iteration cleanup-owner race were separately validated in the immediately preceding temporary prototypes.

## Cleanup and unchanged project
The disposable temp directory was removed after inspection: 145 files, 1,043,918 bytes. No active compiler, linker, test, or TLC process remained.

Working Copy hashes unchanged:
- dedicated-io-submitter-v2.c: D8ABAFD8D3B731DA141E8B1792534A00EB888055D0F15EB0EDFCCE2D01843941
- dedicated-io-submitter-v2.h: 2D4CBB91D3400AC5416FC1DE25D03012AC2D2644D6F848A42D278F38D5191655
- reaper-managed-worker-v2.c: FDCD768B31CD6FF30F435A5588D6E1000394A7B355A2E37C1A8117D6C78225E4
- reaper-managed-worker-v2.h: F45DB7509C2045A37925A86BE06F4871109BA5348A383B75447ACD3C46423818
- integrated-submit-adapter-v2.c: 6E2427C09974EE47B382F80CD11B21FEAAC16D12ECABC6A6155A11E9E8F62FBE
- integrated-submit-adapter-v2.h: 4E2CC45DCD9E4EA0EA47441158B40F25CCD900095182CD256A90C285FB8D4882
- integrated-submit-adapter-v2-api-test.c: 888567FF2381399A11A9B4070A1BABCF2CA4D55E4CEDEA62A95E256931B8C0F0

Disk free after cleanup: C 13,532,454,912 bytes; D 2,111,696,896 bytes.

## Remaining design work
A production-quality startup orchestrator still needs an explicit ownership contract:
- Worker created by orchestrator vs borrowed Worker supplied by caller.
- On Worker startup failure, retry Worker failed-start cleanup before any Adapter/Submitter startup.
- On Adapter/Submitter startup failure, retry Submitter cleanup, then either leave borrowed Worker running or stop/destroy an orchestrator-owned Worker.
- Preserve both objects and all remaining HANDLEs on any failed cleanup.
- Serialize cleanup and prohibit new API entrants until object destruction is safe.
- Do not make Adapter silently assume ownership of its borrowed components.

Production remains NO-GO; Phase 1 remains NOT ACCEPTED.
