# Orchestrator Cleanup Owner Recovery Primitives — 2026-11-17

## Scope
Extended the existing process-lifetime `raya-orchestrator-registry-v1` with explicit, fail-closed cleanup-owner yielding, reacquisition, and evidence-based cursor reconciliation. This is a Registry primitive slice, not a public Orchestrator facade or component integration.

## Changes
- `raya-orchestrator-registry-v1.h`
  - Documented that BeginClose can claim a CLOSING slot only after the prior owner explicitly yielded.
  - Declared `raya_orch_release_cleanup_owner`.
  - Declared `raya_orch_cursor_reconcile`.
- `raya-orchestrator-registry-v1.c`
  - BeginClose now elects an owner for OPEN slots or CLOSING slots whose owner is zero; all other states remain BUSY.
  - Owner release requires the exact current owner, zero outstanding API leases, and no IN_PROGRESS, UNKNOWN, or BLOCKED cursor steps.
  - Owner release preserves slot state and all cursor history; it only clears the owner token.
  - Reacquisition assigns a fresh monotonically increasing owner token without reopening admission or resetting cursor history.
  - Explicit reconciliation accepts only determinate outcomes: NOT_ATTEMPTED + RETRYABLE_FAILURE (caller has evidence no side effect occurred), or COMMITTED + SUCCESS/ABNORMAL_EXIT/TERMINAL_FAILURE (caller has evidence the effect is committed). The exact owner and a currently IN_PROGRESS/UNKNOWN/BLOCKED step are required.
- `raya-orchestrator-registry-v1-test.c`
  - Covers refusal to yield while a step is uncertain, refusal to yield while leases remain, evidence-based resolution, stale-owner rejection, ownership transfer/reacquisition, cursor/attempt persistence, and duplicate lease release rejection.

## Verification
- MSVC x64 build: PASS with `/W4 /WX`.
- Registry tests: 10 consecutive runs, each 50 passed / 0 failed; total 500 assertions passed.
- Test EXE and both OBJ outputs removed; final artifact check confirms absent.
- Final process check: no `cl.exe`, `link.exe`, `java.exe`, or `tlc.exe` processes.
- Disk free after verification: C: approximately 12 GB; D: approximately 2 GB.

## Changed-file hashes
- `raya-orchestrator-registry-v1.h` — 4,366 bytes — SHA-256 `417E7A5174B6A09436F156BC74A491B124F8971C99FFFFBD5481F665E173737C`
- `raya-orchestrator-registry-v1.c` — 13,443 bytes — SHA-256 `E25310FA78F8EB7F4C70ACB573F78A6827BECBF3941F3B4DB3BBD378C4E0BA53`
- `raya-orchestrator-registry-v1-test.c` — 8,056 bytes — SHA-256 `DE538121939BB16BDDBB5FDE5660179D9A24A8393602F17AD15F3BBFAABDB0FE`

## Safety properties and limitations
- A cleanup owner cannot be silently stolen after a crash. A slot left with a nonzero owner remains fail-closed; explicit yield is required before a new owner can claim it.
- A step left IN_PROGRESS/UNKNOWN/BLOCKED prevents owner yield until a caller with external evidence explicitly reconciles it. This API cannot independently prove that evidence; its caller contract must require a trustworthy component snapshot/result or operator decision.
- Owner yield is blocked until all pre-close API leases drain.
- This does not implement durable cross-process recovery; the registry and cursor remain in-memory.
- No Submitter/Worker `_ex` calls are wired to this cursor yet. No public Orchestrator facade, Owned/Borrowed teardown chain, startup rollback, or chained component failure matrix was implemented or claimed.
- No component API is called under the registry lock.

## Safety
- Working Copy only.
- No commit/reset; README untouched.
- Source project and Production unchanged.
- IranPriceScout and port-8765 tunnel untouched.
- No process terminated; no install/download performed.
