# Orchestrator OWNED Worker Lifecycle Cursor Integration — 2026-11-18

## Scope
Implemented the missing Orchestrator Worker cleanup cursor steps in the Working Copy:

`D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`

This is a Stage 5 lifecycle slice. It is not a production entrypoint integration or full product end-to-end acceptance.

No commit/reset, Production deployment, install/download, process termination, README edit, or unrelated cleanup was performed.

## Changes

### Cleanup cursor steps
Added public step constants:
- Step 5: `RAYA_ORCHESTRATOR_CLEANUP_STEP_WORKER_SHUTDOWN`
- Step 6: `RAYA_ORCHESTRATOR_CLEANUP_STEP_WORKER_JOIN`
- Step 7: `RAYA_ORCHESTRATOR_CLEANUP_STEP_WORKER_DESTROY`

The existing strict Registry policy already requires steps 0–4 for all Contexts and steps 5–7 only for OWNED Worker Contexts.

### New owner-only APIs
Added:
- `raya_orchestrator_worker_shutdown_owned`
- `raya_orchestrator_worker_join_owned`
- `raya_orchestrator_worker_destroy_owned`

All APIs require the exact cleanup owner and an OWNED Worker. BORROWED Worker requests are rejected before invoking Worker lifecycle APIs.

Ordering:
1. Worker Shutdown requires all common Adapter/Submitter steps 0–4 to be COMMITTED + SUCCESS.
2. Worker Join requires Worker Shutdown COMMITTED + SUCCESS.
3. Worker Destroy requires Worker Shutdown and Join both COMMITTED + SUCCESS.
4. Context reclamation remains governed by the strict eight-step mask for OWNED Worker.

### Result mapping
- Shutdown success/already-stopped -> COMMITTED + SUCCESS.
- Shutdown retryable no-effect -> NOT_ATTEMPTED + RETRYABLE_FAILURE; a known STOPPING effect is represented as PARTIAL + RETRYABLE_FAILURE.
- Shutdown terminal/degraded -> BLOCKED + UNCERTAIN.
- Join wait/query/thread-handle-close retryable failure -> NOT_ATTEMPTED + RETRYABLE_FAILURE.
- Join after abnormal thread exit, where the thread handle was successfully reaped -> COMMITTED + ABNORMAL_EXIT. This records the handle-reap effect but does not permit Worker Destroy or Context reclamation.
- Join uncertainty without a reaped handle -> BLOCKED + UNCERTAIN.
- Destroy retryable wake-event close failure -> NOT_ATTEMPTED + RETRYABLE_FAILURE.
- Destroy unsafe state or uncertain result -> BLOCKED + UNCERTAIN.
- Only normal Worker thread exit and successful Worker Destroy satisfy the required cursor steps.

The existing component APIs remain the authority for Worker lifecycle invariants; the Orchestrator does not bypass their checks.

## Tests and validation

Command:
`build-raya-orchestrator-v1-test.cmd`

Compiler:
Visual Studio 2026 Build Tools, x64, `/W4 /WX /TC`.

Final result:
- 152 assertions per run
- 10 consecutive runs
- **1,520 passed; 0 failed**

New test coverage:
1. Real Worker thread: common steps 0–4 precede Worker Shutdown.
2. Deterministic Join timeout using a suspended real thread: cursor records retryable/no-effect; after resume, retry succeeds.
3. Normal thread exit: Join reaps the handle; Destroy succeeds; all eight steps permit Context reclamation.
4. Abnormal thread exit: Join records ABNORMAL_EXIT and the reaped handle; Destroy is not invoked; Context reclamation remains blocked.
5. BORROWED Worker: Orchestrator Worker teardown API rejects the request without calling Worker lifecycle operations.

The test fixture for abnormal exit manually closes its remaining wake Event and deletes its critical section after confirming that production Destroy correctly refuses the STOPPING state. This is test-harness-only cleanup, not a production recovery path.

Important limitation: steps 0–4 in the new Worker-specific tests are committed as controlled cursor preconditions. These tests validate the Worker lifecycle integration and gating, but do not execute a complete live Adapter → Submitter → Worker workload and teardown in one scenario. No production callsite/entrypoint integration is claimed.

## Changed files and SHA-256

| File | Bytes | SHA-256 |
|---|---:|---|
| `raya-orchestrator-v1.h` | 5,915 | `240A4C58F9D1F5B59787B4441CF008FB6B931C0334F6DD377E9F6A9821562363` |
| `raya-orchestrator-v1.c` | 27,655 | `8B5FB4BFCB890E76EBA55649B1BFA16B648B96C48DA2469671E07C248D220B24` |
| `raya-orchestrator-v1-test.c` | 39,897 | `500C4DC922E7579B0C7F58926D52D5EBAB3D4131E0637BD12125B48F5CE71831` |

Post-build cleanup check:
- Targeted executable/object artifacts remaining: **0**.
- C: 7.21 GB free.
- D: 2.04 GB free.

## Remaining work
- Exercise the complete Adapter → Submitter → Worker lifecycle using real initialized components and actual retained/outstanding operation states.
- Add deterministic Orchestrator-level tests for Worker shutdown degradation and Destroy close failure where the available test seam supports them.
- Confirm the production entrypoint and lifecycle authority; this Working Copy still has no demonstrated production caller chain.
- Revisit durable recovery and retained-work reconciliation; Worker cursor integration does not resolve retained operations or provide crash recovery.

## Status
Stage 5 remains in progress.
Phase 1: NOT ACCEPTED.
Production: NO-GO.
