# Orchestrator Cleanup Cursor Transition Guards — 2026-11-17

## Scope
Hardened the existing `raya-orchestrator-registry-v1` cleanup cursor. No parallel registry or Orchestrator facade was created. Working Copy only.

## Changes
- `raya-orchestrator-registry-v1.c`
  - Rejects negative and out-of-range effect/outcome enum values.
  - A step can enter IN_PROGRESS only from its initial state or after an explicitly recorded RETRYABLE_FAILURE with no committed effect.
  - Each accepted attempt start increments the persistent attempt counter exactly once.
  - A result can be recorded only for an IN_PROGRESS attempt and must carry an evaluated outcome.
  - UNKNOWN/BLOCKED states cannot be blindly re-executed; they require reconciliation.
  - COMMITTED results cannot be changed or re-executed; an exact duplicate committed result is idempotently accepted.
- `raya-orchestrator-registry-v1-test.c`
  - Added coverage for foreign/stale owner rejection, retryable no-effect result, second attempt counting, committed-step re-execution rejection, and exact committed-result idempotence.

## Verification
- MSVC x64 build: PASS, `/W4 /WX`.
- Registry test: 10 consecutive runs, each 36 passed / 0 failed; total 360 assertions passed.
- Test EXE and both OBJ files removed by the test command's cleanup.
- Final artifact check: EXE/OBJ absent.
- Final process check: no `cl.exe`, `link.exe`, `java.exe`, or `tlc.exe` processes.
- Disk free after verification: C: approximately 11 GB; D: approximately 2 GB.

## Changed-file hashes
- `raya-orchestrator-registry-v1.c` — 10,759 bytes — SHA-256 `F48756155A343EEB95326A65EC97C9DD79120D7ED16D80B42D297F8AB4F46F88`
- `raya-orchestrator-registry-v1-test.c` — 5,941 bytes — SHA-256 `8CD7222E43399D07844A0698694EDE0ADC0ECD776E9E25C965531E2E4B0F55E0`

## Remaining limitations
- This change hardens the Registry cursor only; it does not wire Submitter/Worker `_ex` APIs into a public Orchestrator facade.
- Cleanup owner crash recovery/ownership transfer remains unspecified. A CLOSING slot remains owned by its elected token; no unsafe automatic takeover was added.
- Registry storage lifetime, lease table capacity, and caller serialization constraints remain as previously documented.
- No component calls are made while holding the registry lock; no component integration was introduced here.
- No claim of complete lifecycle orchestration, Phase 1 acceptance, or Production readiness.

## Safety
- No commit/reset.
- README unchanged.
- Source project and Production unchanged.
- IranPriceScout and its port-8765 tunnel untouched.
- No process was terminated.
