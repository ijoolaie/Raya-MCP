# Stage 5.18 — Gate 1 Source and Lock Audit

Date: 2026-11-20  
Status: PARTIAL — lock-order prerequisite established for the audited implementation; wake-cause gating and production publication are not accepted  
Scope: broker-prototype-v2 only. Read-only source audit; no code or runtime behavior changed.

## Files audited

- `reaper-managed-worker-v2.c/.h`
- `integrated-submit-adapter-v2.c/.h`
- `dedicated-io-submitter-v2.c/.h`
- `reaper-integrated-v2.c/.h`

The audit inspected all Worker, Adapter, and Submitter critical-section acquisitions in these modules, callback call sites, handoff transitions, retained publication paths, and the test-only transferred-degraded callback.

## Findings

### 1. Current lock ordering

No Adapter-lock -> Worker-lock nested acquisition was found in the audited source.

- `retain_envelope`: acquires/releases Adapter lock, then separately acquires/releases Worker lock.
- `retain_post_terminal_ack_notification`: same sequential pattern.
- Adapter admission, submit, backpressure, cleanup, snapshot and destroy paths do not hold Adapter lock while calling Worker APIs.
- `adapter_free` acquires only Adapter lock.
- Worker implementation does not acquire Adapter lock in its ordinary production paths.
- The one observed nested Worker -> Adapter acquisition is the `RAYA_ADAPTER_TESTING` callback `adapter_quarantine_transferred_degraded`, invoked while Worker handoff still holds Worker lock. This matches the proposed direction.
- No callback path was found that holds Adapter lock and then enters Worker lock before releasing Adapter lock.

### 2. Submitter callback boundary

In `dedicated-io-submitter-v2.c`, the Submitter removes the request and releases Submitter lock before calling `issue`, `publish`, or `not_issued`. In particular, `publish` runs outside Submitter lock, then Submitter lock is reacquired to commit request state. Therefore the existing Adapter publish callback can enter Worker handoff without inheriting Submitter lock.

The Worker release callback is also invoked after the Worker lock is released. The Adapter callback may then acknowledge the Submitter; this does not create a Worker -> Submitter nested lock.

### 3. Critical unresolved wake-cause gate

The current test-only `managed_worker_handoff_internal` calls `on_degraded` whenever the result is `RAYA_MANAGED_HANDOFF_TRANSFERRED_DEGRADED`. That result is produced for both:
- pipe-owner handoff failure, which marks `RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT`; and
- wake signal failure, which marks `RAYA_RECOVERY_CAUSE_WAKE_SIGNAL`.

The callback signature receives only the operation and context. It does not receive the cause, and `adapter_quarantine_transferred_degraded` does not independently validate that the Worker cause contains WAKE_SIGNAL and excludes the owner-handoff failure case. Thus the current hook does **not** prove the design's required “only after committed transfer and only for WAKE_SIGNAL” condition.

Before this callback can serve as the Gate 2 publication seam, handoff must distinguish the failure reason while still holding Worker lock, and the callback must be invoked only for the WAKE_SIGNAL branch. The callback must continue to reject mismatched operation/envelope identity and duplicate publication.

### 4. Current hook is not the production design

The existing quarantine callback is compiled only under `RAYA_ADAPTER_TESTING`. It links the envelope into the existing Adapter retained list and sets test-only `transferred_degraded_quarantined`; it is not a distinct production in-flight quarantine registry. It therefore supplies useful deterministic lock-boundary evidence but does not implement Candidate A.

Similarly, the finite 100 ms Worker wait is test-only. The non-test-macro production branch still calls `WaitForMultipleObjects(..., INFINITE)`. No bounded production observation has been implemented or measured.

### 5. ABI, inventory and ownership constraints

- Worker v1 retained inventory remains a separate post-terminal/retained-record contract.
- The current test hook does not publish this pre-terminal queue-owned operation into Worker retained inventory.
- No new production registry, diagnostic version, ABI, ownership transition, recovery, cleanup, ACK, replay, or reconciliation behavior was introduced by this audit.
- The lock audit alone does not authorize changing any of those contracts.

## Gate 1 disposition

| Requirement | Result |
|---|---|
| Enumerate Worker/Adapter lock acquisitions in audited modules | PASS |
| Prove no Adapter -> Worker nested lock in audited modules | PASS |
| Confirm Submitter callback runs outside Submitter lock | PASS |
| Confirm Worker release callback runs outside Worker lock | PASS |
| Callback only for committed transfer + WAKE_SIGNAL | BLOCKED — current test hook conflates two degraded causes |
| Production atomic in-flight registry exists | NOT IMPLEMENTED |
| Production bounded wait exists | NOT IMPLEMENTED |

**Gate 1 remains PARTIAL, not PASS.** The lock-order prerequisite is established for the audited source. Cause-specific callback gating is the immediate blocker. The production registry and bounded wait remain later implementation gates.

## Next permitted slice

Implement and test the cause-specific distinction in the broker prototype only, preserving production semantics and v1 inventory. The test callback must be invoked only for a committed handoff whose wake signal failed. Add a negative owner-handoff-failure case proving the callback is not invoked. Do not yet reuse Worker v1 retained records, add recovery/reconciliation, or release any uncertain record.

No tests or builds were run for this read-only audit. No temporary files or test artifacts were created.