# Adapter Retained First-Stage Classification — 2026-11-18

## Scope

Working Copy only:
`D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`

This follow-up extends the prior identity/first-cause slice with a first-retain stage classification. It does not introduce production enumeration, reconciliation, retry, release, unlink, free, or I/O replay.

## Changes

### First-retain stage

Added `raya_adapter_retain_stage` and an immutable `first_retain_stage` field on each Adapter envelope. The field is written only on the first transition into Adapter retained inventory and exposed through the existing test-only retained snapshot.

Current recorded classifications:
- `HANDLE_CLOSE_FAILURE`: retained because a handle-close path failed.
- `OWNERSHIP_INVARIANT`: retained because a component ownership/transition invariant failed.
- `SUBMITTER_ACK`: inferred only when the Worker terminal-release marker is already set and the first-retain cause is an ownership invariant (the ACK-failure path).

Other enum values reserve more granular future classifications (pre-issue setup, Submitter admission, Issue/handoff); those are not yet emitted by all call sites. Accordingly, this is a coarse failure-boundary classification, **not yet an exact per-callsite stage ledger**.

The first-retain cause and stage are immutable after the initial retain. Later Worker-wide degradation causes do not overwrite them.

### Test-only snapshot validation

The existing `RAYA_ADAPTER_TESTING` validator now rejects snapshots with:
- zero Adapter instance ID or operation ID;
- missing first-retain cause;
- UNKNOWN first-retain stage;
- terminal-released Adapter records not classified as Submitter ACK.

The synthetic validator fixture now initializes the added fields explicitly.

## Validation

Visual Studio 2026 Build Tools 18.7.3, x64, `/W4 /WX /TC`.

### Normal configured build
- `build-integrated-submit-adapter-v2-api-test.cmd`
- 10 consecutive runs.
- Each run: 751 assertions across 66 cases passed.
- Total: 7,510 assertions.
- Exit code 0.
- Script left no expected temporary artifacts.

### Test-seam build
- Explicit `/DRAYA_ADAPTER_TESTING` build and run.
- 897 assertions across 68 cases passed.
- Exit code 0.
- During development, an over-broad terminal-release inference and then an uninitialized synthetic test fixture each caused a focused test failure. Both were corrected; the final test-seam run passed.
- 15 expected temporary executable/object artifacts were removed after the run; zero remained.

### No destructive recovery behavior
No retained resource was released, retried, unlinked, closed manually, or freed by this change. No uncertain READ/WRITE was replayed. The production Adapter Destroy retained guard remains unchanged.

## Files changed

- `integrated-submit-adapter-v2.h`: stage enum and test snapshot field.
- `integrated-submit-adapter-v2.c`: envelope stage storage, first-retain assignment, stage classification and snapshot population.
- `integrated-submit-adapter-v2-api-test.c`: snapshot validation and synthetic fixture initialization.

No README, build script, project configuration, Orchestrator, Worker, Submitter, direct managed-pipe implementation, or Production entrypoint changed. No commit or reset.

## Verified fingerprints

- Header: `integrated-submit-adapter-v2.h` — 6,475 bytes, SHA-256 `AFFBC304014CD80557207DBE790663E2B99FCDE19496E160DEF3BD4765D35EBC`.
- Implementation: `integrated-submit-adapter-v2.c` — 29,250 bytes, SHA-256 `4112EFD3FB271284EA047FB9145042417C7C9E3D4302AED83A2C1A8C30F7D11B`.
- Tests: `integrated-submit-adapter-v2-api-test.c` — 43,619 bytes, SHA-256 `BD91FBCC23D09913136AD86C5F82D3F5178765726283E947EFBAE1E9CB3FA2E8`.

## Remaining gaps

- Stage classification is coarse and not yet exact at every retain callsite.
- No per-effect immutable ledger (Issue, handoff, completion, finalizer substeps, owner release, unreserve, ACK).
- Identity remains process-local and Adapter-scoped; direct managed-pipe Worker-only entries remain outside this namespace.
- No production copied snapshot or durable audit.
- No recipient-visible idempotency identity or authoritative receipt/status query.
- No safe retained reconciliation/release API.

Phase 1 remains **NOT ACCEPTED**. Production remains **NO-GO**.
