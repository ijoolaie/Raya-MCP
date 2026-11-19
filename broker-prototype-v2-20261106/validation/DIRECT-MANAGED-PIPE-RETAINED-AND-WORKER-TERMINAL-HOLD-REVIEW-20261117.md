# Direct Managed-Pipe Retention and Worker Terminal-Hold Review — 2026-11-17

Status: read-only source review. Working Copy only.
Production: NO-GO. Phase 1: NOT ACCEPTED.

## Executive findings

The direct `raya_managed_pipe_submit` path has its own Worker retained-list insertion logic, independent of Adapter envelopes.

There are two broad classes:
1. Caller-side retention before successful handoff, where the operation is linked to `worker.retained_head`.
2. Worker-thread terminal holds after handoff, where the Worker exits degraded while the operation remains in core queue/registry/outstanding accounting. These failures do not necessarily link the operation to `worker.retained_head`.

Therefore `worker.retained_count` is not a complete inventory of every unresolved Worker-owned operation. A zero retained-list count does not prove there is no unresolved operation. Worker Destroy separately checks lifecycle, outstanding/queued/core list, reserved slots, registry count and retained list.

## 1. Direct caller-side insertion paths in managed-pipe-io-v2.c

### A. DuplicateHandle failure followed by Event close failure
- Admission has been acquired.
- Event exists; pipe duplication failed.
- Event close callback fails, so the operation is initialized enough to retain the Event in the finalizer record, with issuing pipe invalid.
- Operation, buffer and callback context are retained.
- No ReadFile/WriteFile was issued.
- Recovery is a local Event-handle ownership problem; no I/O replay is relevant.

### B. Operation initialization failure
- Duplicate pipe and Event exist.
- The code stores the handles, OVERLAPPED event, release callback and callback context into the operation.
- If Finalizer initialization succeeds, retain cause is ownership invariant; otherwise retain cause is handle close.
- No ReadFile/WriteFile was issued.
- Buffer and callback context remain allocated.

### C. Pre-I/O discard cleanup failures
`discard_unissued` closes finalizer handles, commits release, and then frees buffer/context/operation. If close or commit fails it calls `retain_operation`.
- Used after Worker reservation failure.
- No I/O was issued.
- If admission was already taken, it attempts Worker finish; failure itself can degrade Worker.
- A failed close is not permission to manually close/free the same resource.

### D. Caller-terminal cleanup failures
`finish_caller_terminal` can retain after:
- Pipe Owner says resources may not be freed;
- integrated caller-terminal transition fails;
- Finalizer close fails;
- Finalizer release commit fails;
- Pipe Owner release fails;
- integrated unreserve fails;
- caller-finish accounting fails.

These branches occur after a terminal caller-side observation, but their I/O phase differs:
- Immediate ReadFile/WriteFile TRUE is already terminal at the API boundary.
- Immediate non-pending native failure is not accepted as pending I/O and enters submission-failed terminal cleanup.
- Exact event + exact OVERLAPPED + GetOverlappedResult terminal result is terminal.
No callback is issued on a retained result.

If finalizer/owner cleanup has advanced, do not repeat earlier close/release steps blindly. The state objects encode partial progress and must govern any future retry.

### E. Handoff failure after pending I/O
- `ReadFile`/`WriteFile` returned FALSE with `ERROR_IO_PENDING`.
- Caller wait/drain did not establish terminal completion.
- Handoff result TRANSFERRED returns HANDED_OFF.
- TRANSFERRED_DEGRADED returns RETAINED; Worker owns the operation but is degraded.
- Any other handoff result is retained through `retain_operation`.
- In the last case the operation may still have a kernel-visible pending OVERLAPPED I/O. It must not be freed or reissued.

## 2. Worker-thread failures after successful handoff are a different hold

The Worker thread's `collect_operation` path can stop on:
- event wait invalid/failure;
- completion classifier returns UNCERTAIN;
- owner reaper collection fails;
- integrated terminal-hold transition fails;
- finalizer close/commit fails;
- Pipe Owner release fails;
- integrated registry release fails.

For these failures the code marks Worker degraded and returns an error from the Worker thread. It does not generally call the caller-side `retain_operation` helper and does not necessarily link the operation into `worker.retained_head`.

Instead, the operation remains protected by one or more of:
- core outstanding/queued/head/tail state;
- integrated reservation;
- integrated registry entry (depending on failure point);
- nonterminal owner/finalizer state.

This is a terminal hold, not a retained-list node. The worker thread exits abnormally/degraded; Join may reap the thread handle, but Worker Destroy rejects because lifecycle is not STOPPED and/or outstanding/reservation/registry state remains.

A specific close-failure native test verifies:
- lifecycle DEGRADED;
- outstanding = 1;
- reserved_slots = 1;
- registry count = 0 at that failure point;
- finalizer CLOSE_UNCERTAIN, zero successful closes and zero release commits;
- release callback count = 0;
- Join returns failure but marks joined;
- Destroy refuses.

This proves that registry count zero alone is not sufficient to reclaim the operation.

## 3. Ownership and resource matrix

| State | OS I/O phase | Protected resources | Callback |
|---|---|---|---|
| Duplicate Event close failure | Pre-I/O | Event, operation, callback context, buffer | Suppressed |
| Init failure retained | Pre-I/O | Duplicated pipe/Event as recorded, operation, buffer, callback context | Suppressed |
| discard_unissued cleanup failure | Pre-I/O | Remaining Finalizer-owned handles and operation state | Suppressed |
| finish_caller_terminal cleanup failure | Immediate or exact terminal result already known | Any not-yet-released handles, owner/finalizer state, reservation/accounting | Suppressed |
| pending handoff failure | OS accepted pending I/O; terminal result not established | issuing pipe, Event, OVERLAPPED, buffer, operation, owner/finalizer | Suppressed |
| Worker terminal hold | Worker accepted ownership; completion may be uncertain or terminal with cleanup failure | core/registry/reservation and remaining operation resources | Suppressed |
| Normal Worker release | Exact terminal result + finalizer/owner/registry release succeeded | Release callback receives operation once | Invoked once outside Worker lock |

## 4. Inventory observability conclusions

- Direct managed-pipe operations have no Adapter envelope.
- Worker retained list stores operation pointers only; no stable operation ID or typed cause per node.
- Worker public API has no production enumeration/reconciliation/release operation for retained list nodes.
- Worker Destroy checks both retained list and independent core/registry/reservation/accounting invariants.
- A diagnostic relying only on `retained_count` misses terminal holds still present in core state.
- No automatic retry/unlink/free of retained-list nodes was found.
- No production operation can safely turn a degraded terminal hold into STOPPED.

## 5. Safe recovery implications

1. Inventory must represent at least three Worker states: retained-list node, core terminal hold, and healthy in-flight queue/registry operation.
2. Snapshot must report core outstanding/queued/head/tail, reservation count, registry count, retained list count, lifecycle/degradation cause, operation stage, owner/finalizer state.
3. Stable operation identity must be assigned before admission/Issue; raw pointers are diagnostic only.
4. A pending I/O handoff failure is never safe to replay without exact terminal evidence and a higher-level idempotency contract.
5. A finalizer close retry is only a local cleanup action and must use the finalizer's per-handle progress; it does not establish recipient outcome.
6. Do not equate Worker registry count zero with operation release; core outstanding and reservation may still protect the object.
7. Worker degraded state currently has no generic owner-only recovery path; keep it quarantined.
8. Orchestrator integration needs Worker-specific cleanup cursor steps and evidence-backed reconciliation before Context/Registry reclamation.

## 6. Verification and scope

- Inspected `managed-pipe-io-v2.c/.h`, `managed-pipe-io-v2-native-test.c`, `reaper-managed-worker-v2.c/.h`, and `reaper-managed-worker-v2-pipeclosefail-native-test.c`.
- Existing native test assertions were inspected, not re-run.
- No source/header/test/config/README changed.
- No build/test run, process termination, install/download, or temporary artifact operation.

## 7. Source SHA-256

| File | SHA-256 |
|---|---|
| managed-pipe-io-v2.c | 2FDEF399F1491C613F6E7A8593A4C4117483D2FBB4E67AC3A56559332CF66891 |
| managed-pipe-io-v2.h | 558CCEE826FAB783A0F1A60F44702E6E62FA18422AFBA7B6AA5C209180E0CE5A |
| managed-pipe-io-v2-native-test.c | 89C5010A9B6187F2834E2A955C79590AF29F934F857B1860D485F177B73CE5D3 |
| reaper-managed-worker-v2.c | 5B0B43BA60303A0EC3EA07A9E26A641E32E40812702C0F87EC37C1ADD11DD8DD |
| reaper-managed-worker-v2.h | 5D0230BC5295AF2DFE8B8BC4B64E0E7D1938F977A446F0C785CEA0C2E0918F96 |
| reaper-managed-worker-v2-pipeclosefail-native-test.c | 8E9D292B213328EF0DAD74DAA2F304A0DE88AAEDFE3D512FAE980C66523FEC36 |
