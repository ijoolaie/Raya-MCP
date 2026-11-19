# STARTUP ORCHESTRATOR — REAL API LIFECYCLE GATE AUDIT — 2026-11-16

## Scope
Read-only audit of the actual Working Copy APIs after the temporary ownership-policy harness passed 24 assertions. This determines whether an Orchestrator can safely implement retryable startup/teardown using the current component contracts. No source, header, test, build, or Production file was changed.

## Confirmed ownership
- `raya_integrated_adapter_start_with_apis` requires an already initialized and started Worker and an uninitialized Submitter.
- Adapter stores borrowed pointers to both components. Header explicitly says both outlive Adapter.
- Adapter startup initializes its own lock/state, then starts Submitter. If Submitter startup fails, Adapter zeroes itself; it does not own or clean Worker.
- Adapter destroy closes Adapter admission and destroys only Adapter state. It does not shut down Submitter or Worker.
- Therefore Orchestrator must explicitly track whether it owns Worker; Adapter itself must remain non-owning.

## Actual lifecycle retry gates
### Worker
- Normal Join preserves thread handle if wait/GetExitCode/CloseHandle fails; retry is possible while `joined` remains false.
- Worker Destroy requires joined + STOPPED + all queues/registry/retained state empty. If closing wake_event fails, it leaves wake_event and initialized state intact and returns false; another Destroy attempt can retry because its preconditions remain true.
- Failed Worker thread creation is different: startup attempts CloseHandle directly. On failure it marks HANDLE_CLOSE degradation and leaves initialized Worker, wake_event and lock alive, but started=false/thread=NULL. There is no public failed-start cleanup API in the current header. Ordinary Destroy requires joined, which cannot be reached through normal Join because started=false.
- Thus Owned Worker startup rollback after a failed thread creation is not generally implementable through public APIs alone. A dedicated failed-start cleanup operation (or equivalent internal orchestrator-only API) is required.

### Submitter
- Join preserves thread handle on wait/GetExitCode/CloseHandle failure; retry is possible.
- Destroy requires joined and lifecycle STOPPED. It closes wake_event, nulls it, then closes changed_event. If closing changed_event fails, it marks lifecycle DEGRADED and returns false while preserving changed_event.
- A second call to the current Destroy fails its STOPPED precondition because lifecycle is now DEGRADED. The remaining handle is retained, but the public API has no cleanup-retry path for this partial-Destroy state.
- The temporary failed-start cleanup helper previously prototyped only covered startup failure (not started/no thread); it does not resolve this normal post-Join partial-Destroy gate.
- Submitter failed-start cleanup and normal-destroy cleanup therefore need distinct, explicit preconditioned retry paths, or one shared cleanup state machine that safely covers both.

### Adapter
- Adapter Destroy permanently closes admission before checking retained/active state. If not yet safe, it returns false but retains the closing bit; retry is possible after work drains. Caller must still quiesce all potential API callers and preserve object storage.

## Orchestrator implications
The intended ordering remains:
1. Adapter admission close/destroy.
2. Submitter shutdown, join, destroy/cleanup-retry.
3. Owned Worker shutdown, join, destroy/cleanup-retry.
Borrowed Worker is never stopped or destroyed by Orchestrator.

But current public APIs do not provide all required cleanup transitions. In particular:
- Worker failed-start + failed wake-event close is stranded without a dedicated cleanup API.
- Submitter post-Join partial Destroy can become DEGRADED and then cannot retry through Destroy.
- Orchestrator must not paper over either case by memset, deleting a critical section, or manually closing component-owned handles outside an explicit component cleanup contract.

## Result / decision
**Integration gate: BLOCKED — no orchestrator implementation against public APIs yet.**
The 24-assertion temporary ownership-policy harness validates intended ownership/order decisions only; it is not evidence that current APIs can complete every rollback path.

Next safe step is to design a unified, explicit component cleanup state machine (Worker failed-start, Submitter failed-start, Submitter partial Destroy, Worker partial Destroy) and test it in a disposable copy. It must serialize cleanup ownership, preserve failed handles, keep API admission closed, and require external object-storage lifetime/quiescence. No Production change or merge recommendation.

## Integrity
Working Copy source hashes checked unchanged:
- `dedicated-io-submitter-v2.c`: `D8ABAFD8D3B731DA141E8B1792534A00EB888055D0F15EB0EDFCCE2D01843941`
- `reaper-managed-worker-v2.c`: `FDCD768B31CD6FF30F435A5588D6E1000394A7B355A2E37C1A8117D6C78225E4`
- `integrated-submit-adapter-v2.c`: `6E2427C09974EE47B382F80CD11B21FEAAC16D12ECABC6A6155A11E9E8F62FBE`
- C free space after audit: approximately 12.59 GiB; D: approximately 1.97 GiB.
- No commits/resets, no Production/source changes, no active build/test/TLC process from this audit.
