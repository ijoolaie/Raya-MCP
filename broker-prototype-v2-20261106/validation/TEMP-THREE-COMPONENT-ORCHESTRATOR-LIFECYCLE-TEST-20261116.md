# TEMPORARY THREE-COMPONENT ORCHESTRATOR LIFECYCLE TEST — 2026-11-16

## Result
A disposable copy of the actual Worker, Submitter and Adapter implementation was built and exercised with a small orchestration wrapper. **PASS: 11 assertions per run × 10 runs = 110 assertions.** Both Borrowed and Owned Worker normal startup/teardown paths passed.

## Tested lifecycle paths
### Borrowed Worker
1. Start a real Managed Worker outside the orchestrator.
2. Start the real Adapter, which starts its Submitter while borrowing the already-started Worker.
3. Orchestrator teardown destroys Adapter first, then shuts down/joins/destroys Submitter.
4. Verify Worker remains initialized, started, and not joined after orchestrator teardown.
5. Explicitly stop/join/destroy the Worker from its external owner.

### Owned Worker
1. Orchestrator starts a real Managed Worker.
2. Start the real Adapter and its Submitter.
3. Teardown order is Adapter → Submitter shutdown/join/destroy → Worker shutdown/join/destroy.
4. Verify all three components are fully uninitialized after teardown.

## Orchestrator gate behavior in the temporary harness
- Ordinary entry increments a low-bit entrant count and rejects once the closing bit is set.
- Teardown acquires a separate cleanup-owner token, sets closing, waits for admitted entries to drain, then performs ordered cleanup.
- If teardown fails, it releases only the cleanup-owner token and leaves closing set. A later teardown attempt can reacquire the cleanup-owner token and resume; ordinary new API entry remains closed.
- This is a test wrapper, not a project API or complete proof of late-caller/object-storage lifetime.

## Build and execution
- Visual Studio 2026 Build Tools 18.7.3 x64, `/W4 /WX /TC`.
- Linked the actual copied component sources: Submitter, Reaper core/registry/integrated/wait modules, pipe owner/finalizer, Managed Worker and Integrated Adapter.
- 10 consecutive runs; each printed `PASS 11 assertions: Borrowed and Owned actual lifecycle`.

## What remains untested
- No injected teardown failure was exercised through the complete Orchestrator, so retry-after-failure is a code-path design in the harness, not a demonstrated end-to-end result.
- No Adapter startup failure was injected within this orchestrator test; previous separate tests cover Submitter partial startup cleanup and Worker failed-start cleanup, but not their combined orchestration.
- No simultaneous API caller vs orchestrator teardown race was run. The wrapper's entry gate was exercised only by normal sequential lifecycle calls.
- No concurrent late caller after successful object destruction; storage lifetime remains the external owner's responsibility.
- No complete Owned Worker thread-creation failure → failed-start cleanup → restart sequence in this run.
- No Production source or public Orchestrator API was changed.

## Cleanup and integrity
- Temporary directory removed: 145 files, 866,274 bytes.
- No matching temp directory remained; no active compiler/linker/test process remained.
- C free space: 13,501,218,816 bytes (~12.58 GiB); D: 2,111,631,360 bytes (~1.97 GiB).
- Working Copy hashes unchanged:
  - Submitter C `D8ABAFD8D3B731DA141E8B1792534A00EB888055D0F15EB0EDFCCE2D01843941`
  - Submitter H `2D4CBB91D3400AC5416FC1DE25D03012AC2D2644D6F848A42D278F38D5191655`
  - Worker C `FDCD768B31CD6FF30F435A5588D6E1000394A7B355A2E37C1A8117D6C78225E4`
  - Worker H `F45DB7509C2045A37925A86BE06F4871109BA5348A383B75447ACD3C46423818`
  - Adapter C `6E2427C09974EE47B382F80CD11B21FEAAC16D12ECABC6A6155A11E9E8F62FBE`
  - Adapter H `4E2CC45DCD9E4EA0EA47441158B40F25CCD900095182CD256A90C285FB8D4882`
- No Production/source changes, commits or resets.

## Decision
**Borrowed and Owned normal lifecycle ordering: PASS in a temporary integration harness.** Full Orchestrator acceptance remains OPEN: failure/retry, startup rollback, concurrent entrant drain and full restart still need an end-to-end test. Stage 5 remains INCOMPLETE; Phase 1 NOT ACCEPTED; Production NO-GO.
