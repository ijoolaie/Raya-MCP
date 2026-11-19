# Adapter Test Snapshot — Terminal Collection and Completion Fields — 2026-11-18

## Change scope
Working Copy only. Extended the existing `RAYA_ADAPTER_TESTING` retained-item snapshot; no production API was added.

The test-only retained item now exposes:
- `terminal_collection_recorded`: true exactly when the owner ledger reports one terminal collection;
- `completion_error`: the operation's stored Win32 completion error;
- `transferred_bytes`: the operation's stored transfer count.

The snapshot copies these fields alongside the existing owner, finalizer, integrated-reservation, Worker, and Submitter diagnostics. No completion or lifecycle transition was changed.

## Semantics and limitations
- `terminal_collection_recorded` is deliberately named as an owner-ledger fact. It does not claim to distinguish caller-side immediate completion from Worker-side overlapped completion.
- `completion_error` and `transferred_bytes` are raw operation fields. Their interpretation depends on the operation path; they are not an application-level receipt.
- The test snapshot still requires Adapter admission closed, Worker joined, Submitter quiescent, and external direct callers quiesced.
- The Adapter, Worker, and Submitter portions are copied under separate locks. This is not an atomic cross-component snapshot.
- No recipient-stable identity, recipient receipt/status query, durable record, restart recovery, production enumeration, reconciliation, retry, release, or replay was added.
- Uncertain WRITE operations remain quarantined and must not be replayed absent recipient idempotency and evidence.

## Validation
Compiler: Visual Studio 2026 Build Tools 18.7.3, x64, `/W4 /WX /TC`.

1. Normal adapter API test build:
   - 10 consecutive runs;
   - 751 assertions across 66 cases per run;
   - 7,510 assertions total;
   - exit code 0.
2. Test-only `/DRAYA_ADAPTER_TESTING` build:
   - 908 assertions across 68 cases;
   - exit code 0.
   - Includes assertions that a post-terminal ACK-failure retained item has a recorded terminal collection and `ERROR_SUCCESS`, and that the immediate caller-terminal retained fixture has a recorded terminal collection.
3. Temporary artifacts:
   - Test-seam build produced 15 expected executable/object artifacts;
   - all 15 removed after execution;
   - 0 expected artifacts remained.
   - Normal build script also completed its own cleanup.

## Files changed
- `integrated-submit-adapter-v2.h`
- `integrated-submit-adapter-v2.c`
- `integrated-submit-adapter-v2-api-test.c`
- This report.

No build script, README, configuration, Worker/Submitter implementation, Orchestrator, or Production entrypoint was changed. No retained resource was released or retried. No commit/reset.

## Status
This is diagnostic coverage only, not retained-resource recovery or Phase 1 acceptance. Phase 1 remains NOT ACCEPTED; Production remains NO-GO.
