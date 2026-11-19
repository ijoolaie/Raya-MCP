# Orchestrator Real-I/O and BORROWED/OWNED Lifecycle Integration Tests — 2026-11-18

## Scope

Working Copy only:
`D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`

Added two integration tests to `raya-orchestrator-v1-test.c`. No production implementation, public header, README, configuration, or build script was changed.

## Test A — real READ/WRITE with OWNED Worker

`test_real_io_orchestrator_owned_lifecycle` uses the production Win32 I/O path rather than a mocked I/O seam:

1. Creates a process-unique named pipe with `FILE_FLAG_OVERLAPPED` and connects a client.
2. Starts a real Managed Worker.
3. Starts the Integrated Adapter, which initializes and starts the Submitter.
4. Registers the live Adapter/Submitter/Worker in a strict OWNED Orchestrator Context.
5. Acquires/releases a Context lease, enters BeginClose, and verifies lease drain.
6. Submits an actual asynchronous named-pipe READ through Adapter/Submitter; the client writes the payload after submission.
7. Waits up to 5 seconds for the Worker-driven READ callback and checks exactly one callback, `ERROR_SUCCESS`, byte count, and exact payload.
8. Waits for the READ envelope to drain, then submits an actual Adapter WRITE; the client reads and verifies the exact payload, and the test verifies the WRITE callback's success and byte count.
9. Waits boundedly for Adapter active/retained state and Worker caller/outstanding/queued/list/reservation/registry/retained state to become empty; checks the Submitter snapshot has no queued requests, producers, active issue, or issuer leases.
10. Executes the strict cleanup sequence through Orchestrator APIs: Adapter Destroy → Submitter admission close → Submitter Shutdown → Submitter Join → Submitter Destroy → Worker Shutdown → Worker Join → Worker Destroy.
11. Verifies all eight required cursor steps are COMMITTED+SUCCESS and reclaims the strict Context.

## Test B — BORROWED Worker lifecycle boundary

`test_borrowed_worker_lifecycle_boundary` starts a real Worker, Adapter, and Submitter and registers them under the BORROWED policy. It executes the five common Adapter/Submitter cleanup steps through Orchestrator and reclaims the Context without calling any Orchestrator Worker lifecycle API. After Context reclamation, it verifies the external Worker thread is still alive and the Worker remains ACCEPTING, successfully performs a Worker admission/finish pair, then has the external lifecycle owner perform Shutdown, normal Join, and Destroy.

This verifies the ownership boundary and external teardown path. It is a lifecycle integration test, not an additional pipe-I/O test; real READ and WRITE are covered by Test A.

If an intermediate step fails while a live thread could still reference stack-owned fixture state, the test terminates the test process rather than continuing with unsafe teardown.

## Build and test evidence

- Compiler: Visual Studio 2026 Build Tools, x64 environment.
- Flags: `/nologo /W4 /WX /TC`.
- All Orchestrator test dependencies compiled and linked successfully.
- Executable: temporary output under `C:\RayaMCP-Validation\orch-real-io-20261118`.
- Executed 4 times (one initial run plus 3 repeat runs).
- Every run: `SUMMARY: 216 passed; 0 failed`.
- Aggregate: 864 passed assertions across four successful process runs; 0 failed.
- No timeout or test process failure observed.

This is evidence for one normal-path real asynchronous READ and WRITE and both OWNED/BORROWED lifecycle policies. It does not establish retained-state recovery, durable recovery, or Production readiness. The intentionally retained negative scenarios remain separate and untouched.

## Changed file

| File | Bytes | SHA-256 |
|---|---:|---|
| `raya-orchestrator-v1-test.c` | 55,221 | `3748264F1D6FE56894C13C7852606581A44A3AF54D82E6184093DE9C633125D8` |

No other Working Copy source, header, test, build, configuration, or README file was intentionally modified.

## Temporary artifacts and disk

The isolated C: output directory contained exactly 17 compiler outputs: 16 `.obj` files and one test `.exe`. These are temporary build artifacts and are removed after recording the results. No D: build outputs were created.

Before the first build in this pass, free space was C: 10.18 GB and D: 1.96 GB. After the test builds/runs, free space was approximately C: 10.20 GB and D: 1.96 GB.

## Limitations and next validation

- Both normal-path READ and WRITE are covered, but only with one sequential operation pair and no concurrency/fault injection in this Orchestrator-level test.
- BORROWED integration validates ownership and external Worker usability/teardown but does not send pipe I/O.
- The retained/uncertain negative integration scenario remains unimplemented and must remain isolated; no retained resource was replayed or freed.
- No full Adapter→Submitter→Worker recovery/reconciliation API is implied by these tests.
- Stage 5 remains in progress. Phase 1 remains NOT ACCEPTED. Production remains NO-GO.
