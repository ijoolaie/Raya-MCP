# Retained Snapshot Stable-Identity Validator Validation — 2026-11-18

## Scope
Updated the test-only Adapter/Worker retained snapshot validator and its synthetic regression cases in the Working Copy.

## File changed
- `integrated-submit-adapter-v2-api-test.c`

No production source/header, build script, README, model, configuration, or Production artifact changed in this slice.

## Validator changes
- Adapter-to-Worker membership now matches the stable pair `(adapter_instance_id, stable_operation_id)` for identity-bearing Adapter-associated Worker records.
- Address-only cross-plane matching is restricted to explicitly identity-less partial records.
- Worker diagnostic `operation_id` and `operation_address` must be nonzero and equal; they remain diagnostics, not the identity join key for identity-bearing records.
- Validates Worker identity shape by inventory kind:
  - DIRECT: nonzero Worker epoch/operation ID; no Adapter association; DIRECT or POST_TERMINAL first-retain stage.
  - ADAPTER_ASSOCIATED: nonzero Worker identity and complete nonzero Adapter pair; ADAPTER_ASSOCIATED or POST_TERMINAL stage.
  - Identity-less partial: zero identity and association fields; PARTIAL_RECORD or POST_TERMINAL stage.
- All identity-bearing records in one Worker snapshot must share the same Worker epoch.
- Rejects duplicate Worker epoch/operation IDs, duplicate Adapter association pairs, duplicate diagnostic addresses, and duplicate Adapter stable identity pairs.
- Adapter-only pending-I/O remains distinct from Worker retained-list membership.
- First-retain cause/stage must be populated and stage must be in-domain.

## Regression coverage
- Stable Adapter/Worker identity match succeeds even when their address diagnostics differ.
- Mismatched Adapter operation identity is rejected.
- Identity-less partial record uses the explicit address compatibility path and rejects mismatched address.
- DIRECT Worker inventory validates without Adapter inventory.
- Multiple DIRECT operations in one Worker epoch validate.
- Cross-epoch Worker entries and duplicate Worker operation IDs are rejected.
- Multiple Adapter-associated operations in one Worker epoch validate.
- Duplicate Adapter association pair and duplicate Adapter stable identity are rejected.
- Existing lifecycle, reservation, finalizer, owner, registry, and membership invariants remain covered.

## Validation results
1. Testing-hook build, compiled explicitly with `/DRAYA_ADAPTER_TESTING`:
   - 10 consecutive runs PASS.
   - 948 assertions per run, 68 cases.
   - 9,480 assertion executions total.
2. Standard `build-integrated-submit-adapter-v2-api-test.cmd`:
   - 10 consecutive runs PASS.
   - 769 assertions per run, 66 cases.
   - 7,690 assertion executions total.
3. Both build paths exited successfully.
4. All 15 declared API-test executable/object outputs absent after cleanup.
5. 14 pre-existing root `.obj/.exe` artifacts remain untouched; no manual deletion of those artifacts.
6. Disk after validation: C free 14,120,370,176 bytes; D free 4,810,530,816 bytes.

## Remaining work
- This validator remains test-only; it does not create production retained enumeration, reconciliation, release, or replay APIs.
- Need verify identity metadata on actual multi-operation runtime snapshots, including a mixture of DIRECT and ADAPTER_ASSOCIATED retained records, when deterministic test setup is available.
- Continue focused lifecycle/recovery tests before any Stage 5 acceptance decision.
- No commit/reset performed.

## Acceptance
- Stage 5 review/reporting checklist 5.1–5.15 remains complete.
- Stage 5 implementation/recovery acceptance: BLOCKED / NOT ACCEPTED.
- Phase 1: NOT ACCEPTED.
- Production: NO-GO.
