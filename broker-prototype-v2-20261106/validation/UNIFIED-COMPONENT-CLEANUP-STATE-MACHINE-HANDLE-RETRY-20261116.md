# UNIFIED COMPONENT CLEANUP STATE MACHINE — TEMPORARY HANDLE RETRY TEST — 2026-11-16

## Result
A disposable native Win32 C harness exercised one explicit cleanup state machine across four lifecycle failure classes. **PASS: 1,900 assertions, 100 iterations per class.** The Working Copy source/header/test/build files were not changed. No Production change, commit, or reset.

## State machine
Each component instance carries:
- atomic cleanup owner token: 0=available, 1=cleanup owner active, 2=cleanup complete;
- owned HANDLE slots, nulled only after successful CloseHandle;
- lifecycle preconditions (thread absent, counters quiescent; normal-destroy paths additionally require joined and stopped);
- lock/object-live marker and terminal done marker.

Cleanup contract:
1. Acquire exactly one cleanup owner with InterlockedCompareExchange.
2. Reject if already done, not quiescent, thread still present, or normal destroy is not joined+stopped.
3. Walk owned handles in deterministic order.
4. On close failure, retain that exact HANDLE, leave object/lock alive, release owner token for retry, and return failure.
5. On success, null each closed slot.
6. Only after all owned handles are null, mark lock/object cleanup complete and publish terminal DONE.
7. Caller must externally quiesce all possible API callers and preserve object storage throughout cleanup. The helper is not a substitute for a public API lifetime gate.

## Native scenarios
Using real Win32 Event HANDLEs and an injected CloseHandle wrapper:
- Worker failed-start: first wake-event close fails; handle remains valid; retry succeeds.
- Worker normal destroy: joined/stopped preconditions; wake-event close fails once; retry succeeds.
- Submitter failed-start: first event closes, second event close fails; first slot stays NULL, second remains valid; retry closes only the remaining event.
- Submitter normal destroy: wake event is already NULL from an earlier successful close; changed event close fails; retry closes only the remaining event.

Result: 100 iterations × 4 paths × 19 assertions = 1,900 assertions, all passed. Built with Visual Studio 2026 Build Tools 18.7.3 x64, /W4 /WX /TC.

## Important limitations
- This is a native handle/state-machine harness, not an integration build of the actual Worker, Submitter, Adapter or their public APIs.
- The harness models already-quiesced lifecycle states. It does not prove that production callers have drained, that a worker thread has terminated, or that outstanding I/O is safe to release.
- It does not test two simultaneous cleanup callers in this particular run. Prior separate temporary prototypes tested cleanup-owner serialization for Worker failed-start and Submitter integrated gate paths; those results remain separate evidence.
- It does not implement an Orchestrator, change the public component contracts, or resolve the actual API gates found in the preceding audit.
- A failed CloseHandle is treated as preserving ownership of the original handle. The helper never zeroes the object or deletes the critical section on a partial failure.

## Cleanup and integrity
Temporary directory removed: 3 files, 157,710 bytes. No active compiler/linker/test process remained. C free space approximately 12.59 GiB; D approximately 1.97 GiB.
No Working Copy source/header/test/build files were modified. No Production changes, commits, or resets.

## Decision
**State-machine primitive: PASS as an isolated prototype. Orchestrator integration: NOT YET VALIDATED.**
The next required step is to integrate this cleanup contract into disposable copies of the actual Worker and Submitter implementations, with explicit failed-start and post-Destroy cleanup entry points. Then test Adapter/Submitter/Worker ownership ordering and fresh startup after full cleanup. Do not merge this prototype or claim production readiness.
