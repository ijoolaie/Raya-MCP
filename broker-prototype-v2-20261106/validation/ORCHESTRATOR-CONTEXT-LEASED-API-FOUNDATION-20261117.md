# ORCHESTRATOR CONTEXT AND LEASED API FOUNDATION — 2026-11-17

## Scope
Implemented the first internal Orchestrator facade slice in the Working Copy only:
- caller-owned Context registration in the existing generation-token Registry;
- typed Handle acquisition and API Lease release;
- Worker ownership policy metadata (Owned/Borrowed);
- component pointer access only through an active Orchestrator Lease;
- active lease identity validation to reject stale copied lease values after the original lease is released.

No Adapter, Submitter, or Worker lifecycle operation is invoked by this slice. No Production/source-project change was made.

## Files changed
1. `raya-orchestrator-v1.h` — new, 2,410 bytes.
2. `raya-orchestrator-v1.c` — new, 4,352 bytes.
3. `raya-orchestrator-v1-test.c` — new, 4,907 bytes.
4. `build-raya-orchestrator-v1-test.cmd` — new, 634 bytes.
5. `raya-orchestrator-registry-v1.h` — modified, 4,583 bytes.
6. `raya-orchestrator-registry-v1.c` — modified, 14,163 bytes.

## Contract implemented
- Context storage is caller-owned, initialized through `raya_orchestrator_context_init`, and must not be copied or relocated after registration.
- Context storage and Registry must outlive all Handles/Leases and Registry reclamation.
- Registration stores only the Context address in the Registry and returns the Registry's generation-bearing 64-bit Handle.
- Acquire delegates to typed Registry Acquire. The Context is dereferenced only after that operation returns a valid Lease.
- Accessors expose Adapter, Submitter, Worker, and ownership policy only through a live Orchestrator Lease.
- Registry lock is not held while the facade validates Context fields or while consumers later invoke component APIs.
- Lease values remain move-only by contract. A new `raya_orch_lease_is_active` helper checks the unique lease identity under the Registry shared lock; a copied stale lease cannot expose the Context after the original lease has been released.
- Worker ownership is explicit metadata. This facade does not take over storage ownership or perform teardown.

## Verification
- MSVC x64 compile/link with `/W4 /WX`: PASS.
- Orchestrator facade test: 10 consecutive runs, 24 assertions/run, 240 passed, 0 failed.
- Registry regression test after Registry API extension: 1 run, 50 assertions passed, 0 failed.
- The initial facade test build exposed one test-only local-variable shadow warning (C4456); the test was corrected and the clean build then passed. No production-code warning remained.
- Test executables and object files were removed by the build scripts. Final artifact checks confirmed all six expected temporary EXE/OBJ paths absent.
- No cl/link/java/tlc processes remained after verification.
- Final free disk: C approximately 12.69 GB; D approximately 2.11 GB.

## SHA-256
- `raya-orchestrator-v1.h`: `32D8B2499A58EEA832F9C90C8750F36F401B06141AD64F1429252BCAD192C926`
- `raya-orchestrator-v1.c`: `FD247B07B64275975FECCDC71B03730909FAB74B2264A186FD9F6405DF34BB27`
- `raya-orchestrator-v1-test.c`: `102E2F7CCC68933E479E68E451B90EBF4EDF3F74487AC54F1725456982BC0F14`
- `raya-orchestrator-registry-v1.h`: `E211355A881F7BF9B651C529657CB28AFE920981C04175FF87F29810545637E9`
- `raya-orchestrator-registry-v1.c`: `8F3292226412130E4B6BC480DD85DA71F97945FB55B2CE90FAFE9AA189120C67`
- `build-raya-orchestrator-v1-test.cmd`: `50D04F5527E3C43887FC4478A7EC3E758B02234D4A4B4D4DA1F2A000CF467E56`

## Remaining blockers / next slice
1. Add an owner-only Registry operation to retrieve the registered Context after BeginClose and lease drain; it must validate Handle, CLOSING state, exact cleanup owner, expected object kind, and zero leases while holding the Registry lock, then return the pointer for use after unlocking.
2. Implement the serialized cleanup runner with durable-in-memory cursor transitions and explicit effect/outcome mapping.
3. Preserve required order: close Adapter admission and finish Adapter Destroy first; then Submitter Shutdown/Join/Destroy using `raya_submitter_*_ex`; then Worker Shutdown/Join/Destroy only when ownership is Owned. Borrowed Worker is never torn down by this owner.
4. Resolve Adapter Destroy false-result semantics and Submitter/Worker DEGRADED reconciliation before claiming safe automatic retry.
5. Add chained Owned/Borrowed failure injection, concurrent close, stale-handle, and lease-drain tests. No end-to-end chained lifecycle test is claimed here.

## Safety / project state
- Working Copy only: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`.
- No commit or reset.
- README untouched.
- Source project and Production unchanged; Phase 1 remains NOT ACCEPTED and Production remains NO-GO.
- IranPriceScout and its independent port 8765 tunnel untouched.
- No process terminated; no installation or download performed.
