Worker Structured Lifecycle Result API — 2026-11-17

Scope: Working Copy only (D:\Work\project\Raya-MCP\broker-prototype-v2-20261106).

Changed files:
1. reaper-managed-worker-v2.h — added operation status/effect/retry enums, raya_worker_op_result, and shutdown_ex/join_ex/destroy_ex APIs. Legacy Boolean APIs remain compatibility wrappers.
2. reaper-managed-worker-v2.c — added structured result helpers. Shutdown differentiates already stopped, successful signaling, and degraded/failed states requiring reconciliation. Join distinguishes retryable wait/query/thread-handle-close failures from abnormal exit after thread HANDLE reaping; repeated Join reports the committed effect without closing twice. Destroy reports retryable wake-event close failure and committed destruction.
3. reaper-managed-worker-v2-lifecycle-seam-test.c — expanded assertions for structured status, effect, retry class, step, native error, exit code, and resource presence.

Final verification after the last source edit:
- Lifecycle seam: 10 consecutive runs × 29 assertions = 290 passed.
- Existing native Worker regression: 10 consecutive runs × 35 assertions = 350 passed.
- Production-path Worker compile without RAYA_ADAPTER_TESTING: PASS using existing /W4 /WX script.
- Integrated Adapter API regression: 10 consecutive runs × 724 assertions = 7,240 passed across 66 cases per run.
- Total assertions in final test runs: 7,880 passed, zero reported failures.
- Build scripts removed generated EXE/OBJ outputs; final artifact check found no relevant test EXE/OBJ files.
- Final process inventory: no cl.exe, link.exe, java.exe, or tlc.exe.
- Final disk free: C 12,331,597,824 bytes; D 2,108,190,720 bytes.

SHA-256:
- reaper-managed-worker-v2.h — 5D0230BC5295AF2DFE8B8BC4B64E0E7D1938F977A446F0C785CEA0C2E0918F96 (6,534 bytes)
- reaper-managed-worker-v2.c — 5B0B43BA60303A0EC3EA07A9E26A641E32E40812702C0F87EC37C1ADD11DD8DD (27,593 bytes)
- reaper-managed-worker-v2-lifecycle-seam-test.c — 1A4A21E39D11CCDB65C809E1D149DB4FF9D9BFF81CB4497E30EB2327666778EF (4,479 bytes)

Limitations / next gates:
- No Worker/Adapter/Submitter wiring to stable-handle registry or public Orchestrator facade yet.
- Caller must serialize Worker Join/Destroy and quiesce callers before destruction; _ex does not add concurrent lifecycle-call safety.
- Existing Worker lifecycle seam covers wait, exit-code, thread-handle close and wake-event close. No dedicated injected Shutdown wake-signal failure test was added in this turn.
- Worker shutdown degradation remains reconciliation-required; no generic recovery from DEGRADED was invented.
- Startup rollback and full chained Owned/Borrowed three-component failure matrix remain open.
- No full Orchestrator, Phase 1 acceptance, full Raft, or Production readiness claim.

Scope protection:
- No commit/reset; README untouched.
- Source project and Production not changed.
- IranPriceScout and its independent port-8765 tunnel not touched.
- No processes terminated.
