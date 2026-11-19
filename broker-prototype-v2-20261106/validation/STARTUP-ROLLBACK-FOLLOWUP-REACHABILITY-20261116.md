# STARTUP ROLLBACK FOLLOW-UP AND REACHABILITY CORRECTION — 2026-11-16

## Scope
Follow-up to STARTUP-ROLLBACK-OWNERSHIP-CONTRACT-AUDIT-20261116.md. Read-only source inspection plus execution of the existing Managed Worker close-failure native regression. No source, header, test, build, or Production files were modified. No temporary prototype was created in this pass.

## Important correction: integrated-init failure branch
The earlier audit identified the Worker branch after `raya_reaper_integrated_init(&w->integrated)` as a possible CloseHandle-failure handle-loss path. Static call-path inspection now establishes that this branch is not reachable through the current normal startup call:

- `managed_worker_start_internal` first zeroes the entire Worker object.
- `raya_reaper_integrated_init` rejects only a NULL state pointer, then zeroes the integrated state and calls `raya_reaper_core_start`.
- `raya_reaper_core_start` rejects only NULL or a lifecycle other than UNINITIALIZED.
- Because the integrated state was just zeroed and its pointer is the address of the embedded member, both preconditions are satisfied in this call path.

Therefore the integrated-init failure cleanup branch is defensive/unreachable under the present implementation, absent memory corruption, concurrent misuse, or future changes to initialization semantics. It should not be treated as a currently reproducible startup leak. The raw CloseHandle result handling there remains fragile if reachability changes, but it is not the priority for current regression coverage.

## Reachable Worker startup failure
The thread-factory failure path is reachable through the existing public `raya_managed_worker_start_with_factory` seam. On factory failure:
- the Worker is initialized but not started;
- thread remains NULL;
- if closing wake_event fails, the exact handle is retained;
- the Worker marks HANDLE_CLOSE and DEGRADED while preserving the lock and object.

This is a safer failure shape than the integrated-init branch, but the current public lifecycle APIs do not provide a dedicated cleanup retry for this never-started state. Ordinary Destroy requires joined and is not a valid substitute.

## Existing regression executed
Executed `build-reaper-managed-worker-v2-closefail-native-test.cmd` in the Working Copy using Visual Studio 2026 Build Tools 18.7.3 x64, /W4 /WX /TC.

- 10/10 executions passed.
- 34 assertions per execution; 340 total.
- Confirms a post-start operation finalizer CloseHandle failure retains outstanding ownership, blocks release, and prevents ordinary join/destroy from pretending cleanup succeeded.
- This is not a startup-failure test and does not prove failed-start cleanup retry.

The runner removed its own executable and object outputs. Final output audit found no generated EXE/OBJ from this run. No active compiler, test, linker, or TLC process remained.

## Unchanged source hashes
- reaper-managed-worker-v2.c: FDCD768B31CD6FF30F435A5588D6E1000394A7B355A2E37C1A8117D6C78225E4
- reaper-managed-worker-v2.h: F45DB7509C2045A37925A86BE06F4871109BA5348A383B75447ACD3C46423818
- dedicated-io-submitter-v2.c: D8ABAFD8D3B731DA141E8B1792534A00EB888055D0F15EB0EDFCCE2D01843941
- integrated-submit-adapter-v2.c: 6E2427C09974EE47B382F80CD11B21FEAAC16D12ECABC6A6155A11E9E8F62FBE

## Decision / next bounded work
Prioritize a temporary-copy Worker failed-start cleanup prototype for the reachable thread-factory-failure + wake-event-close-failure case. It must preserve the handle on close failure, serialize retry cleanup, require thread == NULL and no admissions/operations, and permit a fresh start only after cleanup fully succeeds. Then integrate that Worker retry with the already-prototyped Submitter failed-start cleanup at Adapter startup boundary. Keep all prototypes outside the Working Copy source tree until separately authorized.

No commit/reset. Production remains NO-GO; Phase 1 remains NOT ACCEPTED.
