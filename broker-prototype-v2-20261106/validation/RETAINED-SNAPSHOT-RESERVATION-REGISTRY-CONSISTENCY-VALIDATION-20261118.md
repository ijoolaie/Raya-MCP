# Retained Snapshot Reservation/Registry Consistency Validation — 2026-11-18

## Scope
Working Copy only. Strengthened the test-only retained-snapshot validator to cross-check reservation state, operation state, and per-operation registry membership. No production source, public API, runtime transition, build script, or README changed.

## Source-backed constraints added
- HELD reservation: no Worker registry membership; operation remains CALLER_PENDING or CALLER_TERMINAL.
- HANDED_OFF reservation: operation is QUEUED or WORKER_PENDING.
- TERMINAL_HELD reservation: operation is TERMINAL_HELD and absent from the Worker registry.
- RELEASED reservation: absent from the Worker registry; operation is CALLER_TERMINAL, WORKER_TERMINAL, or RELEASED.
- A positive per-operation registry match requires HANDED_OFF reservation and WORKER_PENDING operation.
- Existing checks for released Owner/Finalizer evidence and registry multiplicity remain active.

These constraints reflect the transitions in reaper-integrated-v2.c: reserve/unreserve, handoff, promotion/registry insertion, terminal collection, terminal hold, and terminal-held release.

## Tests
- Added rejection for a RELEASED reservation that still has registry membership.
- Added rejection for a HELD reservation paired with a Worker-pending/released operation.
- Corrected the synthetic HANDED_OFF fixture to represent the actual QUEUED state after handoff.
- Normal build: `build-integrated-submit-adapter-v2-api-test.cmd`; Visual Studio 2026 Build Tools 18.7.3 x64 `/W4 /WX /TC`; 10 consecutive runs, each 751 assertions across 66 cases; exit 0.
- Test seam: `/DRAYA_ADAPTER_TESTING`; 918 assertions across 68 cases; exit 0.
- The test seam generated 15 exact executable/object artifacts. All 15 were removed; zero remain.
- Disk free after cleanup: C: 13,839,769,600 bytes; D: 4,811,366,400 bytes.

## Changed file
- `integrated-submit-adapter-v2-api-test.c` — SHA-256 `A6BCBF5F9CD8FEFCEAA46C2501F4BB72A76418BC053E19EF505C5D94B7167D7A`.

## Unchanged build script
- `build-integrated-submit-adapter-v2-api-test.cmd` — SHA-256 `A3EA896889A3C93813BB953775D90886C32A835DC402A22288474FA14AA603CE`.

## Limits / remaining blockers
- This remains a test-only validator over a diagnostic snapshot. Snapshot collection reads Adapter and Worker planes under separate locks; it is not an atomic cross-component snapshot.
- Per-operation registry membership is counted, but production enumeration/reconciliation/release APIs do not exist.
- Global reserved-slot/outstanding/queued counters are captured, but a single per-item snapshot cannot prove a complete aggregate accounting identity across every concurrent lifecycle transition.
- No retained resource was released, replayed, or reconciled. No production runtime behavior changed.

## Repository handling
No commit or reset. Existing uncommitted changes were preserved. Production and README were not modified.
