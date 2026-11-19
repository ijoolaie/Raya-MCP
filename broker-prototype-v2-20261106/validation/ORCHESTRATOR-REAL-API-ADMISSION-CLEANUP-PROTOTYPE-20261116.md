# Real API Orchestrator Admission and Cleanup Serialization Prototype — 2026-11-16

## Result
PASS — disposable x64 Windows C prototype called the actual Adapter, Submitter, and Worker public lifecycle APIs. This is not a checked-in Orchestrator and not Phase 1 acceptance.

## Harness and execution
- Built the copied component sources plus a temporary Orchestrator harness with Visual Studio 18 Build Tools x64 MSVC, `/W4 /WX /TC /O2`.
- 10 independent lifecycle iterations; 130 explicit CHECK assertions passed (13 per iteration).
- Started a real Managed Worker, then used `raya_integrated_adapter_start`, which initializes its borrowed Submitter internally.
- Cleanup was serialized by one temporary Orchestrator-wide atomic owner token. It executed actual `raya_integrated_adapter_destroy`, `raya_submitter_shutdown` + `raya_submitter_join` + `raya_submitter_destroy`, then `raya_managed_worker_shutdown` + `raya_managed_worker_join` + `raya_managed_worker_destroy`.

## Concurrency and caller-admission checks
- One caller acquired the temporary Orchestrator admission lease and was held at a deterministic event barrier.
- Cleanup acquired the global owner and closed admission, then waited for the active lease to drain before invoking component destruction.
- A late entrant after admission closure was rejected.
- A second concurrent cleanup caller was rejected as BUSY while the first held the owner token.
- After the active caller left, the owner completed the real three-component cleanup; cursor reached terminal stage 3 and owner token was released.

## Scope limits
- This exercises the successful Owned-Worker path only; Borrowed-Worker policy was covered separately by the earlier standalone 3,200-check policy harness, not in this real-API harness.
- No component close-failure injection or partial-destroy retry was included here. The component cleanup retry prototypes remain separate evidence and are not represented as this test.
- The admission lease protects callers that enter while Orchestrator storage is already valid. An embedded gate cannot make a dangling raw Orchestrator pointer safe: the owner must externally prevent any new pointer dereference and retain Orchestrator/component storage until all possible callers are quiesced. This test did not free storage while arbitrary late callers might still hold raw pointers.
- The harness is temporary and not a production implementation. Component APIs were not modified to make direct concurrent Destroy calls safe; the wrapper serialized them.

## Build and cleanup
- Build and run completed successfully; output: `PASS iterations=10 checks=130 actual_component_apis=adapter_submitter_worker`.
- Temporary directory contained $($items.Count) files and $bytes bytes before cleanup; all temporary source copies, object files, executable, and harness were removed after inspection.
- No source/header/test/build/model/archive/Production files in the Working Copy were modified. No Commit or Reset; no process was terminated.

## Additional API contract discrepancy found during integration
- The public Adapter header comment says Adapter borrows both already-started Submitter and Worker components.
- The implementation of `raya_integrated_adapter_start_with_apis` rejects an already-initialized Submitter (`s->initialized` must be false) and then calls `raya_submitter_start_with_api` itself. The real Worker, in contrast, must already be initialized and started.
- Therefore the Submitter startup/ownership contract is inconsistent between header documentation and implementation. This prototype followed implementation behavior: start Worker first; Adapter start initializes Submitter. No source correction was made. Resolve this contract before defining checked-in Orchestrator ownership and rollback semantics.
