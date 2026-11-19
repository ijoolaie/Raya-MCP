# Retained Snapshot Operation/Collection State-Domain Validation — 2026-11-18

## Scope
Working Copy only. This change strengthens the test-only retained-snapshot validator; it does not alter production code, runtime transitions, ownership, or recovery behavior.

## Source-backed state domains
- `raya_reaper_op_state` is explicitly enumerated from `RAYA_REAPER_OP_CALLER_PENDING=1` through `RAYA_REAPER_OP_CALLER_TERMINAL=7` in `reaper-core-v2.h`.
- `raya_integrated_collect_state` is explicitly enumerated from `RAYA_INTEGRATED_COLLECT_PENDING=0` through `RAYA_INTEGRATED_UNCERTAIN=2` in `reaper-integrated-v2.h`.
- Reservation state remains bounded by `RAYA_INTEGRATED_RESERVATION_TERMINAL_HELD`; the existing validator already checks that domain.
- Collection success leaves collection state at COLLECTING while the operation may progress through Worker terminal/release. Therefore no simplistic equality between collection state and operation state was imposed.
- An uncertain completion can retain a Worker operation with a terminal-held reservation; it is not treated as a successful terminal release.

## Changes
- `validate_retained_snapshot()` now rejects operation-state values below CALLER_PENDING or above CALLER_TERMINAL.
- It rejects collection-state values above UNCERTAIN.
- Synthetic validator fixtures now explicitly initialize operation and collection state rather than leaving those newly inspected fields indeterminate.
- Added negative cases for operation state zero and collection state value 3.
- No production `.c`/`.h`, public API, build script, or README changed.

## Validation
- Normal build: `build-integrated-submit-adapter-v2-api-test.cmd`, Visual Studio 2026 Build Tools 18.7.3, x64, `/W4 /WX /TC`; 10 consecutive runs, each 751 assertions across 66 cases; 7,510 total assertions; exit 0.
- Test seam: same source set compiled with `/DRAYA_ADAPTER_TESTING`, x64, `/W4 /WX /TC`; 914 assertions across 68 cases; exit 0.
- The seam build generated 15 expected executable/object artifacts. All 15 exact artifacts were removed; zero remained.
- No C/C++ runtime component was stopped. No retained resource was released, replayed, or reconciled.

## Limits
- Snapshot remains test-only, raw-pointer diagnostic data gathered under separate locks; it is not an atomic cross-component snapshot.
- Enum-domain validation does not prove recipient commit, durable recovery, or safe replay of uncertain WRITE operations.
- No production retained enumeration/reconciliation/release API was added.

## Changed file
- `integrated-submit-adapter-v2-api-test.c` — 49,710 bytes; SHA-256 `D9DA18D1C1BA6D72DF0A6F423B027D558E6F7EECF2F5B7F49D86BCFE633A3674`

## Repository handling
No commit or reset. Existing uncommitted changes were preserved. Production and README were not modified.