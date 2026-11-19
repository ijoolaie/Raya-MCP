# Orchestrator Registry Lease Identity Hardening — 2026-11-17

## Scope
Working Copy only:
`D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`

A focused correctness defect was found in the existing stable-handle registry: `raya_orch_lease` was a copyable value, while release validated only the slot, object pointer, and aggregate lease count. A copied lease could therefore be released twice; the second release could decrement another caller's outstanding lease and allow premature drain/reclamation.

## Changes
1. `raya-orchestrator-registry-v1.h`
   - Added a bounded 256-entry lease identity table per registry.
   - Added monotonically increasing 64-bit lease IDs to lease values and registry state.
   - Documented lease values as move-only identities; copying a lease does not create a second releasable lease.
2. `raya-orchestrator-registry-v1.c`
   - Acquire now reserves a unique active lease record while holding the registry lock, then increments the slot lease count.
   - Exhausted lease identity space fails closed; full lease table returns `RAYA_ORCH_CAPACITY`.
   - Release now requires an exact active identity match (ID + handle + object), invalidates that record exactly once, and only then decrements the slot lease count.
   - Duplicate/stale copied lease release is rejected without changing the live lease count.
3. `raya-orchestrator-registry-v1-test.c`
   - Added regression coverage: acquire a second lease, copy its value, release the original, then verify the duplicate copy is rejected.
   - Existing live lease continues to block close/drain until its own release.

## Verification
- Build: existing `build-raya-orchestrator-registry-v1-test.cmd`, MSVC x64, `/W4 /WX`.
- 10 consecutive build-and-run executions.
- Each run: 28 assertions passed, 0 failed.
- Total: **280 assertions passed**.
- Test build script removed its EXE/OBJ outputs. Post-run check found no test EXE/OBJ artifacts.
- Post-run process inventory: no `cl.exe`, `link.exe`, `java.exe`, or `tlc.exe`.
- Disk free after test: C 12,325,478,400 bytes; D 2,109,534,208 bytes.

## Changed-file SHA-256
- `raya-orchestrator-registry-v1.h` — 03A9848191A6991CA76ACC4E3B0924DF4BD5A701286EDB528FC91E27C31FA1DE (3,734 bytes)
- `raya-orchestrator-registry-v1.c` — 91C2FE673083E55FC125246A366BCA9336337342A7BDE149AAB7ADC5F6BA80D2 (9,504 bytes)
- `raya-orchestrator-registry-v1-test.c` — 290B68331ED3712E8B8791655A1DE9A3185F9FAB59DF2593BCEEAA5F79749931 (4,553 bytes)

## Limits / next work
- This remains a registry primitive, not an Orchestrator facade and not wired to Adapter, Submitter, Worker, or managed-pipe.
- Lease capacity is bounded at 256 simultaneous leases per registry.
- Lease values must not be concurrently released or used after release; the registry identity table prevents duplicate copied values from decrementing twice.
- Registry storage still must outlive all handles, leases, and cleanup operations.
- No proof yet of callback self-destroy prevention, complete public API lifetime safety, startup rollback, Worker structured lifecycle results, or full chained Owned/Borrowed cleanup.
- No Production/source-project changes; no commit/reset; README untouched; IranPriceScout and its port-8765 tunnel untouched.
