# Registry Close Policy and Orchestrator Context Reclamation Design Review Ã¢â‚¬â€ 2026-11-18

## Scope
Read-only design review in the Working Copy:
`D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`.

Reviewed the current Registry and Orchestrator headers/implementations and their tests. No source, header, test, build, configuration, or README files were changed. No build or test was run.

## Verified current behavior

### 1. Cursor outcomes do not currently represent an ownership-policy skip
`raya_orch_outcome` currently ends at `RAYA_ORCH_OUTCOME_UNCERTAIN=5`. Cursor validation rejects values above it. Adding a new `SKIPPED_BY_OWNERSHIP_POLICY` outcome would therefore change the public enum contract and require updates to cursor validation, reconciliation, completion rules, and consumers.

It is not necessary for the lifecycle contract. For a BORROWED Worker, steps 5Ã¢â‚¬â€œ7 can remain `NOT_ATTEMPTED / NOT_EVALUATED`; the Orchestrator's policy-aware close operation can interpret these slots according to the Context's ownership metadata. This avoids inventing a fake committed side effect and avoids an enum/API expansion.

### 2. Current generic completion permits Context reclamation without lifecycle work
At `raya-orchestrator-registry-v1.c:329-357`, `raya_orch_complete_close` checks:
- valid CLOSING slot and exact cleanup owner;
- zero API leases;
- each cursor slot is either NOT_ATTEMPTED or COMMITTED with SUCCESS, ABNORMAL_EXIT, or TERMINAL_FAILURE.

Consequently, a Context can currently be reclaimed while all eight cursor entries are untouched. This is the demonstrated bypass. It also permits ABNORMAL_EXIT and TERMINAL_FAILURE as generic committed outcomes; these must not be interpreted as healthy completion by the Orchestrator.

### 3. Registry already has the required atomic primitives
`raya_orch_owner_get_object` validates exact owner, CLOSING state, zero leases, non-null object, and expected object kind while holding the Registry shared lock. `raya_orch_complete_close` performs slot reclamation under the Registry exclusive lock.

The Registry does not need to understand the Context structure or dereference it to enforce a close policy.

## Recommended design

### A. Add an explicit per-slot close policy at registration
Add a small policy field to `raya_orch_registry_slot`, for example:
- `RAYA_ORCH_CLOSE_POLICY_GENERIC` (default for existing `raya_orch_register`);
- `RAYA_ORCH_CLOSE_POLICY_REQUIRED_STEPS` (selected by a new registration API).

Keep the existing `raya_orch_register` behavior unchanged for generic Registry clients and existing generic tests. Add a registration entry point such as `raya_orch_register_with_policy` that atomically stores the policy **and its immutable required-success mask** with object pointer, kind, and OPEN state. For GENERIC policy the mask is zero. For REQUIRED_STEPS policy, validate that the mask has no unsupported bits. The Orchestrator Context registration computes the mask from `worker_ownership` before registration: bits 0Ã¢â‚¬â€œ4 for all Contexts, plus bits 5Ã¢â‚¬â€œ7 for OWNED Worker; BORROWED omits bits 5Ã¢â‚¬â€œ7. Do not register and then patch policy after publishing the handle.

The exact API name/signature is subject to compatibility review. A strict slot stores its immutable required-success mask at registration; the close caller cannot weaken or replace that policy.

### B. Add an atomic policy-aware Registry completion operation
Add a function such as:
`raya_orch_complete_close_required(registry, handle, owner, expected_kind)`. The required mask is immutable slot metadata and is not supplied by the close caller.

Under one Registry exclusive-lock critical section it must:
1. Find the exact generation token.
2. Require CLOSING state, exact owner, expected object kind, and zero leases.
3. Require the slot's registered close policy to be REQUIRED_STEPS.
4. Read the immutable required-success mask stored at registration and validate it contains no bits outside the eight supported steps.
5. For every required bit, require exactly COMMITTED + SUCCESS.
6. For every non-required bit, require exactly NOT_ATTEMPTED + NOT_EVALUATED.
7. Reclaim the slot (clear object/kind/owner/policy/mask; increment generation or retire at generation exhaustion).

No cursor validation and reclaim may be split across separate lock acquisitions. Do not allow ABNORMAL_EXIT or TERMINAL_FAILURE to satisfy a required-success bit.

The legacy `raya_orch_complete_close` must reject strict-policy slots, while retaining its current permissive generic semantics for slots registered with GENERIC policy. This closes the bypass without breaking existing generic Registry use cases. The new strict function must reject generic-policy slots. The strict policy and required-success mask are published atomically at registration and survive cleanup-owner yield/reacquisition.

### C. Put Orchestrator semantics in the Orchestrator layer
Add `raya_orchestrator_complete_close_owned` (or equivalent). It should:
- validate the Context through `raya_orchestrator_owner_get_context`, which already requires exact owner and drained leases;
- derive the required mask from `worker_ownership`;
- require steps 0Ã¢â‚¬â€œ4 to be COMMITTED + SUCCESS for all Contexts:
  0 Adapter Destroy;
  1 Submitter admission close;
  2 Submitter Shutdown;
  3 Submitter Join;
  4 Submitter Destroy;
- for OWNED Worker, additionally require steps 5Ã¢â‚¬â€œ7 to be COMMITTED + SUCCESS (Worker Shutdown, Join, Destroy);
- for BORROWED Worker, leave steps 5Ã¢â‚¬â€œ7 NOT_ATTEMPTED + NOT_EVALUATED and do not call Worker lifecycle APIs;
- call the new atomic Registry completion operation with `RAYA_ORCHESTRATOR_CONTEXT_KIND` and the derived mask.

This avoids a redundant external-owner acknowledgement. BORROWED still means the external owner must independently keep Worker storage alive and prevent late access; Context reclamation does not terminate or transfer ownership of that Worker.

### D. Worker cursor layout
Reserve the remaining slots without changing existing indices:
- 5: Worker Shutdown (OWNED only)
- 6: Worker Join (OWNED only)
- 7: Worker Destroy (OWNED only)

Do not implement Worker lifecycle calls until the preceding Adapter and Submitter stages are successful. Worker teardown is not required for BORROWED ownership.

## Required tests before acceptance

### Registry compatibility and bypass
1. Existing generic registration + generic completion retains current behavior, including untouched cursor slots.
2. Strict-policy registration + generic completion is rejected even when all cursor slots are NOT_ATTEMPTED.
3. Generic-policy registration + strict completion is rejected.
4. Strict completion rejects wrong kind, stale generation, wrong owner, nonzero leases, invalid stored mask (rejected at registration), missing required step, retryable/partial/unknown/blocked/in-progress effects, and non-SUCCESS committed outcomes.
5. Strict completion accepts exactly the required COMMITTED/SUCCESS slots and exactly NOT_ATTEMPTED/NOT_EVALUATED for all skipped slots.
6. Reclamation remains atomic with the policy and kind checks; stale token is rejected after success.

### Orchestrator ownership policy
1. OWNED Context cannot close until all eight steps are successful.
2. OWNED abnormal Worker Join, terminal failure, degraded Shutdown, or failed Destroy prevents Context reclamation.
3. BORROWED Context closes after steps 0Ã¢â‚¬â€œ4 succeed while steps 5Ã¢â‚¬â€œ7 remain untouched.
4. BORROWED path never invokes Worker Shutdown/Join/Destroy.
5. BORROWED still documents external Worker lifetime responsibility.
6. Direct generic Registry completion cannot bypass the Orchestrator policy.
7. Owner transfer/reacquisition preserves the cursor and strict close policy.

## Compatibility and implementation cautions
- Do not globally tighten `raya_orch_complete_close`; generic Registry tests and potential external clients rely on its permissive semantics.
- Do not add a new outcome enum merely to label BORROWED skips.
- Never accept a caller-supplied required mask at close time. The mask must be fixed atomically at registration from validated Orchestrator ownership metadata; Registry checks that stored mask and expected object kind under its exclusive lock.
- The Context ownership field is read only while exact cleanup ownership is held and leases are zero.
- Keep the low-level Registry generic: it enforces registered policy, object kind, cursor states, and atomic reclamation; Orchestrator defines which steps are required.
- No Worker lifecycle implementation is included in this design slice.
- This is an in-process lifecycle policy only; it does not create durable cross-process cleanup recovery.

## Verified file identities

| File | Bytes | SHA-256 |
|---|---:|---|
| raya-orchestrator-registry-v1.h | 5,187 | `032574EF501ED535F600CC0E9F932C42F1D0BB43907BB7851ABE5DF8FAED5CAE` |
| raya-orchestrator-registry-v1.c | 15,097 | `7805F1780BF44E8A40A3A2F0B9E26E22A304549A546FCAAEDDEEFB398073185D` |
| raya-orchestrator-v1.h | 4,725 | `A73F9961F7C02EE3F64488DC7920BDD092BA4F5763FF05A74A2B4FBF6B342EC7` |
| raya-orchestrator-v1.c | 18,950 | `80073CBFFDCBBF96B01879562506616F0ED2E969081D0F50C32BE56D8E534F25` |
| raya-orchestrator-registry-v1-test.c | 8,056 | `DE538121939BB16BDDBB5FDE5660179D9A24A8393602F17AD15F3BBFAABDB0FE` |
| raya-orchestrator-v1-test.c | 30,512 | `DB493C621EFB0C331A30341076150EFF5943A6CB2A58C8C9FE0CB16220AAD06C` |

## Result
Design review completed. No files other than this new report were written. No tests/builds were run. Phase 1 remains NOT ACCEPTED; Production remains NO-GO.
