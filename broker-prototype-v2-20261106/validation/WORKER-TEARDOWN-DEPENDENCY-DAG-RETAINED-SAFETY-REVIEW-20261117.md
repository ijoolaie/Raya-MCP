# Worker Teardown Dependency DAG and Retained-Work Safety Review — 2026-11-17

Status: source-backed lifecycle review; no code change, no teardown execution.
Scope: Working Copy only — `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`.
Production: NO-GO. Phase 1: NOT ACCEPTED.

## Executive conclusion

The Worker already exposes structured `shutdown_ex`, `join_ex`, and `destroy_ex` APIs, but the Orchestrator does not call them and has no Worker cleanup cursor steps. Context's `OWNED/BORROWED` flag is descriptive teardown policy metadata only.

A safe normal-path teardown order can be derived from current component contracts:

1. Close Adapter admission and drain all Adapter entrants/active operations; require Adapter retained count = 0; only then commit Adapter Destroy.
2. Close Submitter operational admission and drain direct Submitter API entrants.
3. Submitter Shutdown → Join → Destroy.
4. Worker Shutdown → Join → Destroy, only after Adapter has no active/retained envelopes and Submitter is destroyed, and only if Worker lifecycle remains healthy and its internal outstanding/queued/reserved/registry counts are zero.
5. Only after all required component teardown succeeds may Context storage be invalidated/freed and Registry close be completed/reclaimed. Registry storage itself must outlive all handles/leases/cleanup activity.

This is a candidate normal-path order, not a proven universal order for every degraded/retained state. In particular, do not shut down or destroy a Worker whose thread exited DEGRADED or whose operations remain held/quarantined.

## Source-backed behavior

### Worker shutdown

`raya_managed_worker_shutdown_ex` calls `raya_reaper_integrated_shutdown` only from ACCEPTING; STOPPING/STOPPED is idempotently accepted. It signals the wake event when STOPPING. DEGRADED/FAILED is reported as TERMINAL_FAILURE with RECONCILE; it is not converted to STOPPED.

The Worker thread:
- refuses to continue when lifecycle is DEGRADED;
- marks STOPPED only when `raya_reaper_integrated_can_exit` returns true;
- on exact terminal I/O, closes both owned handles through the Finalizer, commits Finalizer release, releases Pipe Owner, releases integrated registry ownership, then invokes the operation release callback outside the Worker lock;
- on uncertain completion or finalizer/ownership failure, records degradation/terminal hold and exits rather than claiming successful completion.

### Worker join and destroy

`join_ex` waits for the thread, reads its exit code, closes its thread handle, then reports SUCCESS only for ERROR_SUCCESS. An abnormal exit is TERMINAL_FAILURE even though the thread handle has been reaped.

`destroy_ex` requires:
- thread already joined;
- Worker lifecycle exactly STOPPED;
- caller_active, outstanding, queued all zero;
- core head/tail NULL;
- reserved_slots zero;
- integrated registry count zero;
- Worker retained_count zero and retained_head NULL.

Only then does it close the wake event and delete the Critical Section. It cannot be used to erase a DEGRADED Worker.

### Adapter and Submitter ordering

Adapter Destroy closes Adapter admission and returns BUSY/RECONCILE while entrants, active envelopes or retained envelopes remain. It does not shut down or destroy Submitter or Worker.

Adapter borrows Worker; Adapter initializes Submitter but does not own its storage/lifecycle. Adapter work can call Worker admission/reservation/handoff and later Worker finish. Therefore Worker shutdown must not race Adapter submissions or outstanding handoffs.

Submitter direct operational callers must also be quiesced. Adapter Destroy alone does not close Submitter's separate operational admission gate. The Orchestrator's existing sequence—Adapter Destroy, Submitter admission close, Submitter Shutdown/Join/Destroy—covers that Submitter boundary, but omits Worker teardown.

### Retained work

There are two distinct retained inventories:
- Adapter envelope retained list/count;
- Worker operation retained list/count fields.

The source scan found no production writes/link insertion/removal for Worker `retained_head`/`retained_count` in `reaper-managed-worker-v2.c`; these fields are nevertheless checked by lifecycle test seams and Destroy safety validation. Do not treat them as a working public recovery queue.

Adapter retained entries are real and can arise from pre-issue cleanup failures, ownership invariant failures, uncertain post-issue handoff, or transferred-degraded quarantine. Adapter exposes a retained count, but the production API does not expose a public enumerate/reconcile/release operation. Adapter Destroy refuses while retained entries exist.

For transferred-degraded handoff, Worker ownership has transferred but Worker lifecycle is marked degraded and Adapter publishes quarantine. Worker main exits on DEGRADED; its Join may reap an abnormal thread, but Destroy rejects because lifecycle is not STOPPED. This is intentional fail-closed behavior, not a path to force cleanup.

## Candidate dependency DAG

```text
External callers stop creating new work
                |
                v
Adapter admission close + entrants drain
                |
                v
Adapter active work = 0 AND retained = 0
                |
                v
Adapter Destroy committed
                |
                v
Submitter admission close + direct entrants drain
                |
                v
Submitter Shutdown -> Join -> Destroy
                |
                v
Worker has no caller admissions / outstanding / queued /
reserved slots / registry entries; no retained/quarantined work
                |
                v
Worker Shutdown -> Join (normal exit) -> Destroy
                |
                v
Context no longer used -> Registry close/reclaim
                |
                v
Caller frees Context and Registry only under their lifetime contract
```

Important: this DAG is a normal-path candidate. A failure at any gate stops progression. Do not bypass a blocked gate by advancing to the next node.

## Degraded and exceptional paths

| Observed state | Safe conclusion | Forbidden shortcut |
|---|---|---|
| Adapter retained_count > 0 | Adapter teardown blocked; operation-level evidence/reconciliation needed | Force Adapter Destroy or free envelopes |
| Worker lifecycle DEGRADED | Worker thread may exit abnormally; outstanding resources may remain held | Treat Join as successful shutdown or call Destroy to erase state |
| Worker Join abnormal exit | Thread handle is reaped, not proof of healthy Worker stop | Record Worker teardown success |
| Worker Destroy INVALID_STATE | Internal state not safely empty/STOPPED | Close wake event or delete lock externally |
| Submitter direct callers not quiesced | Submitter storage may still be accessed | Destroy Submitter or free Context |
| Registry leases remain | Context may still be in use | Free Context or Registry |
| Worker ownership BORROWED | Orchestrator must not destroy Worker | Infer ownership from pointer presence |
| Worker ownership OWNED | Intended policy only; no Orchestrator Worker step exists | Claim automatic Worker teardown is implemented |

## Coverage gap

The existing Worker lifecycle seam test covers Worker shutdown, retryable Join wait/query/CloseHandle failures, abnormal exit after handle reaping, and retryable Destroy wake-event close. It does not demonstrate the complete Orchestrator → Adapter → Submitter → Worker → Context/Registry teardown chain.

The current Orchestrator cleanup step constants stop at Submitter Destroy (steps 0–4). There are no Worker Shutdown/Join/Destroy owner APIs in the reviewed Orchestrator header/source. No integrated test proves:
- Worker-owned teardown is performed only for OWNED;
- BORROWED Worker is left untouched;
- Worker cleanup cursor records retryable vs abnormal exit vs degraded/quarantined outcomes;
- Adapter retained inventory prevents progression;
- Context/Registry reclamation occurs only after all component cleanup steps commit.

## Required implementation decisions before coding

1. Decide whether Worker teardown belongs in Orchestrator or a higher-level external lifecycle owner. Do not have both.
2. Define the exact precondition for Worker Shutdown, including zero Adapter active/retained work, zero Submitter admissions and no outstanding Worker reservations.
3. Define how Adapter retained entries are inspected and reconciled. Until then, any nonzero retained count is a hard stop.
4. Add owner-only Worker Shutdown/Join/Destroy cursor operations only after the above preconditions and abnormal-exit policy are specified.
5. Distinguish thread-handle reaped from Worker cleanly STOPPED; a DEGRADED exit must remain blocked/quarantined.
6. Define Registry completion and caller-owned Context/Registry freeing as explicit final steps, with lifetime checks.
7. Add an integration regression test using real component objects and injected failures across each boundary before claiming this DAG validated.

## Verification and scope

- Inspected Worker implementation/header, Orchestrator implementation/header/Registry header, Adapter implementation, Pipe Owner and Finalizer contracts, and Worker lifecycle seam tests.
- Reviewed exact Worker lifecycle and Adapter retained-list source paths.
- No build or tests run.
- No source/header/test/config/README changes.
- No process terminated, install/download performed, or temporary artifact created/deleted.
- No Production changes.

## Source fingerprints

| File | SHA-256 |
|---|---|
| reaper-managed-worker-v2.h | 5D0230BC5295AF2DFE8B8BC4B64E0E7D1938F977A446F0C785CEA0C2E0918F96 |
| reaper-managed-worker-v2.c | 5B0B43BA60303A0EC3EA07A9E26A641E32E40812702C0F87EC37C1ADD11DD8DD |
| raya-orchestrator-v1.h | A73F9961F7C02EE3F64488DC7920BDD092BA4F5763FF05A74A2B4FBF6B342EC7 |
| raya-orchestrator-v1.c | 80073CBFFDCBBF96B01879562506616F0ED2E969081D0F50C32BE56D8E534F25 |
| integrated-submit-adapter-v2.c | A9B39D0D4094E8418F820E53B3E28FEE64FC7B0A9EC417450F25EC422EE8D87F |
| pipe-io-owner-v2.c | 9A27A45414D4A58374620B761D7BD0E9CDD832D48A68598214C0CF25E1D529BC |
| pipe-io-finalizer-v2.c | EEB081AAEE9C76357F71305C0B12DC127166C732F05476364CAC65752E850368 |
| reaper-managed-worker-v2-lifecycle-seam-test.c | 1A4A21E39D11CCDB65C809E1D149DB4FF9D9BFF81CB4497E30EB2327666778EF |
