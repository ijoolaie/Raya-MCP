# BORROWED Worker Lifecycle Boundary â€” Evidence-Based Correction to External Acknowledgement Proposal â€” 2026-11-17

Status: read-only correction and source audit. Working Copy only.
Phase 1: NOT ACCEPTED. Production: NO-GO.

## 1. Correction to the immediately preceding proposal

The prior report `BORROWED-WORKER-EXTERNAL-OWNER-ACKNOWLEDGEMENT-CURSOR-DESIGN-20261117.md` proposed requiring a new external-owner acknowledgement in cursor slot 5 before reclaiming the Orchestrator Context.

That requirement is not supported by the current component dependency graph and is stronger than the existing documented ownership contract. The source review below finds no production reference from Worker to Orchestrator Context, and the Adapter release callback points to its own envelope, not to Orchestrator Context. Once Adapter and Submitter cleanup has completed successfully, the borrowed Worker can remain running under its independent owner without an additional Worker lifecycle acknowledgement being necessary merely to reclaim the Orchestrator Context.

Therefore, do not implement the slot-5 acknowledgement proposal as written. It should be superseded by the ownership-policy skip design in this report.

## 2. Direct source evidence

### Orchestrator Context
`raya_orchestrator_context` stores pointers to Registry, Adapter, Submitter and Worker, plus immutable-after-registration Worker ownership metadata. It is not passed to Worker callbacks in the inspected production path.

### Adapter envelope and release callback
The Adapter envelope stores:
- pointer to Adapter;
- caller completion callback and caller callback_context;
- Submitter request;
- managed Worker operation.

The managed operation's release callback is `adapter_worker_release`, with `release_context=e` (the Adapter envelope). That callback dereferences the envelope and its Adapter/Submitter pointers, acknowledges the Submitter request, invokes the caller completion callback, and frees the envelope.

The Worker therefore calls the Adapter envelope release callback, not an Orchestrator Context callback. The caller-supplied `callback_context` is forwarded to the caller's completion callback; the Orchestrator Context is not substituted for it by the Adapter.

### Worker
The inspected Worker implementation uses each operation's release function/context and its own Worker state. Searches of Worker and integrated Worker implementation sources found no Orchestrator Context pointer or Orchestrator API call. Worker has no direct Adapter pointer in its public Worker struct; the Adapter's operation callback context is the indirect link while an Adapter operation is live.

### Adapter Destroy
`raya_integrated_adapter_destroy_ex`:
- atomically closes Adapter admission;
- rejects nonzero Adapter entrants;
- reads active and retained counts under Adapter lock;
- refuses destruction if either count is nonzero;
- only then deletes the Adapter lock and zeroes Adapter storage.

Thus successful Adapter Destroy establishes that no tracked active Adapter envelope and no Adapter retained envelope remains. It does not stop Worker or destroy Submitter.

### Submitter
The Orchestrator then closes Submitter admission, shuts it down, joins its thread, and destroys it. Successful Submitter Destroy is required before Context reclamation.

## 3. Correct ownership interpretation

For BORROWED Worker:
- Orchestrator does not own Worker storage.
- Orchestrator must never call Worker Shutdown, Join or Destroy.
- The external owner remains responsible for Worker lifetime and teardown.
- Orchestrator may reclaim its own Context after Adapter and Submitter have reached their required successful terminal states and all Orchestrator leases are drained.
- Worker remains independently alive; Context reclamation does not imply Worker termination.
- The external owner must keep Worker storage alive for its own use and any other independent users. That is inherent in BORROWED ownership, not a new acknowledgement needed to release the Orchestrator Context.

This conclusion is scoped to the inspected composition. If future Worker callbacks capture Orchestrator Context or if future Adapter/Worker operations retain a Context pointer, this contract must be revisited.

## 4. Correct eight-slot policy

Keep existing steps 0..4:
0 Adapter Destroy â€” COMMITTED + SUCCESS
1 Submitter admission close â€” COMMITTED + SUCCESS
2 Submitter Shutdown â€” COMMITTED + SUCCESS
3 Submitter Join â€” COMMITTED + SUCCESS
4 Submitter Destroy â€” COMMITTED + SUCCESS

Worker steps 5..7 are ownership conditional:

OWNED:
- 5 Worker Shutdown â€” COMMITTED + SUCCESS
- 6 Worker Join â€” COMMITTED + SUCCESS
- 7 Worker Destroy â€” COMMITTED + SUCCESS

BORROWED:
- 5 Worker Shutdown â€” skipped by ownership policy
- 6 Worker Join â€” skipped by ownership policy
- 7 Worker Destroy â€” skipped by ownership policy

No Worker lifecycle API is called in BORROWED mode.

The current `raya_orch_outcome` enum has no `SKIPPED_BY_OWNERSHIP_POLICY` value. Two implementation choices:
A. Add an explicit outcome value and record each of steps 5..7 as COMMITTED + SKIPPED_BY_OWNERSHIP_POLICY. This is clearer for diagnostics, but changes the public enum and transition validator; ABI/source compatibility must be reviewed.
B. Keep slots 5..7 NOT_ATTEMPTED and let the policy-aware Orchestrator completion function interpret them as not applicable only when the registered Context's immutable ownership is BORROWED. This avoids an enum change but leaves no explicit cursor audit marker for the skip.

Preferred: A, if public enum compatibility is acceptable. Otherwise B with explicit policy documentation and tests. Never encode borrowed skip as Worker Shutdown/Join/Destroy SUCCESS.

## 5. Required policy-aware reclamation

The Orchestrator completion predicate must be:
- common slots 0..4: COMMITTED + SUCCESS;
- OWNED: slots 5..7 COMMITTED + SUCCESS;
- BORROWED: slots 5..7 each explicitly skipped, or NOT_ATTEMPTED under option B;
- all leases drained and exact cleanup owner still held;
- Context object kind validated atomically by Registry reclaim operation.

Generic `raya_orch_complete_close` must not reclaim Orchestrator Context kind directly. It should preserve generic behavior for non-Orchestrator object kinds. The kind restriction must be enforced under Registry lock at the final reclaim point, not merely checked in an unlocked Orchestrator preflight.

This report does not implement that guard or completion API.

## 6. Important retained-work limitation

Successful Adapter Destroy is stronger than merely observing Worker retained_count==0: it requires Adapter active_count==0 and Adapter retained_count==0, and its own retained list must be consistent with its count. However, it does not independently prove every Worker operation in the entire Worker belongs to this Adapter. Worker may have independent users and operations.

For BORROWED Worker, Orchestrator must not inspect global Worker idleness and then infer it may stop or destroy the Worker. It only needs to establish that its own Adapter/Submitter relationship is terminal and no remaining Orchestrator/Adapter callback can touch Context. The external Worker owner remains responsible for all Worker-wide users.

If Adapter Destroy is BUSY/INVALID, or Submitter cleanup fails/abnormally exits, Context reclamation remains blocked.

## 7. Test matrix correction

BORROWED:
- actual Adapter Destroy + Submitter admission close/Shutdown/Join/Destroy succeed;
- Worker remains initialized, started, unjoined, with live thread handle;
- no Worker Shutdown/Join/Destroy API invoked by Orchestrator;
- slots 5..7 recorded as ownership-policy skips (or remain NOT_ATTEMPTED under option B);
- policy-aware Context completion succeeds;
- external owner subsequently shuts down/joins/destroys Worker independently;
- verify Worker callbacks cannot reference reclaimed Context (source-level contract plus runtime fixture);
- generic Registry completion bypass against Context kind is rejected.

OWNED:
- each Worker step required and successful;
- missing step blocks;
- abnormal Join blocks;
- DEGRADED Worker blocks;
- Destroy partial retry only through component-tracked retry;
- generic Registry completion bypass rejected.

Generic Registry:
- non-Orchestrator kind retains existing permissive semantics;
- generation reuse and generic cursor tests remain valid.

The prior 30+30 actual API harness is useful baseline evidence but did not test checked-in Orchestrator APIs, cross-thread external ownership, or late raw-pointer callers. It is not sufficient for Phase 1 acceptance.

## 8. Remaining risks / non-claims

- No source-level lifetime gate protects arbitrary raw Worker pointers after the external owner frees storage. External owner must prevent late callers and retain storage until its own users drain.
- This review does not prove process-wide Worker quiescence; it is not required for Orchestrator Context reclamation under the inspected no-back-reference contract.
- A future callback that captures Context invalidates this conclusion.
- Registry cursor is volatile in-memory state, not durable recovery.
- Public Registry enum/function ABI compatibility remains open.
- No external consumer inventory is available from this Working Copy.

## 9. Scope and verification

- Read-only inspection of Orchestrator Context header/implementation, Adapter header/implementation, Worker header/implementation, integrated Worker implementation, and existing actual Owned/Borrowed harness report.
- No source/header/test/config/README changes.
- No build/test run.
- No process termination, install/download, or temporary artifact operation.
- No Production changes; no Commit/Reset.

## 10. Verified source SHA-256

| File | SHA-256 |
|---|---|
| integrated-submit-adapter-v2.c | A9B39D0D4094E8418F820E53B3E28FEE64FC7B0A9EC417450F25EC422EE8D87F |
| integrated-submit-adapter-v2.h | DB52B1D6723DCA1159AD8737B83E64EFE26FF4D5F4053A32F9D1C8CD6C0AA1DC |
| reaper-managed-worker-v2.c | 5B0B43BA60303A0EC3EA07A9E26A641E32E40812702C0F87EC37C1ADD11DD8DD |
| reaper-managed-worker-v2.h | 5D0230BC5295AF2DFE8B8BC4B64E0E7D1938F977A446F0C785CEA0C2E0918F96 |
| reaper-integrated-v2.c | 3CFECEB6850324C0D8436D161F26C010555E63C3E1CB8F5F597829A53725E952 |
| raya-orchestrator-v1.h | A73F9961F7C02EE3F64488DC7920BDD092BA4F5763FF05A74A2B4FBF6B342EC7 |
| raya-orchestrator-v1.c | 80073CBFFDCBBF96B01879562506616F0ED2E969081D0F50C32BE56D8E534F25 |
| raya-orchestrator-registry-v1.h | 032574EF501ED535F600CC0E9F932C42F1D0BB43907BB7851ABE5DF8FAED5CAE |
| raya-orchestrator-registry-v1.c | 7805F1780BF44E8A40A3A2F0B9E26E22A304549A546FCAAEDDEEFB398073185D |
| ORCHESTRATOR-OWNED-BORROWED-ACTUAL-API-LIFECYCLE-20261116.md | inspected existing report; prior test result not rerun in this review |
