# Durable Callback Invocation Runtime Boundary v1
Date: 2026-11-17

## Scope and location
Working Copy only: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`.
Added an isolated Windows runtime wrapper around the versioned durable callback table and deterministic owner-state model. No Submitter, Adapter, Worker, or Orchestrator integration was performed. No production path was changed.

## Runtime synchronization and invocation
- Two independent Windows `CRITICAL_SECTION` locks:
  - state lock protects owner-state admission, completion, close, and reconciliation;
  - callback lock serializes all provider callbacks because v1 has no validated provider-thread-safe capability.
- Lock order is callback lock then state lock only for the post-queue recheck; the state lock is released before invoking provider code. Admission takes and releases only the state lock before waiting for the callback lock.
- Each invocation validates the operation ID, admits a unique owner lease, waits for callback serialization, then rechecks owner state and exact active lease under the state lock.
- A callback admitted before BeginClose but still waiting for the callback lock is suppressed if close/quarantine has begun. Its lease is completed as known/no callback effect. This prevents a queued callback from starting after the close boundary.
- Result buffers are initialized to exact v1 size/schema, UNKNOWN disposition, zeroed reserved/native fields, and the exact expected operation-ID echo before invoking the provider.
- The provider callback executes without the state lock. Results are normalized by the existing v1 normalizers; malformed structures, missing callback writes, bad echoes, or invalid evidence become UNKNOWN and quarantine the owner.
- UNKNOWN retains the exact lease ID for explicit reconciliation. BeginClose/FinishClose and reconciliation are exposed through synchronized runtime entry points.
- Destroy requires CLOSED and zero in-flight/unresolved work. The caller must externally quiesce every API entrant before destroy; runtime storage, callback table, and callback context remain caller-owned until then. Initialization is only for fresh storage.

## Validation
- MSVC x64, `/W4 /WX /TC`: successful.
- Runtime test: **35 checks/run × 25 runs = 875 assertions**, zero failures.
- Covered callback table/config validation, initialized typed Issue and Handoff result contracts, successful normalized Issue/Handoff, malformed operation-ID echo -> UNKNOWN/quarantine, admission rejection while quarantined, explicit lease reconciliation, close/finish/destroy, two concurrent callers proving callback serialization (`max_active == 1`), and a blocked callback plus queued second callback where BeginClose occurs before release: the active callback completes, the queued callback is suppressed and returns BUSY, and no second provider invocation occurs.
- Test executable SHA-256: `E1E19E9DD430D8B6D1261FFF4568B560B2229A19B1932BA3F84EC5BEEA4D8ABC`.

## Source hashes
- `raya-durable-runtime-v1.h`: `6799F895EB70E74D3A830B044B6EB8BE4FD1BBFC9CDFEADB92B9C59445CD8C4E`
- `raya-durable-runtime-v1.c`: `9BB9571D5D24601A0E3CE63C155C7228966F1E5125ABE29068AD99B82DCB64FF`
- `raya-durable-runtime-v1-test.c`: `FF3ECEFA6FC7AD92BD7A122EA0149F324F651AB8754E8CC7504C0A44D1535984`

## Explicit limitations
- This is a synchronized callback invocation boundary, not a durable journal. It does not persist EFFECT_BEGIN before a callback or EFFECT_RESULT after it; process crash loses the in-memory owner/lease state.
- It does not implement stable identity reservation, WAL/torn-tail recovery, replay, recipient receipts/status queries, or exactly-once delivery.
- The runtime serializes callbacks unconditionally; no thread-safe provider capability exists in v1.
- No timeout, cancellation, callback exception isolation, or forced callback termination is provided. A hung callback holds callback serialization and can prevent close from draining.
- Reconciliation accepts a caller-supplied determinate decision; the runtime does not independently verify external evidence. A caller must only reconcile after trustworthy external evidence.
- The caller must not free or move runtime storage, callback table, or context during use; must not call teardown from a callback; and must externally stop all API entrants before destroy. The wrapper cannot protect against a caller violating those lifetime rules.
- The configuration's local-journal capability describes the wider contract; this runtime slice itself does not implement that journal.

## Build artifact hygiene
Build outputs were created in a dedicated temporary directory on D:. They were inspected by exact filename, type, size, and SHA-256 before cleanup. The final cleanup count and residual-artifact check are recorded in the execution summary accompanying this report.

## Project status
This slice advances Stage 5 (lifecycle, teardown, and recovery hardening) only. Phase 1 remains NOT ACCEPTED; Production remains NO-GO.
