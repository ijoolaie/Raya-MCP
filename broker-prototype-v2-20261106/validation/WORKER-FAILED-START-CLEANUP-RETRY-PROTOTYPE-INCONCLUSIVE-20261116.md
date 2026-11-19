# WORKER FAILED-START CLEANUP RETRY PROTOTYPE — INCONCLUSIVE — 2026-11-16

## Scope and safety
A temporary-copy-only prototype was attempted for the reachable Worker startup failure path: thread factory returns NULL and closing the wake event fails. The experiment did not establish a valid test of that path and is classified INCONCLUSIVE. No source, header, test, or build file in the Working Copy was modified. No Production change, commit, or reset.

## Prototype attempted
In a unique temporary copy, a narrow `raya_managed_worker_cleanup_failed_start` helper was added. Its intended preconditions were:
- Worker initialized, not started, thread NULL, not joined;
- integrated lifecycle DEGRADED;
- caller/outstanding/queued/reserved/registry/retained state empty;
- wake event and close callback non-NULL.

It attempted to close the retained wake event, preserve it on close failure, and only after successful close delete the critical section and zero the object. A test factory was intended to install a close callback that fails once, then succeeds, and return NULL; the test would retry cleanup and then perform a fresh start/shutdown/join/destroy.

## Result
- The temporary code compiled successfully with Visual Studio 2026 Build Tools 18.7.3 x64 and `/W4 /WX /TC`.
- The runtime test failed before reaching the intended observable failed-thread-creation state: after `raya_managed_worker_start_with_factory` returned false, the Worker object was zeroed (`initialized=0`, `wake_event=NULL`, `thread=NULL`, lifecycle 0), and the injected close callback had not been called.
- The test did not isolate whether the early return came from critical-section initialization or wake-event creation. No claim is made that the cleanup prototype passed, or that retry cleanup was validated.
- A first compile attempt also exposed a temporary test-header newline-generation error; this was corrected before the successful compile. This was confined to the disposable temporary copy.

## Cleanup
The exact temporary directory was removed after inspecting its contents: 143 files, 822,816 bytes. No active compiler, linker, or test process remained. Existing project outputs were not removed.

## Working Copy source hashes unchanged
- reaper-managed-worker-v2.c: FDCD768B31CD6FF30F435A5588D6E1000394A7B355A2E37C1A8117D6C78225E4
- reaper-managed-worker-v2.h: F45DB7509C2045A37925A86BE06F4871109BA5348A383B75447ACD3C46423818
- dedicated-io-submitter-v2.c: D8ABAFD8D3B731DA141E8B1792534A00EB888055D0F15EB0EDFCCE2D01843941
- integrated-submit-adapter-v2.c: 6E2427C09974EE47B382F80CD11B21FEAAC16D12ECABC6A6155A11E9E8F62FBE

## Next action
Before extending the prototype, instrument a temporary harness to separately prove successful `InitializeCriticalSectionEx`, successful `CreateEventW`, entry into the injected factory, and invocation of the injected close callback. Only after those preconditions are observed should retry-cleanup behavior be tested. Do not merge this prototype or treat it as a fix.

Production remains NO-GO; Phase 1 remains NOT ACCEPTED.
