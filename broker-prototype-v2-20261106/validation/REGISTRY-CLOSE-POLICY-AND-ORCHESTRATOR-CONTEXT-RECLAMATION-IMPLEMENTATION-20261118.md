# Registry Close Policy and Orchestrator Context Reclamation — Implementation Report — 2026-11-18

## Scope and safety
Implemented the strict close-policy slice in the Working Copy only:
`D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`.

No commit, reset, production deployment, install, download, or process termination was performed. README was not edited. Existing unrelated uncommitted work was not intentionally modified.

## Implementation

### Registry policy metadata
- Added `raya_orch_close_policy`: GENERIC and REQUIRED_STEPS.
- Added per-slot `close_policy` and immutable `required_success_mask`.
- Added `raya_orch_register_with_policy`; it validates policy/mask and publishes object, kind, policy, mask, and OPEN state together under the Registry exclusive lock.
- Existing `raya_orch_register` remains a GENERIC-policy wrapper with mask zero.
- Invalid registration clears a non-null output handle to zero.
- Slot reclamation clears policy and mask before reuse.

### Close enforcement
- Existing `raya_orch_complete_close` now rejects REQUIRED_STEPS slots. Generic slots retain their previous permissive completion behavior.
- Added `raya_orch_complete_close_required`. Under one exclusive-lock critical section it validates exact handle generation, CLOSING state, cleanup owner, zero leases, expected object kind, strict policy, stored mask, and cursor state before reclaiming.
- Every required step must be exactly COMMITTED + SUCCESS.
- Every skipped step must remain exactly NOT_ATTEMPTED + NOT_EVALUATED with zero attempts/error/exit code.
- ABNORMAL_EXIT, TERMINAL_FAILURE, retryable, partial, unknown, blocked, or in-progress states cannot satisfy a required step.
- The required mask is read from the slot; the close caller cannot supply or weaken it.

### Orchestrator Context policy
- Context registration now atomically registers a strict policy.
- BORROWED Worker mask requires steps 0–4 (Adapter Destroy and Submitter admission close/Shutdown/Join/Destroy).
- OWNED Worker mask requires all eight steps (the same five plus Worker Shutdown/Join/Destroy in reserved slots 5–7).
- Added `raya_orchestrator_complete_close_owned`, which retrieves/validates the Context under the exact cleanup owner and delegates to strict Registry completion using the Context kind.
- BORROWED Worker does not require Worker teardown and no external-owner acknowledgement was added. The external owner remains responsible for the independent Worker lifetime.

### Tests added/updated
- Registry tests verify strict registration, invalid out-of-range mask, generic close bypass rejection, wrong-kind rejection, incomplete cursor rejection, owner transfer preserving strict policy/mask, successful exact completion, and stale-token rejection.
- Orchestrator tests verify direct generic close cannot bypass Context policy; direct strict close cannot skip lifecycle work; BORROWED closes only after common steps; OWNED remains blocked until all eight steps report success.
- Existing lifecycle failure tests continue to reject unhealthy Submitter cleanup and accept the BORROWED path only after common cleanup steps commit.

## Validation

### Registry test
Command: `build-raya-orchestrator-registry-v1-test.cmd`

Result: **64 passed; 0 failed**.

### Orchestrator integration test
Command: `build-raya-orchestrator-v1-test.cmd`

Result: **120 passed; 0 failed**, repeated 10 times = **1,200 passed; 0 failed**.

Combined final assertions: **1,264 passed; 0 failed**.

Compiler: Visual Studio 2026 Build Tools, x64, `/W4 /WX /TC`.

The build scripts removed their generated test executables and object files. Post-run check found **0** of the targeted temporary build artifacts remaining.

Disk space after tests: C: 9.37 GB free; D: 2.04 GB free.

## Changed files and SHA-256

| File | Bytes | SHA-256 |
|---|---:|---|
| raya-orchestrator-registry-v1.h | 6,066 | `F027F9B195B4BC6816CBDFF38E6184BF486832894539EFEE77002F261F9B6C3F` |
| raya-orchestrator-registry-v1.c | 17,909 | `DF5E821498E88ACDC9A01793248EFFA86922A8EDCBB5C761918C14F1CCCA0023` |
| raya-orchestrator-v1.h | 5,008 | `89DBDD546ADC4AB28FE7DA8567AFB75FADA8F4EEF43E9A52B8C1691EC1E561B7` |
| raya-orchestrator-v1.c | 19,782 | `6A67C182A73E2016221427E273E9C39CE29B4978E65BDC5621A1A553F8EDB002` |
| raya-orchestrator-registry-v1-test.c | 11,436 | `26D772016A85A2B0BEC439E1222FE042216A08A54BB0F4FCDEE4D972AC6E8105` |
| raya-orchestrator-v1-test.c | 32,410 | `A1A6185CDD47A29692E5628B9D5CB327CC6777FA9A507C918E793E3D1FEEA197` |

Updated design report:
- `validation\REGISTRY-CLOSE-POLICY-AND-ORCHESTRATOR-CONTEXT-RECLAMATION-DESIGN-20261118.md`
- 9,848 bytes
- SHA-256: `1DF97DBC928176D6F6957CECC3DCDA71434DF4A7082CC8541B9CB69A1BCF92FA`

This implementation report is a separate new file and was created without overwriting an existing file.

## Remaining limitations
- Worker lifecycle APIs/cursor execution (steps 5–7) are not implemented in this slice. OWNED Contexts correctly remain unreclaimable until those steps are later implemented and succeed.
- This policy is in-process Registry state; it is not durable across process crashes.
- The new registration policy API is a C API contract, not a security boundary against arbitrary in-process callers that deliberately misuse the API.
- No full end-to-end production entrypoint or live product composition has been established by these tests.

## Status
This closes the demonstrated direct Registry reclamation bypass for correctly registered Orchestrator Contexts. It does not complete Stage 5 or Phase 1.

Phase 1: NOT ACCEPTED.
Production: NO-GO.
