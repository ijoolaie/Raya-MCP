# Registry Close API Compatibility and Object-Kind Enforcement Review — 2026-11-17

Status: read-only API/call-site review. Working Copy only.
Production: NO-GO. Phase 1: NOT ACCEPTED.

## Executive conclusion

The generic Registry and Orchestrator share the same public `raya_orch_complete_close` function. The function has no expected-object-kind parameter and intentionally permits untouched cleanup cursor entries. Generic Registry tests rely on that permissive, policy-neutral behavior.

The concrete Orchestrator bypass is therefore not safely fixed by changing the generic completion rule globally. There are only two production call sites of `raya_orch_register`: generic Registry tests (kind 7) and Orchestrator Context registration (kind `RAYA_ORCHESTRATOR_CONTEXT_KIND`). This makes a narrow object-kind-aware completion boundary feasible, but it must be explicit and tested.

## 1. Public API and call-site inventory

Public generic API:
`raya_orch_complete_close(registry, handle, owner)`

It validates slot/owner/lease/cursor state and reclaims any registered object kind. It cannot distinguish Orchestrator Context from an unrelated generic object.

Call sites in Working Copy:
- `raya-orchestrator-registry-v1-test.c`: generic objects registered with kind 7.
- `raya-orchestrator-v1-test.c`: Orchestrator Context handles registered with `RAYA_ORCHESTRATOR_CONTEXT_KIND`.
- Production Orchestrator implementation registers Context with `RAYA_ORCHESTRATOR_CONTEXT_KIND`.
- No other non-test registration callsite was found.

There are no production direct callers of `raya_orch_complete_close` outside the two test files; however the function is public in the Registry header, so external consumers can call it.

## 2. Why global strictness is incompatible

Generic Registry tests exercise:
- completion blocked while a lease remains;
- owner completion after lease drain even with all cursor steps NOT_ATTEMPTED;
- slot generation advances and slot can be reused;
- completion with one committed cursor step while other steps remain untouched.

Changing `raya_orch_complete_close` to require all eight cursor entries committed would break the generic Registry contract and those tests. The generic Registry has no component semantics to know which steps apply to an arbitrary object.

Also, the current completion predicate accepts COMMITTED + ABNORMAL_EXIT and COMMITTED + TERMINAL_FAILURE. That is suitable for generic recording of an irreversible effect, but must not be reused as Orchestrator component health acceptance.

## 3. Narrow enforcement alternatives

### Option A — Add explicit expected-kind completion API (preferred candidate)
Add a distinct Registry operation such as:
`raya_orch_complete_close_kind(registry, handle, owner, expected_kind)`

Under the Registry exclusive lock it would:
- validate exact slot, CLOSING state, owner, zero leases;
- validate `object_kind == expected_kind`;
- preserve the existing generic completion API unchanged for non-Orchestrator objects;
- remain a low-level primitive only; it still does not know Worker ownership or required cursor policy.

Then expose Orchestrator-only completion:
`raya_orchestrator_complete_close_owned(...)`

This function must retrieve Context using exact cleanup owner and validate the full component policy before calling kind-aware Registry reclamation.

However, if generic `raya_orch_complete_close` remains callable for Orchestrator Context kind, this alone does not prevent bypass. A stronger enforcement rule is required: generic completion must reject the Orchestrator Context kind, while kind-aware completion is the only path for that kind. This can preserve generic behavior for other kinds.

### Option B — Registry-configured per-kind completion policy
Register a policy descriptor for each object kind, with required steps and accepted outcomes. This is more general but adds policy storage, registration lifecycle, validation and ABI surface. It risks coupling generic Registry to component semantics and is not justified by current two-kind usage.

### Option C — Hide generic Registry completion from product consumers
Make generic completion an internal/private API and expose only the Orchestrator policy-aware wrapper to product code. This is architecturally clean if no external Registry consumers depend on the public symbol, but Working Copy alone cannot establish external compatibility. Requires API/ABI consumer inventory before changing visibility.

## 4. Required Orchestrator completion predicate

For Orchestrator Context kind:
- Adapter Destroy: COMMITTED + SUCCESS.
- Submitter admission close: COMMITTED + SUCCESS.
- Submitter Shutdown: COMMITTED + SUCCESS.
- Submitter Join: COMMITTED + SUCCESS (ABNORMAL_EXIT must block).
- Submitter Destroy: COMMITTED + SUCCESS.
- Worker OWNED: Worker Shutdown, Join, Destroy each COMMITTED + SUCCESS.
- Worker BORROWED: do not invoke Worker lifecycle; require explicit external-owner acknowledgement recorded in a dedicated cursor step or authenticated caller contract.
- Any IN_PROGRESS, UNKNOWN, BLOCKED, PARTIAL, RETRYABLE_FAILURE, ABNORMAL_EXIT or TERMINAL_FAILURE blocks final Context reclamation.
- Cursor state must be validated while the exact cleanup owner holds exclusive close ownership. Do not rely on an unlocked snapshot followed by completion if another operation can mutate the cursor.

The eight-step Registry capacity currently accommodates steps 0–4 and proposed Worker steps 5–7. BORROWED acknowledgement must fit without overloading Worker Destroy's meaning; either add a distinct ownership-policy outcome encoded safely in one existing unused slot or revise capacity/schema deliberately. Do not silently mark a Worker Destroy step successful for a Worker that was never destroyed.

## 5. External compatibility questions still open

Before implementation, determine:
1. Is `raya-orch_complete_close` consumed by any external component outside this Working Copy?
2. Is Registry struct layout part of a stable ABI, or are all consumers rebuilt together?
3. Are non-Orchestrator object kinds expected to use all eight cursor entries?
4. Should BORROWED Worker close require caller-provided proof of its own terminal lifecycle, or only proof that Orchestrator released its references and the external owner retains responsibility?
5. Is explicit BORROWED acknowledgement durable across cleanup-owner handoff? Current cursor is in-memory only.

No answer to these external questions is inferred from local callsite absence.

## 6. Recommended next implementation boundary

Do not modify generic cursor semantics. First add a policy-aware Orchestrator completion design and tests:
- generic kind 7 remains permissive and its tests remain unchanged;
- Orchestrator Context kind is rejected by generic completion;
- only owner-only Orchestrator completion can reclaim Context kind;
- OWNED requires all eight applicable lifecycle steps success;
- BORROWED requires steps 0–4 success plus explicit external-owner acknowledgement, without Worker Shutdown/Join/Destroy;
- abnormal Worker Join and any retained/core terminal hold block reclamation;
- test direct generic API bypass attempt against Context kind;
- test non-Orchestrator generic completion compatibility.

This report does not implement the API or alter the cursor.

## 7. Scope and verification

- Inspected Registry and Orchestrator headers/implementations and all C/H call sites of `raya_orch_register` and `raya_orch_complete_close`.
- No build/test run; test code and build scripts were inspected only.
- No source/header/test/config/README changed.
- No process termination, install/download, or temporary artifact operation.
- Working Copy only; no Git status or Commit claim.

## 8. Source SHA-256

| File | SHA-256 |
|---|---|
| raya-orchestrator-registry-v1.h | 032574EF501ED535F600CC0E9F932C42F1D0BB43907BB7851ABE5DF8FAED5CAE |
| raya-orchestrator-registry-v1.c | 7805F1780BF44E8A40A3A2F0B9E26E22A304549A546FCAAEDDEEFB398073185D |
| raya-orchestrator-registry-v1-test.c | DE538121939BB16BDDBB5FDE5660179D9A24A8393602F17AD15F3BBFAABDB0FE |
| raya-orchestrator-v1.h | A73F9961F7C02EE3F64488DC7920BDD092BA4F5763FF05A74A2B4FBF6B342EC7 |
| raya-orchestrator-v1.c | 80073CBFFDCBBF96B01879562506616F0ED2E969081D0F50C32BE56D8E534F25 |
| raya-orchestrator-v1-test.c | DB493C621EFB0C331A30341076150EFF5943A6CB2A58C8C9FE0CB16220AAD06C |
