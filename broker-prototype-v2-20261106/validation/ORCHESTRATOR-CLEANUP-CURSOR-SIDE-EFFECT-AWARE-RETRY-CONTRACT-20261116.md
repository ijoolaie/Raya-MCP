# Orchestrator Cleanup Cursor — Side-Effect-Aware Retry Contract (2026-11-16)

Status: read-only source audit + design report. No implementation, build, or new test run. Working-copy source unchanged.

## Executive decision

A plain integer cursor plus Boolean component API results is insufficient for a safe teardown controller.

The actual Submitter and Worker Join APIs can close the thread HANDLE and set joined=TRUE, then return FALSE because the thread exit code was nonzero. A caller that treats FALSE as "Join did not happen" will retry Join, but the API rejects the retry because joined is already TRUE. The resource side effect is complete while the component outcome is unsuccessful.

Submitter Destroy has a second concrete trap: it can successfully close and null wake_event, fail closing changed_event, mark lifecycle DEGRADED, and return FALSE. The public Destroy retry requires lifecycle STOPPED, so the retry path is stranded. A temporary component-only prototype previously demonstrated a repair branch, but that repair is not in checked-in Working Copy source.

Therefore the orchestrator must persist per-substep effect state and component outcome separately. It must not infer whether an operation took effect from its Boolean return alone.

## Direct source evidence

### Adapter Destroy
- Atomically sets the high closing bit; new entrants are rejected.
- Returns FALSE if entrant count is nonzero or retained/active work remains.
- A later call can retry after drain.
- On success deletes its lock and zeroes the Adapter.
- Does not stop/destroy Submitter or Worker.
- Therefore the cursor must not advance until success; after success the Adapter storage is no longer a valid component object and must not be touched again.

### Submitter Shutdown
- RUNNING transitions to STOPPING.
- Signals wake_event and changed_event while holding Submitter lock.
- If signaling fails, it marks lifecycle DEGRADED and returns FALSE.
- The ordinary retry path only treats STOPPED as success or STOPPING as a signalable state; DEGRADED does not become a clean STOPPING retry.
- A failed signal may leave the worker thread waiting or the lifecycle degraded. Do not infer shutdown completion from the FALSE result.

### Submitter Join
- Waits for thread; obtains exit code; closes thread HANDLE; sets thread=NULL and joined=TRUE.
- Returns code == ERROR_SUCCESS after those effects.
- Thus FALSE can mean Join's resource effects completed, but worker exit outcome was abnormal. Retrying Join is invalid.
- Persist at least thread_handle_closed, joined, exit_code_known, and exit_code; keep component_outcome=ABNORMAL_EXIT separate from the substep effect.

### Submitter Destroy
- Requires initialized + joined + STOPPED + empty queue/counters/leases.
- Closes wake_event and nulls it; then closes changed_event and nulls it.
- If first close fails, it returns FALSE without nulling that handle.
- If second close fails, wake_event is already NULL, changed_event remains live, lifecycle becomes DEGRADED, and it returns FALSE.
- Current checked-in public Destroy rejects the resulting DEGRADED partial state because it requires STOPPED.
- Do not advance the cursor; do not retry the unchanged public API and claim this is recoverable. It requires a narrowly-scoped, serialized post-Join partial-destroy transition, as proven only in prior disposable prototypes.

### Worker Shutdown
- ACCEPTING transitions to STOPPING.
- Signals wake_event; signal failure marks a recovery cause and returns FALSE.
- Worker may enter DEGRADED or fail to observe shutdown. No generic safe retry from DEGRADED is established by this API.
- Retain the Worker and its resources; do not proceed to Join as though orderly stop were proven unless a separate contract proves the thread has exited.

### Worker Join
- Waits for thread; obtains exit code; closes thread HANDLE; sets thread=NULL and joined=TRUE.
- Returns FALSE if exit code is not ERROR_SUCCESS, after the join effects have happened.
- Same side-effect/result split as Submitter Join.
- If abnormal exit means Worker is DEGRADED rather than STOPPED, Destroy's STOPPED precondition still blocks cleanup. Join completion is not proof that Worker resources can be destroyed.

### Worker Destroy
- Requires joined, STOPPED, zero callers/outstanding/queued/reserved/registry/retained work.
- CloseHandle failure leaves wake_event intact and returns FALSE; retry can be valid after rechecking state.
- Success nulls wake_event, deletes lock and marks uninitialized.
- It has no checked-in close callback for this direct CloseHandle; prior integrated fault-injection run patched only a disposable copy.

## Required cursor data model

Do not use only unsigned step. Persist a record per resource action:

- effect: NOT_ATTEMPTED, IN_PROGRESS, EFFECT_COMMITTED, EFFECT_UNKNOWN, BLOCKED.
- outcome: NOT_EVALUATED, SUCCESS, ABNORMAL_EXIT, RETRYABLE_FAILURE, TERMINAL_FAILURE, UNCERTAIN.
- attempt_count and last Win32 error / exit code where meaningful.
- ownership mode: OWNED or BORROWED.
- cleanup owner token and Orchestrator generation.
- current step and per-step substate.

EFFECT_COMMITTED means the destructive side effect is known to have happened (e.g. thread HANDLE closed), not necessarily that the component reached a clean terminal lifecycle. EFFECT_UNKNOWN must block retries and object free until reconciled. BLOCKED means a known invariant or unsupported lifecycle transition prevents progress; it is not a transient retry signal.

## Ordered teardown graph

For the current implementation's actual ownership behavior:

1. Close public Orchestrator admission and acquire exclusive cleanup owner.
2. Drain all public API leases. If not drained, return BUSY; do not touch components.
3. Adapter Destroy (Adapter borrows Submitter and Worker). Retry only while Adapter remains initialized and destroy preconditions are not yet satisfied. Once success is observed, mark Adapter effect committed and never dereference it again.
4. Submitter Shutdown.
5. Submitter Join.
6. Submitter Destroy, including its separate wake_event and changed_event close effects.
7. If Worker is OWNED: Worker Shutdown.
8. If Worker is OWNED: Worker Join.
9. If Worker is OWNED: Worker Destroy.
10. If Worker is BORROWED: record SKIPPED_BY_OWNERSHIP_POLICY for all Worker teardown actions; do not call Worker Shutdown, Join, or Destroy.
11. Unregister token only after all owned resources are terminal, no leases remain, and the internal Orchestrator object can be freed.

The seven-step prototype cursor (Adapter; Submitter shutdown/join/destroy; Worker shutdown/join/destroy) is a useful outline, but the actual durable state needs substeps and effect markers, especially inside Join and Destroy.

## Transition rules

- Advance past a substep only when its postcondition is proven, not merely because the API returned TRUE.
- For Join, independently inspect/persist joined, thread==NULL and captured exit code under an API that guarantees safe access. If joined is true but exit code is abnormal, mark Join EFFECT_COMMITTED + outcome ABNORMAL_EXIT and stop at a recovery/diagnostic gate; never call Join again.
- For each handle close, record which exact handle was closed and nulled. A failure on the second handle must not cause the first handle to be closed again.
- If a component enters DEGRADED, do not map that to a generic retry. Require an explicit component recovery/partial-cleanup transition with exact accepted preconditions.
- Stop the chain at the first unresolved or blocked component. Do not destroy downstream owned Worker while Submitter cleanup is unresolved unless ownership/dependency analysis explicitly proves it safe.
- Keep the Orchestrator handle slot CLOSING and preserve the cursor after failure. Ordinary facade calls remain rejected. Only authorized retry/diagnostic calls may access it.
- Release cleanup owner on return only if cursor and state are persisted atomically; the next retry must reacquire exclusive ownership and resume from that state.
- No component calls or waits under the public handle registry lock.

## Ownership matrix

| Component | OWNED | BORROWED |
|---|---|---|
| Adapter | Orchestrator-created; destroy after API leases drain | If future API supports borrowed Adapter, never destroy it |
| Submitter | Current Adapter start path initializes it; Orchestrator must own its cleanup through Adapter's private contract | If externally supplied, Adapter must not stop/destroy it |
| Worker | Shutdown → Join → Destroy after Adapter and Submitter are gone | Never shutdown, join, or destroy; caller guarantees it outlives Adapter |

Important: current Adapter header says it borrows an already-started Submitter, but actual implementation initializes Submitter during Adapter start. This contract mismatch must be resolved before implementing ownership flags. Do not infer ownership from the current comment alone.

## Mandatory blockers before a real chained cleanup test

1. Implement and test Submitter post-Join partial Destroy retry in the actual integration branch, with a cleanup-owner gate; the existing passing reports are temporary source-copy prototypes only.
2. Define what happens when Submitter/Worker thread exit code is nonzero after the thread HANDLE has been closed. The current APIs expose a Boolean that loses the distinction to callers; add a structured result or a safe diagnostic state.
3. Define recoverability for Submitter/Worker DEGRADED shutdown. Do not invent a reset to STOPPING or STOPPED.
4. Resolve Adapter's Submitter ownership contract.
5. Add deterministic per-handle close injection for both Submitter and Worker without conflating thread-handle close in Join with event-handle close in Destroy.
6. Integrate scalar Stable Handle registry, API leases, close owner and durable cursor.
7. Only then run an actual three-component failure matrix with repeated failures at every substep, Owned/Borrowed paths, concurrent close callers, and submit/query racing close.

## Acceptance assertions for the future harness

- A Join whose thread exit code is abnormal closes its thread HANDLE exactly once; cursor records joined + abnormal exit; no second Join call occurs.
- A Submitter changed_event close failure records wake_event closed, changed_event still open, and refuses unsafe retry until the explicit partial-state cleanup API is available.
- Repeated changed_event close failures preserve the remaining handle and retry only that handle.
- Worker wake_event close failure retries only Worker Destroy; earlier Adapter/Submitter/Worker shutdown/join steps are not repeated.
- A Shutdown wake signal failure is not counted as clean shutdown and does not authorize object free.
- Borrowed Worker receives zero teardown calls in every failure/retry path.
- A failure at every cursor boundary leaves all earlier committed effects unchanged.
- Two concurrent cleanup callers never execute the same component substep concurrently.
- Ordinary API admission remains closed after first BeginClose, including after failed cleanup.
- Registry generation changes only after all owned resources and object storage are fully retired.
- A stale handle rejects after slot reuse.
- No source object is freed while any API lease, callback, worker thread, retained I/O, or uncertain ownership remains.

## Verification and scope

Performed in this step:
- Fresh C:/D: free-space check and relevant process inventory.
- Read-only inspection of actual Submitter Shutdown/Join/Destroy, Worker Shutdown/Join/Destroy, and Adapter Destroy bodies.
- Compared those bodies with prior disposable cursor and cleanup retry evidence.
- No source edits, builds, new tests, process termination, temp files, archive changes, Production changes, commits, or resets.

Relevant existing evidence (not rerun):
- ORCHESTRATOR-WORKER-DESTROY-FAILURE-CURSOR-RETRY-20261116.md: actual components in a temp copy, Worker Destroy failure/retry, 25 runs / 375 checks; Owned path only.
- SUBMITTER-PARTIAL-DESTROY-RETRY-OWNER-GATE-20261116.md: temporary Submitter patch, 100 runs / 1,000 checks; not integrated into checked-in source.
- ORCHESTRATOR-CONCURRENT-REAL-API-WORKER-RETRY-20261116.md: temporary orchestrator wrapper with actual APIs and patched Worker close seam; 50 runs / 750 checks; does not cover Submitter partial destroy.
- ORCHESTRATOR-CHAINED-CLEANUP-FAILURE-GATE-AUDIT-20261116.md: current integration gate blocked.

## Working-copy hashes at audit time

- integrated-submit-adapter-v2.c: 6E2427C09974EE47B382F80CD11B21FEAAC16D12ECABC6A6155A11E9E8F62FBE
- dedicated-io-submitter-v2.c: D8ABAFD8D3B731DA141E8B1792534A00EB888055D0F15EB0EDFCCE2D01843941
- reaper-managed-worker-v2.c: FDCD768B31CD6FF30F435A5588D6D1000394A7B355A2E37C1A8117D6C78225E4
- integrated-submit-adapter-v2.h: 4E2CC45DCD9E4EA0EA47441158B40F25CCD900095182CD256A90C285FB8D4882
- dedicated-io-submitter-v2.h: 2D4CBB91D3400AC5416FC1DE25D03012AC2D2644D6F848A42D278F38D5191655
- reaper-managed-worker-v2.h: F45DB7509C2045A37925A86BE06F4871109BA5348A383B75447ACD3C46423818
