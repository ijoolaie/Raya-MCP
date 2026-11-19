# Orchestrator Submitter Lifecycle Cursor Integration
Date: 2026-11-17
Scope: Working Copy only — `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`

## Implemented

Added owner-only Orchestrator lifecycle steps after Adapter Destroy and Submitter operational admission close:

| Cursor step | Index | Component call | Completion semantics |
|---|---:|---|---|
| Adapter Destroy | 0 | `raya_integrated_adapter_destroy_ex` | committed only on Destroy success |
| Submitter admission close | 1 | `raya_submitter_admission_close` | committed only after entrant count reaches zero |
| Submitter Shutdown | 2 | `raya_submitter_shutdown_ex` | committed on SUCCESS/ALREADY_DONE; terminal/partial signaling failure becomes BLOCKED+UNCERTAIN |
| Submitter Join | 3 | `raya_submitter_join_ex` | retryable wait/query/CloseHandle failures are NOT_ATTEMPTED+RETRYABLE_FAILURE; successful handle reap is COMMITTED+SUCCESS; abnormal exit after handle reap is COMMITTED+ABNORMAL_EXIT |
| Submitter Destroy | 4 | `raya_submitter_destroy_ex` | normal completion is COMMITTED+SUCCESS; known partial close is PARTIAL+RETRYABLE_FAILURE; invalid/uncertain state becomes BLOCKED+UNCERTAIN |

All lifecycle facade operations require exact cleanup-owner Context retrieval after Registry admission is closed and leases are drained. Callers must serialize cleanup-owner operations. No component lifecycle operation is performed while holding the Registry lock.

## Registry cursor extension

Added `RAYA_ORCH_EFFECT_PARTIAL`:
- represents a known component-tracked partial effect;
- a new IN_PROGRESS attempt is permitted only when the previous outcome is RETRYABLE_FAILURE;
- the component must itself preserve exact substep progress and support safe same-operation retry;
- PARTIAL blocks cleanup-owner release until resolved;
- explicit reconciliation may resolve PARTIAL only from fresh evidence.

Submitter Destroy already tracks `destroy_progress`: if wake Event close succeeds but changed Event close fails, the component preserves the first close and retries only the remaining close on the next call. The Orchestrator cursor now faithfully records that partial effect rather than misclassifying it as no-effect.

## Failure semantics verified

1. Join injected WAIT_FAILED:
   - thread handle remains present;
   - result is RETRYABLE_FAILURE + EFFECT_NONE;
   - cursor records NOT_ATTEMPTED + RETRYABLE_FAILURE and Win32 error;
   - second Join succeeds and cursor commits on attempt 2.
2. Destroy injected second Event CloseHandle failure:
   - wake Event is already closed; changed Event remains;
   - result is RETRYABLE_FAILURE + PARTIAL_DESTROY;
   - cursor records PARTIAL + RETRYABLE_FAILURE;
   - second Destroy resumes from component progress and commits on attempt 2.
3. Abnormal Submitter thread exit:
   - Shutdown returns terminal failure and cursor becomes BLOCKED+UNCERTAIN;
   - test explicitly reconciles Shutdown only after observing the thread is signalled and component state is DEGRADED;
   - Join successfully reaps the thread handle but reports TERMINAL_FAILURE; cursor records COMMITTED+ABNORMAL_EXIT and preserves exit code;
   - Destroy is refused because component lifecycle is DEGRADED, even though the thread handle is reaped and counters are zero. Cursor becomes BLOCKED+UNCERTAIN. Cleanup owner cannot be released.
   - Test harness manually closes its stack-fixture Event handles after asserting the production Destroy refusal. This manual cleanup is test-only and is not a supported production recovery path.

A broader Destroy relaxation was briefly tested and rejected by the existing Submitter failure matrix (it would have allowed Destroy from a degraded lifecycle). That change was reverted. The component's existing STOPPED/DEGRADED preconditions remain intact.

## Test results

All builds used MSVC x64 with `/W4 /WX`. Final successful executions:

| Suite | Repetition | Assertions/checks |
|---|---:|---:|
| Orchestrator lifecycle integration | 10 × 103 | 1,030 |
| Registry regression | 1 × 50 | 50 |
| Submitter Win32 failure matrix | 25 × 223 | 5,575 |
| Submitter admission gate | 10 × 15 | 150 |
| Adapter API seam | 10 × 751 | 7,510 |
| **Total** | | **14,315 passed; 0 failed** |

An intermediate run of the old Submitter failure matrix correctly caught the over-broad DEGRADED Destroy relaxation. The relaxation was reverted, the Orchestrator abnormal-exit test was changed to assert refusal, and the final full set above passed.

## Changed files and SHA-256

1. `raya-orchestrator-registry-v1.h`
   `032574EF501ED535F600CC0E9F932C42F1D0BB43907BB7851ABE5DF8FAED5CAE`
2. `raya-orchestrator-registry-v1.c`
   `7805F1780BF44E8A40A3A2F0B9E26E22A304549A546FCAAEDDEEFB398073185D`
3. `raya-orchestrator-v1.h`
   `A73F9961F7C02EE3F64488DC7920BDD092BA4F5763FF05A74A2B4FBF6B342EC7`
4. `raya-orchestrator-v1.c`
   `80073CBFFDCBBF96B01879562506616F0ED2E969081D0F50C32BE56D8E534F25`
5. `raya-orchestrator-v1-test.c`
   `DB493C621EFB0C331A30341076150EFF5943A6CB2A58C8C9FE0CB16220AAD06C`

No Submitter component source change remains from this turn; the attempted broad Destroy relaxation was fully reverted.

## Final environment inspection

- All test-owned EXE/OBJ outputs checked by the build scripts: none remaining.
- Active cl/link/Java/TLC or target test process: none.
- Final free space: C: 16,548,261,888 bytes; D: 2,111,606,784 bytes.
- No process was terminated.
- No install/download, Git commit/reset, Production change, or TLA+ rerun occurred.

## Remaining limitation / next work

The normal Submitter path is now connected end-to-end through admission close → Shutdown → Join → Destroy. Join abnormal exit is recorded correctly, but a DEGRADED Submitter cannot be destroyed by the current component contract, even after its thread handle is reaped. This remains a hard recovery limitation, not a success case.

Next:
1. Design an explicit, evidence-based Submitter degraded-state recovery/terminalization API (do not weaken existing Destroy preconditions).
2. Define which degraded causes can be safely terminalized and which must remain permanently blocked.
3. Add fault-injection coverage for each cause, including retained/uncertain request ownership.
4. Only then consider a controlled recovery-to-Destroy cursor transition.
5. Keep Worker lifecycle integration separate until Submitter recovery is proven.
