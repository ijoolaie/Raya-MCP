# WORKER FAILED-START CLEANUP RETRY — TEMPORARY PROTOTYPE PASS — 2026-11-16

## Scope
A temporary-copy prototype and deterministic regression were completed for the reachable Managed Worker startup path where thread creation fails and wake-event closure fails. No Working Copy source/header/test/build files were modified. No Production changes, commit, or reset.

## Root cause of prior inconclusive attempt
The prior harness installed a custom `close_handle` callback from the thread factory, but the startup failure branch called raw `CloseHandle(w->wake_event)` directly. Therefore the injected callback could never run and the harness did not actually inject the close failure. This was a harness/seam mismatch, not evidence that Worker startup failed before reaching the factory.

## Temporary-copy change
In a disposable full source/header copy:
- Routed the failed-thread-creation wake-event close through the already-stored `w->close_handle` callback, allowing deterministic failure injection.
- Added a narrow `raya_managed_worker_cleanup_failed_start` prototype.
- Cleanup requires initialized, not started, no thread, not joined, DEGRADED lifecycle, and empty caller/outstanding/queued/reserved/registry/retained state.
- On close failure it leaves the wake-event handle and object state intact for retry.
- On close success it nulls the handle, deletes the critical section, and zeroes the object.
- This prototype does not yet serialize concurrent cleanup callers and assumes the owner has externally quiesced all access. It is not merge-ready.

## Regression scenario
For each execution:
1. Start Worker with a factory that returns NULL.
2. Factory installs a close callback that fails the first two calls and succeeds on the third.
3. Startup reaches initialized + DEGRADED, thread NULL, wake event retained.
4. Startup close attempt fails; handle remains valid.
5. First explicit failed-start cleanup retry fails; handle remains valid.
6. Second cleanup retry succeeds; handle becomes NULL and Worker object is reset.
7. A fresh Worker start succeeds, then Shutdown, Join, and Destroy all succeed.

## Results
- Visual Studio 2026 Build Tools 18.7.3 x64.
- Compile flags: `/W4 /WX /TC`.
- 10/10 executions passed.
- 16 assertions per execution; 160 total.
- No assertion failures.

## Cleanup and unchanged project
The exact temporary directory was removed after inspection: 143 files, 821,617 bytes. No active compiler, linker, test, or TLC process remained.

Working Copy hashes unchanged:
- reaper-managed-worker-v2.c: FDCD768B31CD6FF30F435A5588D6E1000394A7B355A2E37C1A8117D6C78225E4
- reaper-managed-worker-v2.h: F45DB7509C2045A37925A86BE06F4871109BA5348A383B75447ACD3C46423818
- dedicated-io-submitter-v2.c: D8ABAFD8D3B731DA141E8B1792534A00EB888055D0F15EB0EDFCCE2D01843941
- integrated-submit-adapter-v2.c: 6E2427C09974EE47B382F80CD11B21FEAAC16D12ECABC6A6155A11E9E8F62FBE

Disk free after cleanup: C 13,568,892,928 bytes; D 2,111,709,184 bytes.

## Limitations / next step
This proves the narrow failed-start handle-retention and retry sequence in a temporary implementation. It does not prove concurrent cleanup safety, Adapter-level rollback integration, restart races, or full Worker/Submitter ownership recovery. Next step is to add serialized cleanup ownership and race tests in a temporary copy, then integrate the Worker retry with Submitter failed-start cleanup at the Adapter startup boundary. Do not merge or claim Production readiness.

Production remains NO-GO; Phase 1 remains NOT ACCEPTED.
