# Orchestrator Worker Ownership Policy vs Teardown Execution Gap — 2026-11-17

Status: read-only source and test audit. Working Copy only.
Production: NO-GO. Phase 1: NOT ACCEPTED.

## Executive conclusion

The Orchestrator stores a Worker ownership policy (`BORROWED` or `OWNED`) but does not execute Worker lifecycle teardown in either mode. The ownership value is currently metadata exposed through the leased Context API; it is not yet connected to a Worker Shutdown/Join/Destroy cleanup cursor.

The cleanup cursor currently defines only five steps:
0. Adapter Destroy
1. Submitter admission close
2. Submitter Shutdown
3. Submitter Join
4. Submitter Destroy

No Worker cleanup step constant or owner-only Worker lifecycle function exists in `raya-orchestrator-v1.h/.c`. Although the Registry reserves eight cursor slots, the additional slots are not a Worker teardown implementation.

This leaves a concrete lifecycle gap: after Submitter Destroy succeeds, the Orchestrator has no API that proves the Worker is quiescent, shuts it down, joins it, destroys its wake event/lock, or records its result. The Context/Registry reclamation API can be called separately; it does not itself enforce Worker teardown policy.

## 1. Ownership contract as implemented

`raya_orchestrator_context_register`:
- requires non-NULL Adapter, Submitter and Worker;
- validates ownership is exactly BORROWED or OWNED;
- stores all three component pointers and the ownership enum in caller-owned Context;
- does not acquire or transfer component storage ownership.

Header comment explicitly says:
- components are borrowed by this layer;
- Adapter initializes Submitter;
- Adapter borrows Worker;
- Worker ownership records teardown policy only and does not transfer/free storage.

Lease accessors return Worker pointer and ownership enum only while the Registry lease is active. Cleanup-owner retrieval validates Context and returns the same metadata. No Worker lifecycle operation is performed in these accessors.

## 2. Worker ownership is not consumed by current cleanup

A full source search of `raya-orchestrator-v1.c` shows no calls to:
- `raya_managed_worker_shutdown_ex`
- `raya_managed_worker_join_ex`
- `raya_managed_worker_destroy_ex`

The only component lifecycle calls in the Orchestrator implementation are Submitter operations:
- `raya_submitter_admission_close`
- `raya_submitter_shutdown_ex`
- `raya_submitter_join_ex`
- `raya_submitter_destroy_ex`

The Worker pointer is used for registration, validation and lease retrieval only. `worker_ownership` is checked for enum validity and returned by an accessor, but no branch changes cleanup behavior based on OWNED versus BORROWED.

## 3. Adapter/Worker ownership ordering constraints

Adapter startup requires a Worker that is already initialized and started. Adapter stores the Worker pointer and uses Worker admission, reservation, handoff and completion operations. Adapter borrows Worker; Adapter Destroy closes Adapter admission and drains Adapter entrants/envelopes, but does not stop or destroy Worker or Submitter.

Therefore Worker Shutdown must not race:
- Adapter submit calls or in-flight Adapter entrants;
- Submitter Issue/Publish callbacks that can still hand off to Worker;
- Worker reservations or outstanding operations;
- retained envelopes or Worker terminal holds.

The safe normal order remains:
1. stop external producers and direct component callers;
2. Adapter admission close/drain and confirm active=0, retained=0;
3. Adapter Destroy;
4. Submitter admission close/drain;
5. Submitter Shutdown/Join/Destroy;
6. only then consider Worker Shutdown/Join/Destroy, and only if Worker is OWNED;
7. prove all Worker queues, outstanding operations, reservations, registry entries and retained nodes are terminal/empty;
8. reclaim Context/Registry only after all component use has ended.

For BORROWED Worker, step 6 is explicitly external owner responsibility; Orchestrator must not stop or destroy it.

For OWNED Worker, current code also leaves step 6 to an external caller despite the stored policy. The Orchestrator currently provides no enforcement or cursor evidence.

## 4. Worker lifecycle component contract relevant to future integration

Existing Worker APIs:
- `raya_managed_worker_shutdown_ex`
- `raya_managed_worker_join_ex`
- `raya_managed_worker_destroy_ex`

Important outcomes:
- Shutdown on DEGRADED/FAILED is terminal failure/reconciliation, not a conversion to STOPPED.
- Join can return terminal failure after the thread handle has been reaped; this is a committed join effect but not healthy worker termination.
- Destroy requires joined, lifecycle STOPPED, zero caller_active/outstanding/queued, empty core head/tail, zero reservations, zero integrated registry entries, and empty Worker retained list.
- Destroy can have a retryable wake-event CloseHandle failure; its result preserves that effect.
- A Worker with abnormal exit or retained/terminal-hold state must not be destroyed merely because Join reaped the thread handle.

The Orchestrator cursor must preserve these distinctions rather than copy Submitter assumptions.

## 5. Test coverage gap

`raya-orchestrator-v1-test.c` includes:
- real Adapter Destroy cursor behavior;
- Submitter admission gate and drain;
- Submitter Shutdown/Join/Destroy success and injected failures;
- abnormal Submitter exit and blocked Destroy;
- Context registration and lease accessor checks for BORROWED and OWNED metadata.

The OWNED test registers a Worker with OWNED policy, retrieves the Context through cleanup owner and reclaims the Registry slot. It does not start that Worker, call Worker Shutdown/Join/Destroy, or prove the ownership policy caused a teardown action.

No end-to-end test currently proves:
- Adapter → Submitter → Worker cleanup order;
- OWNED Worker lifecycle cursor;
- BORROWED Worker is never stopped/destroyed by Orchestrator;
- Worker abnormal exit/DEGRADED blocks Context/Registry reclamation;
- Worker Destroy retryable wake-event close is resumed by an Orchestrator cursor;
- retained Worker inventory or core terminal holds block Orchestrator close completion.

## 6. Recommended Worker cursor contract

Do not implement this by simply adding three calls after Submitter Destroy. First specify prerequisites and reconciliation.

Suggested cursor steps, preserving existing numeric IDs:
5. WORKER_SHUTDOWN
6. WORKER_JOIN
7. WORKER_DESTROY

Required policy:
- BORROWED: mark these steps NOT_APPLICABLE/externally_owned through an explicit, validated cursor outcome or a separately documented completion contract; never invoke Worker lifecycle.
- OWNED: require Adapter Destroy and Submitter Destroy committed; require external producers and direct Worker callers quiesced; require Worker ownership inventory/evidence before Shutdown.
- Shutdown SUCCESS/ALREADY_DONE commits; DEGRADED/FAILED blocks and requires evidence reconciliation, not blind retry.
- Join retryable wait/query/CloseHandle failure remains no-effect retryable; abnormal exit after handle reaping records COMMITTED + ABNORMAL_EXIT, but does not authorize Destroy.
- Destroy SUCCESS commits; retryable wake-event close failure preserves exact retryable component state; invalid-state/retained/core-hold blocks.
- Before Context/Registry reclamation, require either all OWNED Worker steps committed or BORROWED policy explicitly acknowledged by the external owner. The Registry itself cannot infer this.
- Add Worker snapshot/reconciliation APIs before attempting to recover retained/terminal-hold states; never unlink/free retained operations as a shortcut.

## 7. Scope and verification

- Inspected `raya-orchestrator-v1.h/.c`, `raya-orchestrator-registry-v1.h`, `raya-orchestrator-v1-test.c`, `integrated-submit-adapter-v2.c`, and `reaper-managed-worker-v2.c`.
- No build or tests run; existing test code was inspected only.
- No source/header/test/config/README changed.
- No process terminated, install/download performed, or temporary artifact created/deleted.
- No Production modification.

## 8. Source SHA-256

| File | SHA-256 |
|---|---|
| raya-orchestrator-v1.h | A73F9961F7C02EE3F64488DC7920BDD092BA4F5763FF05A74A2B4FBF6B342EC7 |
| raya-orchestrator-v1.c | 80073CBFFDCBBF96B01879562506616F0ED2E969081D0F50C32BE56D8E534F25 |
| raya-orchestrator-registry-v1.h | 032574EF501ED535F600CC0E9F932C42F1D0BB43907BB7851ABE5DF8FAED5CAE |
| raya-orchestrator-v1-test.c | DB493C621EFB0C331A30341076150EFF5943A6CB2A58C8C9FE0CB16220AAD06C |
| integrated-submit-adapter-v2.c | A9B39D0D4094E8418F820E53B3E28FEE64FC7B0A9EC417450F25EC422EE8D87F |
| reaper-managed-worker-v2.c | 5B0B43BA60303A0EC3EA07A9E26A641E32E40812702C0F87EC37C1ADD11DD8DD |
