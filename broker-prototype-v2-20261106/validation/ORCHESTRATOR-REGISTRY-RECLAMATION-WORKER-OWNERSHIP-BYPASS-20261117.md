# Orchestrator Registry Reclamation Gate and Worker Ownership Bypass — 2026-11-17

Status: read-only source/test audit. Working Copy only.
Production: NO-GO. Phase 1: NOT ACCEPTED.

## Executive finding

The Worker teardown gap is stronger than merely missing Worker cursor steps.

`raya_orch_complete_close` permits Registry slot reclamation when every cleanup cursor entry is still `NOT_ATTEMPTED`. It checks that no leases remain and rejects only cursor states other than NOT_ATTEMPTED or an accepted COMMITTED outcome. It does not require Adapter Destroy, Submitter lifecycle, or Worker lifecycle steps to have been attempted or committed.

The Orchestrator test suite explicitly exercises this bypass for both BORROWED and OWNED Worker contexts:
- the BORROWED test reclaims the Registry slot without destroying Worker (expected for borrowed ownership);
- the OWNED test also reclaims the Registry slot after merely retrieving the Worker pointer/ownership metadata, without Worker Shutdown/Join/Destroy.

This is a concrete enforcement gap. Registry reclamation is not currently proof that owned components were torn down. The public low-level Registry API can be called directly, so adding only a high-level Orchestrator Worker cursor would not close the bypass unless reclamation itself validates a required cleanup policy.

## 1. Exact Registry completion rule

In `raya-orchestrator-registry-v1.c`, `raya_orch_complete_close` checks:
1. Registry initialized and nonzero owner.
2. Slot exists, is CLOSING, exact owner matches, and lease_count is zero.
3. For each of eight cursor entries, effect must be either:
   - NOT_ATTEMPTED; or
   - COMMITTED with outcome SUCCESS, ABNORMAL_EXIT, or TERMINAL_FAILURE.

It then clears object/object_kind/owner and increments generation or retires the slot.

There is no requirement that any specific step be COMMITTED. A fresh slot has all eight entries zeroed to NOT_ATTEMPTED, so a caller can BeginClose, drain zero leases, and complete close without running any cleanup.

The function also has no Context/Worker ownership knowledge; Registry slots store an opaque object and object kind.

## 2. Concrete test evidence

`raya-orchestrator-v1-test.c`:
- BORROWED Context: calls `raya_orch_complete_close` after owner Context retrieval, with no Worker shutdown/join/destroy.
- OWNED Context: registers `RAYA_ORCH_WORKER_OWNED`, retrieves Context through cleanup owner, then calls `raya_orch_complete_close` directly. No Worker lifecycle API is invoked.

The current tests therefore demonstrate that the OWNED ownership policy does not gate Registry reclamation.

`raya-orchestrator-registry-v1-test.c` also intentionally confirms that a drained slot can be reclaimed after only a committed cursor effect in one slot, while other slots remain untouched. This is consistent with generic Registry behavior, but insufficient as an Orchestrator component teardown contract.

## 3. Why a Worker cursor alone is insufficient

Adding steps 5–7 to the existing eight-slot cursor would provide a place to record Worker Shutdown/Join/Destroy, but direct callers of `raya_orch_complete_close` could still bypass those steps because NOT_ATTEMPTED is accepted.

The missing enforcement belongs at a policy-aware Orchestrator completion boundary, or in a Registry completion contract that receives and enforces required-step policy. Do not make the generic Registry infer Worker ownership from opaque object memory.

Candidate architecture:
- Keep generic Registry's low-level slot reclamation primitive internal/private to Orchestrator, if API compatibility permits.
- Add `raya_orchestrator_complete_close_owned` that retrieves Context as exact cleanup owner and validates:
  - Adapter Destroy committed SUCCESS;
  - Submitter admission close committed SUCCESS;
  - Submitter Shutdown committed SUCCESS;
  - Submitter Join committed SUCCESS (not abnormal exit);
  - Submitter Destroy committed SUCCESS;
  - if Worker OWNED: Worker Shutdown, Join, Destroy committed SUCCESS;
  - if Worker BORROWED: explicit external-owner acknowledgement/contract is satisfied, without invoking Worker lifecycle.
- Only then call low-level Registry slot reclamation.
- Prevent or clearly deprecate direct public use of `raya_orch_complete_close` for Orchestrator Context slots. If it remains public, it remains a bypass.
- Context memory remains caller-owned and must not be freed until completion returns success and all component references are gone.

This candidate is a design direction, not an implemented fix. ABI/API compatibility and direct Registry users need a separate review.

## 4. Cursor outcome policy needs refinement

The generic Registry accepts COMMITTED + ABNORMAL_EXIT and COMMITTED + TERMINAL_FAILURE as sufficient for slot reclamation. This is appropriate for recording that a resource-level effect (such as thread handle reaping) occurred, but it cannot mean the component is healthy or safe to destroy.

For Orchestrator policy:
- Worker Join ABNORMAL_EXIT may be a committed handle-reap effect, but must block Worker Destroy and Context reclamation.
- Worker Shutdown DEGRADED/FAILED must remain blocked until explicit evidence reconciliation.
- Worker Destroy success is mandatory for OWNED Worker.
- A BORROWED Worker must not be stopped/destroyed by Orchestrator; an explicit external-owner contract is still needed before Context references are released.

The low-level Registry's generic allowance should not be treated as a product-level cleanup acceptance rule.

## 5. Test gaps and required regression cases

Before implementation, add tests for:
1. OWNED Worker cannot complete Orchestrator close with steps 5–7 NOT_ATTEMPTED.
2. OWNED Worker Join ABNORMAL_EXIT blocks Destroy and Context reclamation.
3. OWNED Worker DEGRADED/FAILED Shutdown blocks close.
4. OWNED Worker Destroy retryable wake-event CloseHandle can retry only with component-tracked partial progress.
5. BORROWED Worker lifecycle APIs are never called.
6. BORROWED close requires explicit external-owner acknowledgement before Context/Registry reclamation.
7. Direct generic Registry behavior remains compatible for non-Orchestrator objects.
8. Direct call to low-level complete_close cannot bypass the policy for registered Orchestrator Context (e.g. separate object kind enforcement or API visibility restriction).

## 6. Scope and verification

- Inspected `raya-orchestrator-registry-v1.c/.h`, `raya-orchestrator-registry-v1-test.c`, `raya-orchestrator-v1.c/.h`, and `raya-orchestrator-v1-test.c`.
- No build/test run; test cases were inspected only.
- No source/header/test/config/README changed.
- No process termination, install/download, or temporary artifact operation.
- No Production changes.

## 7. Source SHA-256

| File | SHA-256 |
|---|---|
| raya-orchestrator-registry-v1.c | 7805F1780BF44E8A40A3A2F0B9E26E22A304549A546FCAAEDDEEFB398073185D |
| raya-orchestrator-registry-v1.h | 032574EF501ED535F600CC0E9F932C42F1D0BB43907BB7851ABE5DF8FAED5CAE |
| raya-orchestrator-registry-v1-test.c | DE538121939BB16BDDBB5FDE5660179D9A24A8393602F17AD15F3BBFAABDB0FE |
| raya-orchestrator-v1.c | 80073CBFFDCBBF96B01879562506616F0ED2E969081D0F50C32BE56D8E534F25 |
| raya-orchestrator-v1.h | A73F9961F7C02EE3F64488DC7920BDD092BA4F5763FF05A74A2B4FBF6B342EC7 |
| raya-orchestrator-v1-test.c | DB493C621EFB0C331A30341076150EFF5943A6CB2A58C8C9FE0CB16220AAD06C |
