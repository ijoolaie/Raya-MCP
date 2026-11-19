# ACTUAL WORKER + SUBMITTER CLEANUP RETRY INTEGRATION — TEMPORARY COPY — 2026-11-16

## Result
**PASS in disposable copies of the actual Worker and Submitter implementation.** Four actual-component cleanup paths passed 10 consecutive executions (23 assertions per execution; 230 assertions total). The existing Adapter API regression suite also passed 10/10 (858 assertions × 68 cases per execution; 8,580 assertions). Total: 8,810 assertions across 20 test executions.

No Working Copy source/header/test/build file was modified. All changes existed only in a disposable copy under the system Temp directory and were removed after testing.

## Temporary implementation changes
### Managed Worker
- Added a temporary cleanup-owner token to the copied Worker struct.
- Routed the failed thread-creation wake-event close through the already existing Worker close callback, allowing deterministic close failure injection.
- Routed normal Worker Destroy wake-event close through the same callback. Existing Destroy remains retryable after a close failure because it retains STOPPED/joined state and the HANDLE.
- Added a temporary `raya_managed_worker_cleanup_failed_start` entry point. It requires initialized, not started, no thread, not joined, DEGRADED lifecycle, zero caller/outstanding/queued/reserved/registry/retained state. It serializes cleanup ownership, preserves a failed HANDLE, and only deletes the lock/zeros the object after successful close.

### Submitter
- Added a temporary cleanup-owner token to the copied Submitter struct.
- Added a temporary `raya_submitter_cleanup_retry` entry point for both failed-start partial cleanup and post-Join partial Destroy. It accepts only DEGRADED or STOPPED with no thread, empty queue, zero producer/issue/issuer counts, and either never started or joined.
- It closes only non-NULL remaining event handles, nulls a slot only after successful close, retains the object/critical section after any close failure, and zeroes only after all owned handles are closed.
- This helper is a distinct recovery path; ordinary Destroy's STOPPED precondition is unchanged.

Both helpers require the owner to externally quiesce every possible API caller and preserve object storage for the entire cleanup. The temporary owner token is not a replacement for a full public API lifetime gate and does not protect against a late caller after successful object destruction.

## Actual-component test paths
1. **Worker failed startup:** a real Worker startup with an injected thread-factory failure; the close callback fails once on the wake event; the event remains owned; explicit failed-start cleanup retry closes it and resets the object.
2. **Worker normal post-Join Destroy:** real Worker thread starts, shuts down and joins; injected wake-event close fails once; ordinary Destroy is retried and succeeds without losing the event.
3. **Submitter failed startup:** real Submitter startup with second event creation failure; first event close fails; the Submitter remains initialized/DEGRADED with the valid event handle; explicit cleanup retry closes it and resets the object.
4. **Submitter post-Join partial Destroy:** real Submitter thread starts, shuts down and joins; Destroy closes the wake event then injected changed-event close fails; lifecycle becomes DEGRADED; explicit cleanup retry closes only the remaining changed event and resets the object.

The four paths passed 10 executions each within the targeted harness: 23 assertions per execution, 230 total. Built with Visual Studio 2026 Build Tools 18.7.3 x64, `/W4 /WX /TC`.

## Existing regression
The full copied `integrated-submit-adapter-v2-api-test.c` suite was rebuilt with `RAYA_ADAPTER_TESTING` and run 10 consecutive times: 858 assertions across 68 cases per execution, all PASS (8,580 assertions). This verifies the temporary struct/API additions did not break the existing Adapter API suite in the copied tree. It does not mean the new cleanup entry points are covered by that existing suite; they are covered by the four focused scenarios above.

## Cleanup and integrity
- Disposable directory removed: 147 files, 1,219,380 bytes.
- No matching temp directory remained.
- No active compiler/linker/test process remained.
- C free space after cleanup: 13,517,946,880 bytes (~12.59 GiB); D: 2,111,647,744 bytes (~1.97 GiB).
- Working Copy hashes unchanged:
  - `dedicated-io-submitter-v2.c`: `D8ABAFD8D3B731DA141E8B1792534A00EB888055D0F15EB0EDFCCE2D01843941`
  - `dedicated-io-submitter-v2.h`: `2D4CBB91D3400AC5416FC1DE25D03012AC2D2644D6F848A42D278F38D5191655`
  - `reaper-managed-worker-v2.c`: `FDCD768B31CD6FF30F435A5588D6E1000394A7B355A2E37C1A8117D6C78225E4`
  - `reaper-managed-worker-v2.h`: `F45DB7509C2045A37925A86BE06F4871109BA5348A383B75447ACD3C46423818`
  - `integrated-submit-adapter-v2.c`: `6E2427C09974EE47B382F80CD11B21FEAAC16D12ECABC6A6155A11E9E8F62FBE`
  - `integrated-submit-adapter-v2.h`: `4E2CC45DCD9E4EA0EA47441158B40F25CCD900095182CD256A90C285FB8D4882`
  - `integrated-submit-adapter-v2-api-test.c`: `888567FF2381399A11A9B4070A1BABCF2CA4D55E4CEDEA62A95E256931B8C0F0`
- No Production/source project changes, commits, or resets.

## Limitations / remaining gate
- The tests invoke actual Worker and Submitter APIs in a copied build, but the new cleanup helpers are temporary prototypes, not current public project APIs.
- The Submitter helper relies on external API-caller quiescence. It does not integrate the previously prototyped atomic API entrant gate with these cleanup paths.
- The Adapter was linked into the build and the existing Adapter API regression suite passed, but no new real Adapter Startup Orchestrator was implemented or exercised here.
- Owned vs Borrowed Worker orchestration, rollback sequencing across all three components, cleanup concurrency against public API callers, late-caller/object-storage lifetime, and full restart after the combined Adapter rollback remain unvalidated as one integrated scenario.
- No production-ready recovery or merge claim is made.

## Decision
**Actual Worker/Submitter cleanup retry prototypes: PASS. Stage 5 overall: INCOMPLETE. Orchestrator integration gate: OPEN / NOT ACCEPTED. Production: NO-GO.**
Next work should combine the atomic public API admission gate with these retry entry points, then test the real three-component Adapter/Submitter/Worker startup and teardown orchestrator for both Owned and Borrowed Worker modes, including multiple sequential close failures and a fresh restart after complete cleanup.
