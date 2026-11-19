# Submitter First-Failure Diagnostics Slice — 2026-11-17

## Result

Added provisional first-failure diagnostics to the Submitter structure and runtime degradation helper. The record is captured only on the first call to `degrade_locked` and preserves:
- reason = `UNCLASSIFIED` (deliberately honest; runtime call sites are not yet classified individually);
- lifecycle value immediately before the first DEGRADED transition;
- `GetLastError()` value observed at that point;
- queue, active producer, active issue, and issuer lease counters.

The public header explicitly labels this data diagnostic-only and not recovery authorization. No API was added to clear DEGRADED or transition to STOPPED. Destroy preconditions and component behavior were not relaxed.

## Tests

Built with MSVC x64 `/W4 /WX` through the existing failure-matrix script. The matrix now has 227 assertions per run and passed 25 consecutive runs: **5,675 assertions passed, 0 failed**.

Four new assertions verify the first-failure record in the multi-signal Shutdown failure case:
- first reason is UNCLASSIFIED;
- pre-transition lifecycle is STOPPING;
- queue and active producer snapshots are zero;
- active issue and issuer lease snapshots are zero.

This is a focused diagnostic regression, not proof of recovery correctness.

## Explicit limitations / remaining work

1. Runtime failure sites still converge on UNCLASSIFIED. They must be classified individually (wake reset/wait, queue invariant, request effect, notification, shutdown signal).
2. The declared STARTUP_ROLLBACK and DESTROY_CLOSE reason values are not yet wired to all direct lifecycle assignments. Startup rollback and Destroy close paths still need first-failure capture and tests.
3. The record stores aggregate counters, not a complete request ledger or stable request identity. It cannot authorize request reclamation, retry, or recovery.
4. `GetLastError()` may be zero or unrelated for internal invariant failures. A later typed failure-record API must carry an explicit error parameter rather than infer it.
5. No `reconcile_degraded_ex` API, external evidence contract, Orchestrator cursor integration, or STOPPED transition was implemented in this slice.
6. No Worker lifecycle integration was attempted.

## Changed files and SHA-256

- `dedicated-io-submitter-v2.h`: `27A58EA6C214371D3F9EF1A2E1C56F809D63E5CDD2DE6B03981E26EBD50BC4E2`
- `dedicated-io-submitter-v2.c`: `E91DF8DCBE8FAD70EC973BC9BCFA269CA0B09844F8E6E93FB816E252B88E8D45`
- `dedicated-io-submitter-v2-failure-test.c`: `2102077962D121EAA70675EB2F56259146F553F876F3F45F76E698223EE94E5C`

## Environment and safety

- Build/test script removed its targeted EXE/OBJ outputs; post-run check found none.
- No active cl/link/Java/TLC or target test process remained.
- Free space: C 16,540,516,352 bytes; D 2,111,582,208 bytes.
- No process was killed, no install/download, no Git commit/reset, no Production change, and no TLA+ rerun.
- Working Copy only. Existing uncommitted work was preserved.

## Next slice

Classify and capture every remaining DEGRADED origin with explicit reason/error/request-state inputs. Add startup rollback and Destroy close diagnostics, then test each classification. Only after the evidence record is complete should the separate reconciliation evidence contract and owner-only recovery API be designed.
