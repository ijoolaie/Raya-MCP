# Retained Cause, Ownership and I/O Boundary Matrix — 2026-11-17

Status: read-only source audit. No implementation change.
Working Copy: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`.

## 1. Purpose and corrected scope

This report continues the Adapter retained inventory review by mapping each observed retain branch to its I/O stage, ownership topology, and evidence that is actually available. It also distinguishes Adapter-retained envelopes from direct managed-pipe Worker retention.

Important correction: retained inventory is not one unified list. Adapter maintains an envelope list; Worker maintains a managed-operation list; `managed-pipe-io-v2.c` has its own Worker retained insertion paths. Membership in one list does not prove membership in another.

## 2. Adapter envelope state and evidence fields

The Adapter envelope contains:
- Submitter request;
- managed operation (buffer, issuing pipe, completion event/OVERLAPPED, owner/finalizer/integrated operation state);
- Adapter pointer, completion callback and callback context;
- immediate native error;
- READ/WRITE discriminator;
- retained, worker_admitted, worker_finished, worker_owns_operation, worker_terminal_released flags;
- retained-list next pointer.

It does not contain a stable public operation ID, durable identity, explicit first-retain cause, or a typed immutable I/O-effect record. The test-only snapshot exposes raw addresses and selected internal state, but not a production-stable identity.

## 3. Adapter retain branch matrix

| Branch / cause | I/O status at branch | Ownership / retained evidence | Safe interpretation |
|---|---|---|---|
| Event created; DuplicateHandle fails; Event CloseHandle also fails | No ReadFile/WriteFile call | Event retained in envelope; issuing pipe set INVALID_HANDLE_VALUE; finalizer/owner state may not be fully initialized | Pre-I/O handle cleanup uncertainty. Preserve Event and envelope; no I/O replay question yet. |
| Managed operation initialization fails | No ReadFile/WriteFile call | Duplicate pipe + Event placed in operation; finalizer initialized if possible; retain cause ownership invariant or handle close | Pre-I/O initialization/ownership failure. Do not free handles absent exact close proof. |
| Worker admission fails; owner transition fails | No I/O call | Operation initialized; Worker did not admit; retain ownership invariant | Caller-side ownership transition uncertain. |
| Worker admission fails; finalizer close/commit or owner release fails | No I/O call | Caller terminal path attempted; finalizer and owner records carry substep evidence | Cleanup-only retention; retry must be specific to failed substep and handle ownership. |
| Submitter admission fails; owner/finalizer/owner-release path fails | No I/O call | Worker admitted; Submitter not admitted; may call worker_finish_once; retained if cleanup incomplete | Pre-I/O but Worker admission reservation exists; preserve reservation/ownership chain. |
| Worker reserve fails and Submitter admission abort fails | No I/O call | Submitter request may still be admitted; retain ownership invariant | Request admission ambiguity; cannot assume queue/request was withdrawn. |
| Worker reserve fails; abort succeeds; owner/finalizer cleanup fails | No I/O call | Request admission withdrawn; caller-side cleanup partially attempted | No I/O accepted; reconcile only local cleanup/ownership effects. |
| Submitter enqueue returns RETAINED | No demonstrated Issue call at this point | Worker reservation exists; Submitter request retained by Submitter contract; Adapter retains envelope | Request state cannot be collapsed into simple rejection; inspect Submitter state and do not free envelope. |
| Enqueue rejects/backpressure; abort fails | No Issue call demonstrated | Submitter admission may remain; Worker reservation exists | Ambiguous admission ownership; quarantine. |
| Enqueue rejects/backpressure; abort succeeds; caller terminal/finalizer/owner release fails | No Issue call | Worker reservation may remain until terminal/unreserve; finalizer/owner state is recorded | Cleanup-only retained path; no I/O replay. |
| Caller-terminal transition fails | No Issue call | Worker integrated operation state remains uncertain; degradation marked | Do not assume reservation can be unreserved. |
| Finalizer close/commit or Pipe Owner release fails after caller-terminal | No Issue call | Caller terminal was accepted; finalizer/owner substate identifies progress | Local resource cleanup failure; no I/O accepted, but don't manually free. |
| Worker unreserve fails after finalizer/owner release | No Issue call | Local handles may already be closed/released; Worker reservation remains | Partial cleanup. Do not retry earlier close blindly; reconcile reservation only. |
| worker_finish_once fails after cleanup/unreserve | No Issue call | Worker caller accounting/finish transition uncertain | Preserve component storage; no generic retry. |
| Worker handoff returns NOT_TRANSFERRED | ReadFile/WriteFile was already accepted by Submitter Issue path | Worker handoff failed; Adapter tries worker_finish_once; if that fails it retains | OS accepted does not mean Worker owns completion. Do not route through NOT_ISSUED cleanup or reissue. |
| Worker handoff returns TRANSFERRED_DEGRADED | I/O accepted and ownership transfer occurred | Production handoff reports degraded; test-only hook publishes Adapter quarantine; Worker has the operation in its core ownership topology | Transferred but unhealthy; no normal completion callback. Preserve Worker-owned operation and resources. |
| Worker release callback: Submitter ACK fails | Worker terminal release callback has been reached; operation resources may already be terminal-released | Adapter calls retain_envelope with ownership invariant; worker_terminal_released is TRUE | Terminal local I/O/resource path may have completed, but Submitter ACK/callback/free did not. Do not re-run I/O. |

## 4. Distinct inventory topologies

### 4.1 Adapter retained list

- Holds `raya_adapter_envelope` nodes.
- Incremented by `retain_envelope` and by the test-only transferred-degraded quarantine hook.
- Public production API exposes only count.
- Adapter Destroy refuses while count is nonzero.
- No production unlink/reconcile/release operation was identified.

### 4.2 Worker retained list reached from Adapter

The general `retain_envelope` helper inserts `&e->operation` into Worker retained list when the envelope is newly retained and either Worker does not own the operation or Worker terminal release has already occurred.

This conditional is important:
- Caller-owned / pre-transfer retained paths can be linked to Worker retained inventory as a safety hold.
- Worker-owned, not-yet-terminal operations are not redundantly linked by this helper; Worker core ownership remains authoritative.
- Worker-owned but terminal-released operations can be linked to Worker retained inventory to preserve the envelope/operation record after the release callback cannot complete Submitter ACK.

The test-only transferred-degraded hook links only the Adapter envelope list; it runs under Worker lock while the Worker owns the operation, and does not also link Worker retained list.

### 4.3 Direct managed-pipe Worker retained list

`managed-pipe-io-v2.c` contains two direct insertion sites into Worker `retained_head`. They are separate from Adapter envelope retention and use managed-operation pointers. They do not automatically create Adapter envelope records.

Consequently, Worker retained count and Adapter retained count are not interchangeable. An Adapter snapshot that reports both lists is a diagnostic cross-section, not a universal inventory of every retained operation in every API profile.

## 5. Evidence boundaries

- Before Issue: no OS I/O submission has been made by Adapter Issue; safe recovery may only concern local admission, reservation, finalizer and owner transitions.
- Issue accepted: ReadFile/WriteFile TRUE or ERROR_IO_PENDING. This is OS acceptance, not recipient/application commit.
- Handoff NOT_TRANSFERRED after Issue accepted: operation is not safe to replay; local ownership remains unresolved.
- Handoff TRANSFERRED_DEGRADED: Worker owns operation but lifecycle is degraded; this is not healthy completion.
- Worker terminal completion + finalizer + Pipe Owner + integrated registry release: local resource chain may be terminal.
- Submitter ACK failure after Worker release: I/O is not to be reissued; the callback/ACK/free chain is incomplete.
- No recipient receipt verifier or remote application commit evidence is available in these paths.

## 6. API and recovery gaps

1. No stable public retained operation ID.
2. No production enumeration API for Adapter or Worker retained nodes.
3. No production per-node cause/effect snapshot.
4. No production reconciliation API or cursor transition from retained state to released state.
5. No general safe unlink/release contract.
6. No proof that all direct managed-pipe Worker retained entries can be mapped to Adapter envelopes.
7. No recipient idempotency/receipt evidence; therefore no safe replay policy for uncertain WRITE.
8. Existing test-only snapshot requires closed Adapter admission, zero Adapter entrants, Worker joined, Submitter queue/producers/active issue zero, and direct Submitter caller quiescence. It is not available in production builds.

## 7. Safe next implementation sequence

1. Define distinct inventory namespaces: Adapter envelope, Worker-owned operation, direct managed-pipe retained operation.
2. Add stable operation identity before any admission/issue boundary; do not use raw addresses as identity.
3. Capture first-retain cause plus monotonic stage/effect facts without overwriting first failure.
4. Add read-only production enumeration with explicit snapshot lifetime and quiescence/lease semantics.
5. Add evidence-specific reconciliation actions for pre-I/O cleanup, post-Issue handoff failure, Worker-degraded transfer, finalizer close failure, and post-terminal ACK failure.
6. Prohibit replay from every state where Issue may have been accepted.
7. Integrate these actions into the Orchestrator cleanup cursor only after the component-level reconciliation contract is tested.
8. Add per-branch fault-injection tests and assert inventory membership across both Adapter and Worker lists.

## 8. Verification

- Read-only inspection of `integrated-submit-adapter-v2.c/.h`, `integrated-submit-adapter-v2-api-test.c`, `managed-pipe-io-v2.c`, `reaper-managed-worker-v2.c/.h`, and `reaper-integrated-v2.c/.h`.
- No build or tests run; inspected existing test assertions only.
- No source/header/test/config/README changed.
- No processes terminated; no install/download; no temporary files created or deleted.
- Production remains untouched.

## 9. Source SHA-256

| File | SHA-256 |
|---|---|
| integrated-submit-adapter-v2.h | DB52B1D6723DCA1159AD8737B83E64EFE26FF4D5F4053A32F9D1C8CD6C0AA1DC |
| integrated-submit-adapter-v2.c | A9B39D0D4094E8418F820E53B3E28FEE64FC7B0A9EC417450F25EC422EE8D87F |
| integrated-submit-adapter-v2-api-test.c | 9F62641155FF413A69DEA3DDDCBD26DF1B062184B6ADE359BF9F42B05C1A142B |
| managed-pipe-io-v2.c | 2FDEF399F1491C613F6E7A8593A4C4117483D2FBB4E67AC3A56559332CF66891 |
| reaper-managed-worker-v2.h | 5D0230BC5295AF2DFE8B8BC4B64E0E7D1938F977A446F0C785CEA0C2E0918F96 |
| reaper-managed-worker-v2.c | 5B0B43BA60303A0EC3EA07A9E26A641E32E40812702C0F87EC37C1ADD11DD8DD |
| reaper-integrated-v2.h | 557072426F216D1AC33BA65E885ADA72890FBBDC8DAFF7BD614492AE2F6DA1AD |
| reaper-integrated-v2.c | 3CFECEB6850324C0D8436D161F26C010555E63C3E1CB8F5F597829A53725E952 |
