# Stage 5.16 — Multi-Submitter ACK Dispatcher Foundation — 2026-11-18

## Scope completed

Updated only `integrated-submit-adapter-v2-api-test.c`.

- Replaced the single global ACK-fault state pointer with a bounded registry of four independent `api_state` entries.
- Added explicit registration and unregistration helpers.
- ACK fault dispatch now scans registered states and matches the exact target Submitter event handle (`wake_event` or `changed_event`) before injecting a fault.
- A fault for one Submitter does not consume or alter another Submitter's pending fault.
- Added `test_multi_submitter_ack_dispatcher`, which creates two independent Submitter state objects and two distinct event handles, registers both, injects the first fault, proves the second remains armed, then injects the second fault and unregisters both.
- Existing single-Submitter ACK tests now use the same registry path.

## Validation

- Ran `build-integrated-submit-adapter-v2-api-test.cmd`.
- Build succeeded using Visual Studio 2026 BuildTools 18.7.3.
- 10 consecutive runs passed.
- Each run: 821 assertions across 66 cases.
- Total: 8,210 assertion executions.
- All declared API-test EXE/OBJ outputs were absent after script cleanup.
- Disk after run: C free 13,229,998,080 bytes; D free 4,810,457,088 bytes.
- No manual artifact deletion was needed.

## Files changed

- `integrated-submit-adapter-v2-api-test.c` only.

No production source/header, build script, README, TLA+ model, configuration, or Production code was changed.

## Exact remaining Stage 5.16 work

This is dispatcher foundation only. It does **not** yet prove two real Adapter-associated operations retained through two independent Adapter/Submitter instances sharing one Worker.

Next:
1. Add a stable Worker-level close-callback dispatcher context that routes each operation's handle-close events to the correct Adapter test state.
2. Start two independent Adapter/Submitter pairs over one Worker.
3. Submit one real operation through each Adapter.
4. Cause both independent operations to reach the real ACK-failure retained path without degrading either Submitter before its own operation is issued.
5. Verify both Adapter snapshots and the shared Worker snapshot using stable identity pairs, without list-order assumptions.
6. Verify serialized teardown; preserve quarantined resources if strict teardown preconditions remain unsatisfied.

## Acceptance

- Multi-Submitter ACK dispatcher unit coverage: PASS.
- Two-Adapter shared-Worker runtime test: NOT RUN / NOT COMPLETE.
- Stage 5.16: PARTIAL.
- Stage 5.20 production enumeration/reconciliation APIs: NOT IMPLEMENTED.
- Stage 5 implementation/recovery acceptance: BLOCKED / NOT ACCEPTED.
- Phase 1: NOT ACCEPTED.
- Production: NO-GO.
