# OWNER-ONLY CONTEXT RETRIEVAL AFTER LEASE DRAIN — 2026-11-17

## Scope
Added a narrow Registry/Orchestrator API for a Cleanup Owner to retrieve its registered Context after admission has transitioned to CLOSING and all API leases have drained.

The API does not execute component operations and does not call Adapter, Submitter, or Worker while holding the Registry lock.

## Changes
1. `raya-orchestrator-registry-v1.h`
   - Declares `raya_orch_owner_get_object`.
   - Documents exact-owner, closed-admission, zero-lease requirements and the pointer lifetime contract.
2. `raya-orchestrator-registry-v1.c`
   - Implements the owner-only lookup.
   - Validates Registry, nonzero owner, expected object kind, generation-bearing Handle, CLOSING state, exact owner token, zero lease count, and non-NULL object under the Registry shared lock.
   - Clears output before validation and returns no pointer on failure.
   - Releases the Registry lock before returning the object pointer.
3. `raya-orchestrator-v1.h`
   - Declares `raya_orchestrator_owner_get_context`.
4. `raya-orchestrator-v1.c`
   - Uses the Registry owner-only lookup, then validates Context magic, registration, Registry/Handle identity, component pointers, and Owned/Borrowed policy after the Registry lock has been released.
5. `raya-orchestrator-v1-test.c`
   - Adds coverage for retrieval before BeginClose, wrong owner, outstanding leases, successful retrieval after drain, and both Borrowed and Owned contexts.

## Verification
- MSVC x64 build/link with `/W4 /WX`: PASS.
- Orchestrator facade regression: 10 consecutive runs ×29 assertions = 290 passed, 0 failed.
- Registry regression: 50 passed, 0 failed.
- Total this run: 340 assertions passed.
- Build scripts removed the test executable and OBJ files. Final artifact/process checks are recorded separately after the test.
- No component lifecycle function was invoked by the new owner retrieval path.

## Contract / limitations
- The owner token is a bearer capability. All operations using the same owner token must be serialized by one cleanup executor; concurrent calls with a copied owner token are outside the supported contract.
- The Context and Registry storage must remain alive until the owner finishes and Registry close is completed.
- The API only returns a Context pointer. It does not prove that external direct callers of Adapter/Submitter/Worker have quiesced.
- Adapter Destroy still has a Boolean result that conflates "admission closed but entrants/retained work remain" with other failure causes. It needs a structured result before the cursor can safely classify all failures.
- Submitter/Worker `_ex` operations still need explicit preconditions, cursor mapping, and owner serialization. No Submitter Shutdown/Join/Destroy or Worker lifecycle call was wired or executed in this slice.
- Submitter DEGRADED shutdown and Worker DEGRADED shutdown remain reconciliation-required, not automatically retryable.
- No chained Owned/Borrowed cleanup matrix or concurrent cleanup-owner race test is claimed.

## Safety / project state
- Working Copy only: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`.
- No commit or reset; README untouched.
- Source project and Production unchanged. Phase 1 remains NOT ACCEPTED; Production remains NO-GO.
- IranPriceScout and independent port 8765 tunnel untouched.
- No process terminated; no install/download performed.
