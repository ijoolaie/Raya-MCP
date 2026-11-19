# Callback Disposition and Retained Integrity Contract - 2026-11-15

Status: design gate only. No source, test, Production, or model files changed.

## Scope

Working copy only: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`.
Inspected `integrated-submit-adapter-v2.c/.h`, `managed-pipe-io-v2.h`, `dedicated-io-submitter-v2.c/.h`, and the ACK-signal-failure cases in `integrated-submit-adapter-v2-api-test.c`.

## Current facts

1. `raya_adapter_envelope` has one Adapter membership flag (`retained`) and one Adapter link (`retained_next`), but no explicit Worker-list membership bit, ACK disposition, notification disposition, or callback disposition.
2. `retain_envelope` inserts the envelope into the Adapter list once. It inserts the embedded operation into the Worker list only on that first insertion and only if `!worker_owns_operation || worker_terminal_released`. These are two separate list mutations protected by different locks; there is no unlink path.
3. In `adapter_worker_release`, `worker_terminal_released` is set before Submitter acknowledgement. If acknowledgement returns false, the envelope is retained and callback is suppressed. In the ACK-signal failure case, the Submitter may already be ACKED with zero issuer leases even though notification failed.
4. On successful acknowledgement, the `void` callback is invoked outside the Worker lock and the envelope is freed immediately after callback return.
5. The public callback contract only says data is valid during callback. It does not define retry, duplicate invocation, durable receipt, or crash recovery semantics.
6. Existing integrated ACK-signal failure tests exercise both changed-event and wake-event failure paths and assert both retained memberships, released owner/finalizer, zero outstanding/reserved counts, zero callback count, and retained Submitter/Adapter state.

## Delivery guarantee decision

The current `void` callback API cannot prove exactly-once external effects. There is no durable recipient receipt, idempotency key, transactional boundary, or callback return value.

For the current API generation, the only honest contract that can be implemented without changing the callback interface is:

- **At-most-once callback invocation per accepted operation within the lifetime of the process.**
- A callback invocation is claimed atomically before entering user code; the callback runs outside internal locks.
- If the process terminates or the callback execution becomes ambiguous after claim, do not replay automatically. The outcome is explicitly unknown; this may lose delivery, but avoids automatic duplicate invocation.
- A callback that needs durable effects must itself use a stable operation idempotency key and recipient-side durable deduplication. That is a future API extension; the present callback does not carry such a key.
- Do not describe this as exactly-once effect or crash-safe delivery.

This contract is a proposal, not implemented. If product requirements demand guaranteed eventual delivery, the API must be extended to an idempotent consumer protocol (stable operation ID + durable deduplication/receipt or transactional inbox/outbox). A local `PENDING -> CLAIMED -> DELIVERED` field alone is insufficient across crashes.

## Required independent state dimensions

Do not compress these into one Boolean:

- I/O ownership: `PENDING`, `TERMINAL`, `OWNER_RELEASED`.
- Finalizer: existing finalizer lifecycle, including close failure.
- Submitter acknowledgement: `NOT_COMMITTED`, `COMMITTED`.
- Submitter notification: `NOT_REQUIRED`, `PENDING_REPAIR`, `SIGNALED`, `REPAIR_BLOCKED`.
- Callback disposition: `PENDING`, `CLAIMED`, `RETURNED`; `CLAIMED` is non-replayable under the at-most-once proposal.
- Adapter-list membership and Worker-list membership: independent explicit facts, each reconciled against actual traversal.

ACK repair must inspect the request under Submitter synchronization. If it is already `ACKED`, it must not call acknowledge again. Notification repair is a separate idempotent operation and must not change ACK state or decrement issuer leases a second time.

## Read-only Retained integrity validator

Before any mutation API, add a read-only validator and negative controls. It must not change links, counters, causes, ownership, lifecycle, or callback state.

### Adapter list checks

- Traverse from `adapter.retained_head` under the Adapter lock.
- Detect cycles and duplicate envelope addresses.
- Count nodes and compare exactly with `adapter.retained_count`.
- Verify each node's Adapter membership flag and owning Adapter pointer.
- Verify every node has a valid, non-null embedded operation and no freed/invalid marker.

### Worker list checks

- Traverse from `worker.retained_head` under the Worker lock.
- Detect cycles and duplicate operation addresses.
- Count nodes and compare exactly with `worker.retained_count`.
- Verify each operation maps to exactly one live envelope where the retention class requires Adapter ownership.
- Verify operation membership is not inferred solely from Adapter membership.

### Cross-list and state checks

- Pending-I/O retention must preserve exact issuing pipe, OVERLAPPED, event, buffer, owner and finalizer state; no terminal release may be inferred from list presence.
- Post-terminal bookkeeping retention must prove terminal release, finalizer released, owner released, and zero outstanding/reserved counts.
- ACK state, issuer lease count, notification state and callback disposition must be checked independently.
- A callback already `CLAIMED` or `RETURNED` must never be auto-invoked by validation or repair.
- Report mismatches; do not repair them in the validator.

### Concurrency and lock order

The current implementation acquires Adapter and Worker locks in separate phases, not as one atomic cross-list transaction. Do not invent a lock order from current call order. Before a validator traverses both lists concurrently, establish and document a global lock order or use a stable quiescent snapshot after admission is closed and all relevant executors are joined. A validator must not deadlock with `retain_envelope`.

### Required negative controls

1. Adapter count too small/large.
2. Worker count too small/large.
3. Cycle in either list.
4. Duplicate envelope or operation node.
5. Missing cross-list counterpart for a post-terminal retained item.
6. Incorrect ownership/finalizer state for the retention class.
7. ACKED with zero lease but notification pending.
8. ACK not committed with nonzero lease.
9. Callback CLAIMED and callback RETURNED cases.
10. Concurrent retain attempt while validator runs; validator must either obtain a quiescent snapshot or return `BUSY`, never produce a false clean result.

## Implementation gate

Do not add unlink/free, callback replay, ACK retry, notification repair, or lifecycle reset in this step. First approve the callback delivery guarantee and lock/snapshot protocol. Then add only explicit state/membership fields and read-only integrity tests. A later mutation API requires separate failure-atomicity tests for both lists and all counters.

## Verification

- Source inspection confirms callback is `void` and invoked outside Worker lock; no recipient receipt/idempotency token exists in this API.
- Existing Adapter API suite has prior evidence of 10/10 runs, 724 assertions/run (7,240 total), including both ACK-signal failure paths. No new run was performed for this design-only step.
- Existing Submitter failure matrix has prior evidence of 25/25 runs, 164 assertions/run (4,100 total), including ACKED + zero issuer lease after notification failure. No new run was performed for this design-only step.
- No source or test files modified. No build outputs created or removed. No processes terminated. No commits or resets.

## Decision

Adopt at-most-once callback invocation as the provisional contract for the current in-process `void` API; do not claim exactly-once external effects. Keep Retained resources fail-closed. Next step: implement a read-only, quiescent-snapshot list-integrity test harness with deliberate corruption fixtures, without adding unlink or recovery mutation.

Production remains NO-GO; Phase 1 remains NOT ACCEPTED.
