# Managed Worker Recovery Design Gate — 2026-11-15

Status: design/investigation only. No production-candidate source changed. No recovery API is approved by this report.

## Scope and inspected implementation

Working copy only: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`.

Inspected `reaper-managed-worker-v2.c/.h`, `reaper-core-v2.c/.h`, `reaper-integrated-v2.c/.h`, `reaper-recovery-policy-v2.c/.h`, and `integrated-submit-adapter-v2.c`, plus the existing isolated recovery-policy and native polling tests.

## Confirmed current behavior

1. `managed_worker_main` exits immediately when it observes `RAYA_REAPER_DEGRADED` (returns `ERROR_INVALID_STATE`). A native `WAIT_FAILED` also marks the core degraded and exits the worker thread.
2. `raya_managed_worker_shutdown` accepts ACCEPTING, STOPPING, or STOPPED only. It does not convert DEGRADED into STOPPING.
3. `raya_managed_worker_destroy` requires joined thread, STOPPED lifecycle, zero caller/queued/outstanding/reserved/registry state, and empty worker Retained list. A degraded worker cannot satisfy this through the current public lifecycle.
4. Core causes are sticky OR-ed bits. `raya_recovery_policy_evaluate` labels only non-empty subsets of WAKE_SIGNAL and WAIT_FAILED as polling-eligible. The policy helper is not called by Managed Worker.
5. Worker and Adapter Retained lists have insertion paths, but no corresponding public or internal reconciliation/removal routine was found. Adapter retained envelopes and worker retained operations therefore have no demonstrated safe disposition path.
6. The normal terminal path is staged: exact event/operation resolution, `GetOverlappedResult` on the issuing pipe and OVERLAPPED, terminal hold, finalizer closes handles, finalizer commit, owner release, integrated release, then release callback. Recovery must not bypass or reorder these stages.
7. The isolated native WAIT_FAILED test proves one operation can be found by polling and collected after a real wait failure. It deliberately leaves lifecycle DEGRADED and does not exercise the production-candidate Managed Worker.

## Recovery eligibility is not permission to release

The policy helper's POLLING_ELIGIBLE result can only authorize a restricted observation mode. It cannot authorize:
- admission of new work;
- dropping or rebuilding operation registrations without proving exact identity;
- freeing an operation, buffer, envelope, event, issuing pipe, or OVERLAPPED while I/O ownership is unresolved;
- decrementing outstanding/reserved/retained counters as a substitute for actual reconciliation;
- clearing sticky causes or setting lifecycle to ACCEPTING/STOPPED;
- destroying the Worker while a retained item or uncertain owner remains.

## Proposed design contract — not implemented

### A. Enter recovery quarantine
- Under the Worker lock, atomically close admission and snapshot the sticky cause set.
- Require `raya_recovery_policy_evaluate(causes) == POLLING_ELIGIBLE`.
- Keep all existing reservations and operation registrations intact.
- Do not restart the ordinary worker loop or permit new submissions.

### B. Prove the recovery executor is safe
- The failed worker thread must be joined before another executor touches its registry or operations.
- Confirm there is exactly one recovery executor; no old worker can still mutate shared state.
- The wake event and each registered operation event/issuing-pipe handle must be independently validated. An invalid wait-set is a blocking cause under the current policy and must not be silently reclassified.

### C. Poll and collect, one exact operation at a time
- Snapshot exact `(operation pointer, event handle, issuing pipe, OVERLAPPED)` associations under the shared lock.
- Probe only registered events using zero-time waits.
- If unsignaled, retain everything and continue with bounded/fair rotation; never infer cancellation or completion from a timeout.
- If signaled, claim the exact registered pair and call the configured completion API with the original issuing pipe and OVERLAPPED, `wait=FALSE`.
- `ERROR_IO_INCOMPLETE` returns the operation to pending observation; no release.
- Only classified terminal success/abort may enter the existing terminal-hold/finalizer/owner-release sequence.
- Any uncertain result, ownership mismatch, or close failure adds a blocking sticky cause and stops automatic recovery.

### D. Reconcile Retained envelopes separately
- Worker-retained operation nodes and Adapter-retained envelopes are distinct ownership layers and both must be accounted for.
- A retained item may be removed only after its exact ownership state, finalizer state, submitter acknowledgement, callback disposition, and both list memberships/counters are reconciled.
- A failed submitter acknowledgement or callback-side invariant must remain retained; do not invoke a callback twice.
- Implement list unlink only with validated membership and count invariants. No blanket list clear/reset.

### E. Close or remain quarantined
- Successful polling/collection alone does not clear DEGRADED.
- A future explicit recovery-commit gate must verify: no kernel-pending I/O; all terminal results collected; all finalizers committed; owners released; both Retained lists empty; submitter requests acknowledged exactly once; registry/queue/reservation/outstanding/caller counts consistent; worker thread joined; wait infrastructure rebuilt and independently validated.
- Only after an audited commit may a dedicated state transition be considered. Whether to resume service or require destroy/recreate is an unresolved architecture decision; no transition to ACCEPTING is currently justified.

## Blocking conditions

Any `COMPLETION_UNCERTAIN`, `OWNERSHIP_INVARIANT`, `HANDLE_CLOSE`, `WAITSET_INVALID`, unknown cause bit, empty cause set, failed thread join, ambiguous operation/event mapping, unresolved pending I/O, or Retained-list inconsistency blocks automatic recovery. Mixed cause sets remain blocked even if a polling-eligible cause is also present.

## Required tests before implementation approval

1. WAKE_SIGNAL-only and WAIT_FAILED-only Worker integration, with one and multiple real overlapped operations.
2. Mixed eligible + blocking cause remains quarantined; causes cannot be cleared by recovery.
3. Worker join race and proof no old executor remains active before polling begins.
4. Polling fairness/rotation across full registry capacity, event unsignaled, `ERROR_IO_INCOMPLETE`, terminal success, terminal abort, and disconnect.
5. Finalizer close failure and owner-release failure retain all affected objects and block commit.
6. Adapter + Worker dual-list membership, submitter acknowledgement failure, and exactly-once callback semantics.
7. Shutdown racing with handoff, completion, and recovery admission closure.
8. Recovery-commit negative controls: each nonzero counter/list entry, unknown cause, or invalid wait infrastructure rejects commit.
9. Repeated randomized stress with process-level cleanup checks and no leaked test outputs.

## Decision

Do not add a generic `promote while degraded`, lifecycle reset, retained-list clear, or worker restart. First implement and independently test an explicit quarantine + polling executor + exact reconciliation contract. Keep the current fail-closed behavior until all gates pass.

Production remains NO-GO; Phase 1 remains NOT ACCEPTED.

## Addendum — Acknowledge commit vs signal failure and post-terminal Retained (2026-11-15)

Further inspection of `dedicated-io-submitter-v2.c`, `dedicated-io-submitter-v2.h`, `dedicated-io-submitter-v2-failure-test.c`, and `integrated-submit-adapter-v2-api-test.c` found a critical split outcome:

- `raya_submitter_acknowledge` changes request state from `ISSUED` to `ACKED` and decrements `issuer_leases` before it signals `changed_event` and `wake_event`.
- If either signal fails, the function returns 0 and degrades the Submitter even though the acknowledgement is already committed (`request.state == RAYA_SUBMITTER_REQ_ACKED`, `issuer_leases == 0`).
- The public header already documents this contract: return 0 can mean ACK state is terminal but lifecycle signaling failed; caller must retain request storage.
- The integrated adapter release callback treats any 0 return as a reason to retain the envelope and suppress the completion callback. It must not call acknowledge a second time: ACKED is already terminal and the retry would return 0 without repairing the failed signal.
- `retain_envelope` then adds the operation to the Worker Retained list when `worker_terminal_released` is true. In this case the Worker list node is a post-terminal bookkeeping hold: owner and finalizer are RELEASED, core outstanding is 0, and reserved slots are 0. It is not pending kernel I/O, but still cannot be freed because Adapter callback/ack disposition is unresolved.
- The existing integrated ACK signal failure test exercises both `changed_event` and `wake_event` failure injection, asserts callback count remains 0, Adapter retained count 1, Worker retained count 1, released owner/finalizer, outstanding/reserved 0, and degraded Submitter. The dedicated Submitter failure test independently asserts `ACKED` plus zero issuer leases after a false return.

### Required recovery model refinement

Represent ACK outcome as at least three distinct facts, not one Boolean:

1. ACK not committed (request remains ISSUED/otherwise unresolved);
2. ACK committed but notification failed (request is ACKED; repair/re-signal is a separate operation);
3. ACK committed and notification succeeded.

A future repair path must inspect request state under the Submitter lock and use an explicit idempotent notification-repair operation. It must not re-acknowledge an ACKED request. The Adapter envelope also needs an exactly-once callback-disposition state (e.g. pending/claimed/delivered) guarded against concurrent recovery; callback execution must occur outside locks, and a claimed-but-interrupted callback cannot be safely replayed without a stronger delivery contract.

The two Retained lists should distinguish operation-lifetime retention from post-terminal callback/ack bookkeeping. A post-terminal node may be unlinked only after ACK is committed, notification state is reconciled, callback delivery is conclusively accounted for, and Adapter/Worker list membership and counters agree. A pending-I/O retained node must continue to obey the original I/O/finalizer/owner gates.

### Regression execution

- `build-dedicated-io-submitter-v2-failure-test.cmd`: 25/25 runs passed, 164 assertions per run, 4,100 total assertions. This includes the ACK-signal-failure state assertion.
- Adapter API regression was not rerun in this continuation: its build script shares `reaper-recovery-policy-v2.obj`, which was already present before the run. To avoid overwriting or deleting an unrelated pre-existing object, the build was refused. Prior 10x API regression evidence remains the only integrated ACK-failure execution evidence cited here.
- Dedicated Submitter test outputs were absent before build and absent after cleanup. No test/compiler process remained.
- Pre-existing `reaper-recovery-policy-v2.obj` was preserved; SHA256 `D9BF8DAEA81446B7E72BE4963CC252DA57CBD1523D8338F9535A50D346BD6ECE`.

Decision remains design-only. No production-candidate source or test source changed. Do not implement generic Retained unlink, duplicate ACK, or callback replay until the state machine above is separately designed and tested.

## Addendum 2 — Retained reconciliation and callback delivery contract (2026-11-15)

### Source-level findings

- Adapter envelope currently stores `retained`, `retained_next`, `worker_terminal_released`, `worker_owns_operation`, and callback/context. It has no explicit ACK-commit/notification state and no callback disposition state.
- `retain_envelope` inserts the envelope into Adapter list once using `e->retained`; it separately inserts the embedded operation into Worker list only when that insertion is new and `(!worker_owns_operation || worker_terminal_released)`. The two list links and counters are independent, but the envelope has no explicit Worker-membership flag. The current function has no unlink/removal path.
- Adapter release callback calls `raya_submitter_acknowledge`; on false it retains and returns without invoking completion. On true it invokes the void callback and immediately frees the envelope.
- Worker API's release callback contract says it is called exactly once, outside the Worker lock, after ownership commits, and may free the operation; Worker will not touch it afterward.
- There is no callback return status, idempotency token, durable callback receipt, or recovery-safe claimed/completed transition in this adapter API. A `void` callback cannot report whether its external effect was committed.

### Exactly-once conclusion

An in-memory `PENDING -> CLAIMED -> DELIVERED` state can prevent concurrent duplicate invocation while the process remains alive, if the claim is atomic and only one recovery executor is permitted. It cannot by itself guarantee exactly-once external effects across a crash or ambiguous interruption:

- Mark CLAIMED before invoking: a crash after claim but before the effect can lose delivery (at-most-once).
- Mark DELIVERED after invoking: a crash after the effect but before the mark can cause replay and duplicate effects (at-least-once).

Exactly-once effect requires a stronger consumer contract, such as a stable operation idempotency key and durable deduplication/transaction at the callback recipient, or a transactional outbox/inbox protocol. Otherwise the product must explicitly choose at-most-once or at-least-once semantics. Do not label a local CAS state as end-to-end exactly-once.

### Reconciliation prerequisites

Before any unlink/free operation, a validator must independently prove:

1. Adapter list traversal count equals `adapter.retained_count`, has no cycle/duplicate envelope, and each node's membership state agrees.
2. Worker list traversal count equals `worker.retained_count`, has no cycle/duplicate operation, and each node maps to exactly one Adapter envelope where applicable.
3. Cross-list relation is valid for the node's retention class: pending-I/O retention still owns unresolved I/O/handles/buffer; post-terminal retention has committed finalizer/owner/integrated release and zero outstanding/reserved ownership counts.
4. Submitter request is conclusively ACKED (not merely `acknowledge()` returning true/false); notification repair is separately confirmed; no issuer lease remains.
5. Callback disposition is resolvable under the chosen delivery contract and no other executor has claimed it.
6. Only then may both list links/counters be updated under a defined lock order, followed by storage release outside locks.

Current structures do not encode enough explicit state to perform this validator safely. In particular, `retained` is a single Adapter membership flag and there is no separate `worker_retained` membership bit or public traversal/validation API. Existing diagnostic counts alone cannot prove cross-list consistency. No list mutation API should be added until lock order, membership ownership, and failure atomicity are specified.

### Isolated regression execution

The existing Adapter API suite was rebuilt in a fresh temporary directory using copies of the exact working-copy source/header inputs. The first isolated build attempt exposed one omitted header (`managed-pipe-io-v2.h`) in the temporary input set and did not run tests; the temporary directory was retained, the missing project headers were copied, and the suite was rerun successfully.

- 10/10 executions passed.
- 724 assertions per execution; 66 cases per execution.
- Total 7,240 assertions.
- The suite includes the integrated ACK-signal-failure path, which verifies both Retained lists contain the post-terminal envelope/operation, owner and finalizer are RELEASED, outstanding/reserved counts are zero, callback count remains zero, and Submitter is degraded.
- No new source or test files were changed. Isolated build outputs were cleaned by the runner; the temporary source copy was removed after confirming no `.obj`, `.exe`, or `.pdb` outputs and no active compiler/test processes.
- Pre-existing working-copy `reaper-recovery-policy-v2.obj` remained untouched with SHA256 `D9BF8DAEA81446B7E72BE4963CC252DA57CBD1523D8338F9535A50D346BD6ECE`.

### Gate decision

No Retained unlink, notification repair, callback replay, or recovery implementation is safe to add yet. Next engineering task is a written callback delivery contract (at-most-once or idempotent at-least-once with consumer dedupe), followed by explicit state/membership fields and read-only list-integrity tests. Only after those pass should a narrowly scoped reconciliation operation be considered. Pending-I/O Retained must remain non-releasable until exact completion is established.
