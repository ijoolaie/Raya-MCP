# Worker Retained Identity Publication Guard Validation — 2026-11-18

## Scope
Validated and hardened the Worker retained-publication boundary in the Working Copy only.

## Files changed in this slice
- `reaper-managed-worker-v2.c`
- `integrated-submit-adapter-v2-api-test.c`

## Implementation
- The Worker-lock-held retained publication helper now rejects a zero recovery cause and UNKNOWN/out-of-range retain stage.
- Identity-bearing records must belong to the publishing Worker's current nonzero epoch.
- DIRECT identity is accepted only with DIRECT or POST_TERMINAL evidence and no Adapter association.
- ADAPTER_ASSOCIATED identity is accepted only with a complete nonzero Adapter identity pair and ADAPTER_ASSOCIATED or POST_TERMINAL evidence.
- Identity-less partial records remain supported only with zero identity/association fields and PARTIAL_RECORD or POST_TERMINAL evidence.
- First-retain cause/stage remains immutable; an already populated stage with a zero cause is rejected.
- Added an always-built API test for successful publication, exactly-once list count, duplicate publication rejection, first-retain evidence immutability, cross-Worker-epoch rejection, and identity-less partial-record compatibility.

## Validation
- `build-managed-pipe-io-v2-native-test.cmd`: PASS; 10 runs × 88 assertions = 880 assertion executions.
- `build-integrated-submit-adapter-v2-api-test.cmd`: PASS; 10 runs × 769 assertions = 7,690 assertion executions.
- No test/build failure in this validation slice.
- Both scripts completed their declared cleanup.
- All 16 expected outputs checked for the two scripts were absent after execution.
- 14 pre-existing root `.obj/.exe` artifacts remain untouched; their purpose was not established as disposable.
- Disk after tests: C free 14,123,716,608 bytes; D free 4,810,555,392 bytes.

## Remaining work / limitations
- Test-only snapshot validator still uses legacy operation-address equality for some cross-plane checks. Stable Adapter/Worker identity comparison and explicit identity-less partial-record validation remain to be completed there.
- Snapshot currently exports copied Worker identity metadata and separate address diagnostics, but this pass did not change snapshot layout or production enumeration/reconciliation APIs.
- No positive recovery/release/replay capability was added. Quarantine semantics remain unchanged.
- No README, build script, model/configuration, or Production artifact changed.
- No commit/reset performed.

## Project acceptance
- Stage 5 review/reporting checklist 5.1–5.15 remains complete; Stage 5 implementation/recovery acceptance remains BLOCKED / NOT ACCEPTED.
- Phase 1: NOT ACCEPTED.
- Production: NO-GO.
