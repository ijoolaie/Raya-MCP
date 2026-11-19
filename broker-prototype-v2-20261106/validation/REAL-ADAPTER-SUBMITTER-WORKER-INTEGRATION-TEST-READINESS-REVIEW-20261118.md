# Real Adapter–Submitter–Worker Integration Test Readiness Review — 2026-11-18

## Scope and execution boundary
Read-only inspection in Working Copy:
`D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`

This pass inspected the existing Adapter API tests, Adapter lifecycle implementation, Worker/Submitter interfaces, and the Orchestrator test/build composition. It did not modify C/C++ source, headers, tests, build scripts, configuration, or README. No build or test was run in this pass because the Working Copy volume D: had only 29,802,496 bytes free (about 28.4 MiB) at the time of inspection. C: had 13,045,043,200 bytes free. Existing build scripts emit intermediate objects and executable into the Working Copy on D:, so a new build was deferred rather than risk filling the volume. No files were deleted to create space.

## Findings

### 1. Existing Adapter tests already exercise real OS I/O, but not the Orchestrator lifecycle
`integrated-submit-adapter-v2-api-test.c` contains real named-pipe READ/WRITE tests and callback completion checks:
- synchronous/immediate READ and WRITE success/failure cases;
- pending READ whose Worker completion reaches the Adapter callback;
- post-OS-acceptance handoff failure retained/quarantine case;
- finalizer close failure retained case;
- ACK signaling failure after resource release;
- concurrent Adapter Destroy admission closure;
- Worker degraded/rejection cases.

These are useful lower-level integration tests. However, the test fixture `setup()` starts a real Worker and initializes Adapter (which starts Submitter), while tests call component APIs directly. They do not register the components in an Orchestrator Context or drive the new Orchestrator cleanup cursor end-to-end.

### 2. Existing shared cleanup helper does not model the new required order
The helper `stop_components()` currently performs:
1. Submitter Shutdown
2. Worker Shutdown
3. Submitter Join
4. Worker Join
5. Adapter retained-count check
6. Adapter Destroy
7. Submitter Destroy
8. Worker Destroy

That helper is suitable only for the older test fixtures where work is already completed and the test's assumptions hold. It is not evidence for the new Orchestrator lifecycle order. The strict Orchestrator sequence is:
1. Adapter Destroy (admission closes; must have zero entrants, active envelopes, and retained envelopes)
2. Submitter admission close/drain
3. Submitter Shutdown
4. Submitter Join
5. Submitter Destroy
6. Worker Shutdown (OWNED only)
7. Worker Join with normal exit
8. Worker Destroy

The existing helper should not be reused unchanged for a new Orchestrator lifecycle test.

### 3. Retained test cases are intentionally terminal holds, not cleanup fixtures
The handoff-failure test causes OS acceptance followed by Worker degradation before ownership handoff completes. It expects:
- Adapter retained count 1 and active count 1;
- Worker retained count 1 and reserved slot 1;
- pipe owner CALLER_UNCERTAIN;
- integrated reservation HELD;
- Submitter DEGRADED with one issuer lease;
- no completion callback;
- Adapter Destroy refusal.

The finalizer-close-failure test similarly expects a retained Adapter envelope and Worker retained operation, one reserved slot, zero outstanding/registry entries, Submitter queue/producers/active issue/issuer leases drained, and Adapter Destroy refusal. The test comments explicitly leave Worker, Adapter envelope, and uncertain OS handles retained. In its test-seam build, it joins the degraded Worker and inspects the test-only retained snapshot, then destroys only the Submitter. It does not recover or free the retained resources.

These tests must remain isolated terminal-hold tests. A new test must not try to clean up, unlink, replay, or free these retained operations. The existing retained snapshot is diagnostic/test-only and is not a production recovery API.

### 4. Adapter Destroy is a hard prerequisite for Submitter teardown
The implementation atomically closes Adapter admission, reports BUSY while entrants remain, then checks active and retained inventory under the Adapter lock. It destroys/zeros Adapter only when both counts are zero and the retained head/count agree. It does not stop Submitter or Worker.

Thus the new Orchestrator lifecycle test should first wait for the real completion callback and confirm:
- Adapter active count = 0;
- Adapter retained count = 0;
- callback count = exactly 1;
- Worker core outstanding/queued/reserved state is terminal/empty;
- no Worker retained operation;
- Submitter has no queued request, active producer, active issue, or issuer lease.

Then call Orchestrator Adapter Destroy, Submitter admission close, Submitter Shutdown/Join/Destroy, Worker Shutdown/Join/Destroy, and finally strict Context close. Do not use private struct-field writes to manufacture the component state.

### 5. Proposed next end-to-end test
A focused test should be added to the Orchestrator test target (which already compiles Adapter, Submitter, Worker, Finalizer, Registry, and Orchestrator sources):
- Create a unique named pipe with `FILE_FLAG_OVERLAPPED`, connect a client, and prepare a READ or WRITE with a deterministic payload.
- Start a real Worker; initialize Adapter through `raya_integrated_adapter_start_with_api` (which starts Submitter).
- Initialize/register an Orchestrator Context with Worker ownership OWNED.
- Begin close and drain the Context's API leases before lifecycle-owner access.
- Submit one real I/O through Adapter and wait for the real callback with a bounded deadline.
- Validate callback count, transferred byte count/data, Adapter active/retained inventory, Submitter snapshot, Worker outstanding/queued/reserved/registry/retained inventory.
- Execute Orchestrator cleanup APIs in strict order, checking each cursor transition and component structured result.
- Reclaim Context only after all eight steps report COMMITTED + SUCCESS.
- Close test-owned client/server pipe handles after component ownership has been released.

A second BORROWED variant should validate that steps 5–7 remain policy-skipped and no Worker lifecycle API is called by Orchestrator. Existing unit coverage already rejects an attempted Orchestrator Worker Shutdown for BORROWED; the full integration variant should additionally confirm that the external Worker remains usable/alive after Context reclamation, then is shut down by its external owner.

### 6. Retained integration should be a separate negative test
A separate scenario may deliberately inject a finalizer close failure or post-issue handoff uncertainty. Its only expected lifecycle result is fail-closed:
- Adapter Destroy BUSY/RECONCILE;
- Submitter/Worker teardown must not be misreported as healthy completion;
- Context strict close remains blocked;
- no replay or resource free is attempted.

This test must preserve the intentionally retained handles and buffers as the current retained tests do. It should run in isolation because the fixture intentionally leaves terminal-held resources until process exit. Avoid multiplying this leak-heavy test across repeated test loops.

## Disk and build decision
At inspection:
- D: 29,802,496 bytes free.
- C: 13,045,043,200 bytes free.

The Adapter API build script writes all object files and the test executable into the Working Copy root on D: and then deletes its named artifacts. Since D: has only about 28.4 MiB free, no build was attempted. Recheck D: before the next build. No temp artifacts were created by this read-only pass.

## Verified source hashes

| File | Bytes | SHA-256 |
|---|---:|---|
| `integrated-submit-adapter-v2-api-test.c` | 42,624 | `9F62641155FF413A69DEA3DDDCBD26DF1B062184B6ADE359BF9F42B05C1A142B` |
| `integrated-submit-adapter-v2.c` | 26,572 | `A9B39D0D4094E8418F820E53B3E28FEE64FC7B0A9EC417450F25EC422EE8D87F` |
| `integrated-submit-adapter-v2.h` | 5,821 | `DB52B1D6723DCA1159AD8737B83E64EFE26FF4D5F4053A32F9D1C8CD6C0AA1DC` |
| `raya-orchestrator-v1-test.c` | 39,897 | `500C4DC922E7579B0C7F58926D52D5EBAB3D4131E0637BD12125B48F5CE71831` |
| `raya-orchestrator-v1.c` | 27,655 | `8B5FB4BFCB890E76EBA55649B1BFA16B648B96C48DA2469671E07C248D220B24` |
| `raya-orchestrator-v1.h` | 5,915 | `240A4C58F9D1F5B59787B4441CF008FB6B931C0334F6DD377E9F6A9821562363` |
| `reaper-managed-worker-v2.c` | 27,593 | `5B0B43BA60303A0EC3EA07A9E26A641E32E40812702C0F87EC37C1ADD11DD8DD` |
| `reaper-managed-worker-v2.h` | 6,534 | `5D0230BC5295AF2DFE8B8BC4B64E0E7D1938F977A446F0C785CEA0C2E0918F96` |
| `build-integrated-submit-adapter-v2-api-test.cmd` | 1,216 | `A3EA896889A3C93813BB953775D90886C32A835DC402A22288474FA14AA603CE` |

## Result
- Read-only integration readiness review completed.
- No source/test/build/config/README changes.
- No build or tests run in this pass due to D: free-space constraint.
- No processes stopped; no install/download; no cleanup of retained state.
- Next implementation target is the normal-path real-I/O Orchestrator integration test described above, followed by an isolated fail-closed retained test.

Stage 5 remains in progress. Phase 1 remains NOT ACCEPTED. Production remains NO-GO.
