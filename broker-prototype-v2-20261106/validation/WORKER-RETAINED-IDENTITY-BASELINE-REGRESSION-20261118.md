# Worker Retained Identity Baseline Regression — 2026-11-18

## Scope
Revalidated the existing Worker/Adapter retained paths and ran the two existing focused native suites before attempting an identity implementation. Working Copy only:
`D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`.

## Baseline test results
- `build-integrated-submit-adapter-v2-api-test.cmd`: PASS, 10 consecutive runs, 751 assertions / 66 cases per run (7,510 assertion executions total).
- `build-managed-pipe-io-v2-native-test.cmd`: PASS, 10 consecutive runs, 88 assertions per run (880 assertion executions total).
- Both scripts returned exit code 0.
- Both scripts' declared output sets were absent before execution.
- After execution, all 18 declared EXE/OBJ outputs across the two scripts were absent. Script cleanup succeeded; no manual deletion was needed.
- Disk after tests: C: 14,123,892,736 bytes free; D: 4,811,149,312 bytes free.

## Confirmed implementation boundary
- Direct managed-pipe retention publishes through `retain_locked` and a separate caller-finish failure branch.
- Adapter-associated Worker retention is published by `retain_envelope`, independently of Adapter-envelope retention.
- The Adapter's pre-Worker-admission failure paths can retain an envelope before `raya_managed_operation_init` has successfully initialized its embedded operation.
- `raya_managed_operation_init` clears the entire operation with `memset`; metadata written before it would be erased.
- Consequently, not every Adapter retained envelope is necessarily a valid initialized Worker operation. Treating every Adapter envelope as a Worker operation inventory record would misclassify partially initialized objects. This is a real boundary, not merely a missing field.
- The test-only snapshot currently stores pointer diagnostics in `operation_id`; those addresses are used by existing cross-plane comparisons. They must remain explicitly address-only diagnostics if stable IDs are added.

## Changes
- Created this report only.
- No production source, header, test, build script, README, model, or configuration changed.
- No Commit or Reset.
- No manual artifact deletion.

## Next implementation steps
1. Define and test an explicit distinction between a valid initialized Worker operation and an Adapter-only retained envelope whose embedded operation initialization failed.
2. Preserve Adapter-envelope quarantine for pre-init failures, but do not publish those objects as Worker operation inventory entries.
3. Add Worker epoch and monotonic operation identity only to successfully initialized Worker operations; ensure registration is ordered after the initializer's memset and before any later retention path.
4. Route all actual Worker retained-list insertions through one lock-protected, duplicate-safe publication helper.
5. Add Worker first-retain cause/stage and explicit Adapter association metadata; retain pointer values only as address diagnostics.
6. Extend the test-only snapshot and validator additively, then add deterministic tests for initialized direct operations, Adapter-associated operations, pre-init Adapter-only retention, duplicate publication, and identity immutability.
7. Re-run the two focused suites and inspect every declared output/cleanup result.

## Status
- Existing focused baseline: PASS.
- Worker identity implementation: NOT STARTED.
- Identity-specific tests: NOT STARTED.
- Stage 5 implementation/recovery acceptance: BLOCKED / NOT ACCEPTED.
- Phase 1: NOT ACCEPTED.
- Production: NO-GO.
