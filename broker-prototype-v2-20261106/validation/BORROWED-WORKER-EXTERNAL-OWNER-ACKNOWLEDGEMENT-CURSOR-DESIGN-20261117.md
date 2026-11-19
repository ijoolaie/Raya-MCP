# BORROWED Worker External-Owner Acknowledgement and Eight-Step Cursor Design — 2026-11-17

Status: read-only design review. Working Copy only.
Phase 1: NOT ACCEPTED. Production: NO-GO.

## 1. Decision summary

The eight Registry cursor slots (0..7) can represent both ownership modes without falsely recording a borrowed Worker as destroyed, but only with an explicit ownership-specific policy:

- Steps 0..4 remain the existing Adapter and Submitter lifecycle.
- OWNED Worker: steps 5, 6, 7 mean Worker Shutdown, Join, Destroy respectively. All three must be COMMITTED + SUCCESS.
- BORROWED Worker: step 5 means explicit external-owner handoff acknowledgement. Steps 6 and 7 remain NOT_ATTEMPTED and are intentionally not executed.

The generic Registry cursor remains policy-neutral. Only a policy-aware Orchestrator completion function may interpret these slots and reclaim an Orchestrator Context.

## 2. Meaning of BORROWED acknowledgement

A BORROWED acknowledgement must not claim:
- Worker has stopped;
- Worker thread has joined;
- Worker resources have been destroyed;
- all external users of Worker have globally quiesced;
- any durable or cross-process ownership transfer occurred.

It should mean only that the designated external lifecycle owner explicitly accepts continuing responsibility for the Worker and confirms that Orchestrator is relinquishing its own references and will not invoke Worker lifecycle APIs.

For safe Context reclamation, the caller must also establish the dependency boundary:
- Adapter Destroy succeeded, with no active or retained Adapter envelopes;
- Submitter admission is closed and Submitter Shutdown/Join/Destroy succeeded;
- no in-flight operation, callback, release path, or Worker-owned operation can still access the Orchestrator Context or the destroyed Adapter/Submitter;
- external owner keeps Worker storage alive for as long as Worker itself or any independent user may access it;
- Worker will not call back into the reclaimed Context or destroyed components.

The current source does not provide a general Worker reference-count or external-owner proof mechanism. Thus this acknowledgement is an explicit caller assertion/contract, not mechanically verified proof. It must fail closed if the caller cannot establish these conditions.

## 3. Proposed eight-slot layout

| Slot | OWNED Worker | BORROWED Worker |
|---|---|---|
| 0 | Adapter Destroy | Adapter Destroy |
| 1 | Submitter admission close | Submitter admission close |
| 2 | Submitter Shutdown | Submitter Shutdown |
| 3 | Submitter Join | Submitter Join |
| 4 | Submitter Destroy | Submitter Destroy |
| 5 | Worker Shutdown | External-owner acknowledgement |
| 6 | Worker Join | NOT_ATTEMPTED (not applicable) |
| 7 | Worker Destroy | NOT_ATTEMPTED (not applicable) |

Completion policy:
- OWNED: slots 0..7 each COMMITTED + SUCCESS.
- BORROWED: slots 0..5 each COMMITTED + SUCCESS; slots 6..7 must remain NOT_ATTEMPTED. Any other state blocks completion.
- A BORROWED acknowledgement is not encoded as Worker Destroy success.
- No ABNORMAL_EXIT or TERMINAL_FAILURE is accepted as successful teardown.
- IN_PROGRESS, UNKNOWN, BLOCKED, PARTIAL, retryable failure, or uncertain outcome blocks Context reclamation.

This avoids increasing Registry capacity or changing its generic cursor representation. It does require versioned Orchestrator policy semantics because the meaning of slot 5 is conditional on immutable Context ownership metadata.

## 4. Acknowledgement API contract proposal

Candidate API name:
`raya_orchestrator_acknowledge_borrowed_worker_owner_owned(...)`

Requirements:
1. Require exact Registry Cleanup Owner, CLOSING state, zero API leases, and valid Context magic/registration.
2. Require Context ownership exactly BORROWED.
3. Require slots 0..4 each COMMITTED + SUCCESS.
4. Require slots 6 and 7 still NOT_ATTEMPTED.
5. Require slot 5 NOT_ATTEMPTED or an explicitly retryable no-effect state; reject IN_PROGRESS/UNKNOWN/BLOCKED/PARTIAL.
6. Record slot 5 as IN_PROGRESS before accepting the assertion, then COMMITTED + SUCCESS only after the caller's explicit acknowledgement is supplied.
7. The API must not invoke Worker Shutdown/Join/Destroy.
8. Do not expose an implicit/default acknowledgement from Context registration or BeginClose.
9. Document the assertion as process-local and non-durable; owner handoff/restart does not preserve it beyond the in-memory Registry.
10. Require serialized cleanup-owner calls; no callback into teardown while acknowledgement is being recorded.

The API cannot independently prove the external owner's assertion. If a stronger guarantee is needed, replace the bare assertion with an external owner token/registration handshake and explicit release protocol. That is a separate cross-component design, not present in the current source.

## 5. Required policy-aware completion behavior

A future `raya_orchestrator_complete_close_owned` should:
- retrieve Context only through exact owner and zero-lease Registry validation;
- inspect the immutable Worker ownership field;
- validate all required slots and exact outcomes for that ownership mode;
- reject BORROWED if slot 5 acknowledgement is missing;
- reject OWNED if any Worker lifecycle step is missing or non-success;
- call the kind-restricted Registry reclaim primitive only after policy passes.

The low-level generic `raya_orch_complete_close` must reject `RAYA_ORCHESTRATOR_CONTEXT_KIND`, otherwise a caller can bypass the policy wrapper. Generic object kinds retain existing generic semantics.

A policy check followed by an unrestricted generic completion call is not sufficient: the Registry must atomically enforce the object-kind restriction at reclamation, under its lock. External consumers and ABI compatibility still need review before changing this public API boundary.

## 6. Critical lifecycle caveat

The current Adapter/Worker dependency is not fully represented by a simple ownership enum. Adapter borrows Worker; Adapter Destroy does not stop Worker. Worker may have its own thread and independent users.

The BORROWED path is safe only if the external owner contract ensures that no Worker activity can touch the Adapter, Submitter, Context, or callbacks being reclaimed. Existing Adapter zero active/retained counts are strong local evidence, but not a universal proof that every external Worker reference is gone.

If this precondition cannot be demonstrated, do not acknowledge and do not reclaim Context. Keep the Context/related storage alive and report BUSY/RECONCILE.

## 7. Test matrix required before implementation acceptance

BORROWED:
- no acknowledgement => completion blocked;
- steps 0..4 missing/abnormal => acknowledgement rejected;
- exact owner mismatch or active lease => acknowledgement rejected;
- wrong ownership mode => acknowledgement rejected;
- explicit acknowledgement => slot 5 committed; slots 6/7 remain NOT_ATTEMPTED;
- generic completion API against Orchestrator Context => rejected;
- policy completion => succeeds only after valid acknowledgement;
- acknowledgement retry/repeated call => idempotent without rewriting contradictory evidence;
- stale owner after owner release/reacquire => rejected;
- no Worker lifecycle API invoked.

OWNED:
- each missing Worker step independently blocks completion;
- Worker Shutdown failure/degraded state blocks;
- abnormal Worker Join blocks even if thread handle was reaped;
- Worker Destroy retryable partial close can resume only through component-tracked retry;
- Worker Destroy success permits completion only after steps 0..4 success;
- generic completion bypass rejected.

Generic Registry compatibility:
- generic kind 7 still completes after lease drain with all steps NOT_ATTEMPTED;
- generic cursor semantics and generation reuse unchanged;
- unknown/non-Orchestrator kinds retain prior behavior.

End-to-end:
- verify no retained Adapter/Worker operation or callback can touch reclaimed dependencies;
- test both OWNED and BORROWED complete teardown paths with real component instances;
- inject failure at each boundary and verify no Context reclamation.

## 8. Open decisions / limitations

- Who is the designated external Worker lifecycle owner, and how is its authority represented?
- Is the acknowledgement a caller assertion acceptable for Phase 1, or is a verifiable owner registration/release handshake required?
- Can the external owner prove Worker will no longer access Adapter/Submitter/context callbacks?
- Does the public Registry API have external ABI consumers? Local call-site search cannot answer this.
- No durable recovery of owner acknowledgement exists; Registry and cursor are in-memory.

Until these are answered and tested, BORROWED acknowledgement is a design proposal, not an implemented safety guarantee.

## 9. Scope and verification

- Inspected Orchestrator Context registration, ownership enum, existing cleanup APIs, Registry cursor storage/transition semantics, and existing OWNED/BORROWED tests.
- No source/header/test/config/README changes.
- No build/test run.
- No process termination, install/download, or temporary artifact deletion.
- No Production or README changes.
- Working Copy only; no Commit/Reset.

## 10. Verified source SHA-256

| File | SHA-256 |
|---|---|
| raya-orchestrator-registry-v1.h | 032574EF501ED535F600CC0E9F932C42F1D0BB43907BB7851ABE5DF8FAED5CAE |
| raya-orchestrator-registry-v1.c | 7805F1780BF44E8A40A3A2F0B9E26E22A304549A546FCAAEDDEEFB398073185D |
| raya-orchestrator-registry-v1-test.c | DE538121939BB16BDDBB5FDE5660179D9A24A8393602F17AD15F3BBFAABDB0FE |
| raya-orchestrator-v1.h | A73F9961F7C02EE3F64488DC7920BDD092BA4F5763FF05A74A2B4FBF6B342EC7 |
| raya-orchestrator-v1.c | 80073CBFFDCBBF96B01879562506616F0ED2E969081D0F50C32BE56D8E534F25 |
| raya-orchestrator-v1-test.c | DB493C621EFB0C331A30341076150EFF5943A6CB2A58C8C9FE0CB16220AAD06C |
| integrated-submit-adapter-v2.c | A9B39D0D4094E8418F820E53B3E28FEE64FC7B0A9EC417450F25EC422EE8D87F |
| reaper-managed-worker-v2.c | 5B0B43BA60303A0EC3EA07A9E26A641E32E40812702C0F87EC37C1ADD11DD8DD |
