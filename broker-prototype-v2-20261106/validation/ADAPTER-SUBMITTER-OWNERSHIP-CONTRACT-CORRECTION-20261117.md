# Adapter / Submitter Ownership Contract Correction — 2026-11-17

## Decision
The public Adapter header previously described both Submitter and Worker as borrowed, already-started components. That statement is false for Submitter and contradicted by the checked-in implementation. The header comment is corrected to match actual behavior. This is a documentation-contract correction only; no lifecycle behavior or ownership transfer was changed.

## Direct source evidence
- raya_integrated_adapter_start_with_apis rejects an already-initialized Submitter (s->initialized must be false).
- It stores the caller-supplied Submitter pointer and calls raya_submitter_start_with_api itself.
- Worker must already be initialized and started; Adapter stores its pointer and uses it for admission/reservation/handoff. Adapter does not start Worker.
- raya_integrated_adapter_destroy closes Adapter admission, checks entrants and retained/active work, deletes Adapter's critical section, and zeroes Adapter storage. It does not call Submitter Shutdown/Join/Destroy or Worker Shutdown/Join/Destroy.

## Corrected contract
- Caller supplies storage for an uninitialized Submitter; Adapter initializes and starts it.
- Caller supplies an already-started Worker; Adapter borrows Worker and requires it to outlive Adapter.
- Adapter Destroy only destroys Adapter state. It does not stop or destroy Submitter or Worker.
- After Adapter admission is closed and Adapter Destroy succeeds, the external lifecycle owner must quiesce direct Submitter users and perform Submitter Shutdown, Join, and Destroy. The Submitter storage must remain alive through that sequence.
- Worker teardown remains an external policy decision: Owned Worker can be stopped/joined/destroyed only after the Adapter/Submitter chain is terminal; Borrowed Worker receives no teardown calls from this owner.

## Working Copy change
Only integrated-submit-adapter-v2.h was edited. Its misleading one-line borrowed-both-components comment was replaced with the explicit contract above. No C implementation, public struct, API signature, test source, build script, or Production/Source Project file was changed.

SHA-256:
- integrated-submit-adapter-v2.h: 612F99F6693FFCDE007ABDF922A1C6FF0762789AF25229037F6B5E1493CC0EDA

## Verification
- Existing build-integrated-submit-adapter-v2-api-test.cmd completed successfully with MSVC /W4 /WX.
- 724 assertions across 66 cases per run; 10 consecutive runs; 7,240 assertions passed.
- Build script removed its executable and object outputs; no broad artifact cleanup was performed.
- Post-run process inventory: no cl.exe, link.exe, java.exe, or tlc.exe.
- Post-run free space: C: 12,941,590,528 bytes; D: 2,110,156,800 bytes.

## Orchestrator integration gate
There is no checked-in root-level Orchestrator C/header implementation in this Working Copy. Existing Orchestrator evidence is from disposable harnesses and design/audit reports, not a durable component implementation. The most recent actual-API Owned/Borrowed harness reported 30 Owned + 30 Borrowed iterations and 540 checks, but explicitly did not cover Submitter failure injection or public Orchestrator pointer-lifetime races. A later three-component chained failure attempt was inconclusive and must not be represented as a pass.

The structured Submitter ex results are now available and its partial Destroy retry has passed component-level failure tests. However, before a real Orchestrator can consume them safely, remaining gates include:
1. a durable cleanup owner and cursor that serialize all teardown calls;
2. a stable external handle/reference contract that protects storage before dereferencing it (an embedded entrant count alone is insufficient for stale raw pointers);
3. explicit mapping of Submitter ex effect/outcome/retry into cursor substates;
4. Worker structured Join result and deterministic Worker Destroy handle-close seam;
5. explicit policy for abnormal Submitter/Worker exit and DEGRADED shutdown;
6. startup rollback partial cleanup and Owned/Borrowed failure matrix;
7. actual three-component chained tests in a disposable harness with reliable instrumentation.

No fake Orchestrator source was introduced to imply these contracts already exist. No full chained failure test was run because the durable owner/registry/cursor and Worker failure seam are not yet implemented. No Production changes, Source Project changes, commits, resets, installs, downloads, or process termination occurred.
