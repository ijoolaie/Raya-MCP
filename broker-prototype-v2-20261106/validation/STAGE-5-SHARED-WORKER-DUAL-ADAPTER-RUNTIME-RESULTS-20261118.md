# Stage 5 — Shared Worker Dual-Adapter Runtime and Snapshot Evidence (2026-11-18)

## Scope and outcome
Implemented a test-harness-only separation of Worker-level close dispatch from Adapter-level close dispatch, then executed a real two-Adapter / two-Submitter / one-Worker named-pipe experiment. Adapter A completed successfully; Adapter B completed Worker-side terminal resource release but its Submitter ACK signaling was deliberately failed, leaving B quarantined. This proves a controlled shared-Worker runtime path and per-Adapter fault isolation. It does not prove two independent retained Adapter submissions.

## Source changes
Only `integrated-submit-adapter-v2-api-test.c` changed:
- Added `arm_ack_on_worker_close` per-state control.
- Worker close dispatcher now owns the ACK-fault trigger and per-state Worker handle close accounting.
- Adapter API `test_close` no longer arms Submitter ACK failure; Adapter and Worker callback planes are independent.
- `setup_ack_failure` registers the state with the Worker-handle and ACK dispatchers and configures the Worker close callback once.
- Added `test_shared_worker_two_adapter_success_then_ack_quarantine`:
  - one Worker, two distinct Adapters, two independent Submitters;
  - two real named-pipe READ operations;
  - Adapter A completion and ACK success verified before submitting B;
  - ACK failure armed only for B;
  - B's Adapter and Worker stable identity, Worker epoch, terminal release, and retained state checked;
  - all tracked Worker-owned handles verified closed before dispatcher unregister.
- Under `RAYA_ADAPTER_TESTING`, added B Adapter/Worker retained-snapshot validation, including stable Adapter identity join and committed issue/publish effects.

No production source/header, build script, README, TLA+ model, or configuration changed.

## Runtime evidence
The new runtime test passed in both build configurations:
- Adapter A: callback exactly once; Worker close callback count 2; no ACK failure; Adapter retained count 0; active count 0.
- Adapter B: Worker close callback count 2; ACK failure injected only for B; callback not invoked; Adapter retained count 1; active count 1.
- Shared Worker: exactly one retained Worker operation; inventory kind ADAPTER_ASSOCIATED; Worker epoch and operation ID nonzero; Adapter identity pair matches B's instance ID and operation ID 1; Worker first-retain stage POST_TERMINAL; owner and finalizer both RELEASED; outstanding and reserved counts zero.
- Both Adapter instance IDs are nonzero and distinct.
- Both states' tracked Worker-owned handle slots are empty after Worker termination.
- Adapter A teardown and Submitter shutdown/join/destroy succeeded. Adapter B, its Submitter, and the Worker correctly remained non-destroyable due to retained/degraded state; no retained resources were released or replayed.

## Snapshot evidence
Testing-hook snapshot for Adapter B and the shared Worker passed:
- Adapter count = 1; Worker retained count = 1.
- Stable snapshot validator returned VALID.
- Adapter instance ID and stable operation ID match B.
- Worker record is identity-bearing ADAPTER_ASSOCIATED and joins to B by the stable identity pair.
- Snapshot shows terminal owner/finalizer release and committed issue/publish effects.
- Worker retained first-retain stage is POST_TERMINAL.

The clean Adapter A path was validated through its completion callback, zero retained count, and successful teardown; the post-quiescence retained snapshot was taken for B, the Adapter that remained quarantined. No claim is made that a retained snapshot was captured for already-destroyed A.

## Test results
1. Standard API test script, VS 2026 BuildTools 18.7.3 x64, `/W4 /WX`:
   - PASS, 10 consecutive runs.
   - 903 assertions / 66 cases per run.
   - 9,030 assertion executions.
2. Testing-hook build with `/DRAYA_ADAPTER_TESTING`, same source set:
   - PASS, 10 consecutive runs.
   - 1,112 assertions / 68 cases per run.
   - 11,120 assertion executions.
3. Standard script cleanup: all 15 declared EXE/OBJ outputs absent after script.
4. Testing-hook cleanup: exactly the 15 declared EXE/OBJ outputs removed; all 15 absent on follow-up check.
5. No other files were deleted. Existing older root EXE/OBJ artifacts, checkpoints, traces, model states, configs, reports, archives, and state stores were preserved.

## Stage 5 disposition
- 5.16 Multiple independent Adapter-associated retained submissions: BLOCKED / NOT COMPLETE. This experiment deliberately produces one clean Adapter completion followed by one retained Adapter. The Worker is degraded by B's ACK failure and exits; it cannot process another peer terminal completion. The earlier source trace establishes that a synchronous ACK failure in the Worker release callback degrades the shared Worker before a later operation can complete. Do not fake a second retained record or change production degradation/ACK architecture without authorization.
- 5.17 DIRECT managed-pipe retained identity runtime: PASS (previous evidence).
- 5.18 First-retain immutability: PARTIAL (previous evidence).
- 5.19 Snapshot consistency: PARTIAL overall; the new B stable-identity cross-plane snapshot passed.
- 5.20 Production enumeration/reconciliation API: draft only; NOT IMPLEMENTED.
- 5.21 Multi-Submitter ACK dispatcher foundation: PASS (previous evidence).
- 5.22 Worker close dispatcher foundation: PASS; now used in real shared-Worker runtime.
- 5.23 Real handle ownership tracking foundation: PASS; verified closed in this runtime.
- 5.24 Earlier dual-Adapter experiment: unsuccessful; superseded by this controlled success-A / ACK-failure-B experiment, not erased.
- 5.25 ACK/quarantine ordering trace: PASS (previous evidence).
- 5.26 Snapshot evidence readiness: PASS (previous evidence).
- 5.27 Callback-plane separation review: PASS; now implemented in the test harness and exercised.
- Stage 5 implementation/recovery acceptance: BLOCKED / NOT ACCEPTED.
- Phase 1: NOT ACCEPTED.
- Production: NO-GO.

## Safety and limits
No production behavior was changed. No process was started or terminated outside the build/test scripts. No uncertain operation was replayed. No retained envelope, Worker operation, HANDLE, checkpoint, trace, model state, configuration, or prior report was deleted. The experiment establishes a single shared Worker with independent Adapter/Submitter ownership and one retained terminal Adapter path; it does not establish multi-retained reconciliation or a production recovery API.
