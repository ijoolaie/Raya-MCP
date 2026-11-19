# Retained Snapshot Owner/Finalizer Consistency Validation — 2026-11-18

## Scope
Working Copy only. Test-only retained-snapshot validation was strengthened. No production source, public API, runtime transition, recovery behavior, build script, or README was changed.

## Source-backed invariants added
- Owner states CALLER_TERMINAL, REAPER_TERMINAL, and RELEASED require exactly one terminal collection; all other owner states require zero.
- Finalizer close attempts cannot exceed two.
- Successful closes cannot exceed close attempts or two.
- HANDLES_CLOSED and RELEASED require exactly two close attempts and two successful closes.
- CLOSE_UNCERTAIN requires at least one unsuccessful close attempt (attempts > successful closes).
- Existing release-commit/state equivalence and owner/finalizer cross-plane checks remain in force.

## Tests added
- Reject terminal Owner state with zero terminal-collection evidence.
- Reject two successful Finalizer closes with only one recorded close attempt.
- Existing valid released, pre-handoff pending, Worker-pending, and uncertain-close fixtures remain covered.

## Validation
- Normal build: `build-integrated-submit-adapter-v2-api-test.cmd`; Visual Studio 2026 Build Tools 18.7.3, x64, `/W4 /WX /TC`; 10 consecutive runs, each 751 assertions across 66 cases; 7,510 total assertions; exit 0.
- Test seam: `/DRAYA_ADAPTER_TESTING`, same source set and compiler warnings; 916 assertions across 68 cases; exit 0.
- The seam build generated 15 expected executable/object artifacts. All 15 exact artifacts were removed; zero remained.
- No runtime components or processes were stopped. No retained resource was released, replayed, or reconciled.
- Disk free after cleanup: C: 13,842,337,792 bytes; D: 4,811,374,592 bytes.

## Changed file
- `integrated-submit-adapter-v2-api-test.c` — 51,402 bytes; SHA-256 `7A26E939C86FA31C39F6C6F28D83381A4F0BF39A8183AB27B78AC7D0AF4A79E5`.

## Unchanged build script
- `build-integrated-submit-adapter-v2-api-test.cmd` — SHA-256 `A3EA896889A3C93813BB953775D90886C32A835DC402A22288474FA14AA603CE`.

## Limits
- This is a test-only snapshot validator; it does not make the snapshot atomic across Adapter, Worker, and Submitter.
- It does not add a production retained-resource enumeration/reconciliation/release API.
- It does not prove recipient commit, durable recovery, or safe replay of uncertain WRITE operations.

## Repository handling
No commit or reset. Existing uncommitted changes were preserved. Production and README were not modified.
