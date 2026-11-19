# UNCERTAIN Path Classification — 2026-11-18

## Scope
Stage 5.5 review only. Working Copy:
D:\Work\project\Raya-MCP\broker-prototype-v2-20261106

Inspected:
- reaper-integrated-v2.c / .h
- reaper-managed-worker-v2.c
- reaper-integrated-uncertain-test.c
- reaper-integrated-fault-injection-test.c
- reaper-managed-worker-v2-completion-api-test.c
- integrated-submit-adapter-v2-api-test.c snapshot validator and uncertainty references

No production code, public API, build script, README, or test source was changed.

## State transition evidence

### A. Completion API reports uncertain
In reaper-managed-worker-v2.c, after a claimed operation:
1. GetOverlappedResult is called through the configured completion API.
2. raya_classify_pipe_completion classifies the result.
3. For RAYA_PIPE_COMPLETION_UNCERTAIN, the worker records owner-side collection with the operation still considered submitted/owned, then calls raya_reaper_integrated_terminal_hold(..., RAYA_INTEGRATED_UNCERTAIN_RESULT).
4. The worker returns an error; it does not execute finalizer close, owner release, or integrated release.

In reaper-integrated-v2.c, terminal_hold accepts only WORKER_PENDING + COLLECTING + HANDED_OFF. For UNCERTAIN_RESULT it:
- sets collect_state = RAYA_INTEGRATED_UNCERTAIN;
- marks RAYA_RECOVERY_CAUSE_COMPLETION_UNCERTAIN;
- returns DEGRADED;
- does not remove the registry entry, terminalize the core operation, decrement reserved_slots, or release resources.

### B. Event/handle observation inconsistent
If the claimed event is not signaled at the immediate zero-time check, worker code records an uncertain owner collection, calls terminal_hold with UNCERTAIN_RESULT, and exits with ERROR_INVALID_DATA. This retains the operation through the same integrated uncertain branch.

### C. Direct integrated collect uncertain result
raya_reaper_integrated_collect has the equivalent branch: collect_state becomes UNCERTAIN and the core is degraded for COMPLETION_UNCERTAIN. Registry membership, outstanding count, and reservation count remain unchanged.

## Recovery/retry boundary
- claim requires collect_state == COLLECT_PENDING and operation state WORKER_PENDING.
- collect and terminal_hold require collect_state == COLLECTING.
- after UNCERTAIN, these preconditions reject another claim/collection attempt.
- The worker thread exits on the uncertain path; there is no in-process retry loop or transition from UNCERTAIN back to COLLECT_PENDING.
- Existing tests explicitly assert retained registry/outstanding/reservation state, no callback, failed Join/Destroy, and intentional process-lifetime retention.
- No production reconciliation, recipient-status query, durable identity, receipt, safe replay, or release API was found in the inspected path.

## Safety interpretation
UNCERTAIN is a terminal *control-flow/degradation decision*, not a proven terminal I/O result. It deliberately prevents ordinary completion/release and preserves ownership because the system cannot prove whether the recipient observed or committed a WRITE. Do not retry/replay an uncertain WRITE and do not free retained operation resources based only on this state.

The owner-side uncertain marker does not establish recipient commit. An OS completion API error or event inconsistency is not a recipient receipt.

## Evidence and limitations
- reaper-integrated-v2.c: collect() lines 108–153; terminal_hold() lines 155–200.
- reaper-managed-worker-v2.c: event check and uncertain completion branches lines 391–440; normal finalization/release follows only the non-uncertain path.
- reaper-integrated-uncertain-test.c: asserts degraded state, registry count 1, outstanding 1, reserved_slots 1, WORKER_PENDING, UNCERTAIN, HANDED_OFF, and cannot exit.
- reaper-integrated-fault-injection-test.c: asserts the same retention for direct uncertain collection.
- reaper-managed-worker-v2-completion-api-test.c: asserts retained state, no callback, Join/Destroy failure, and intentional retention.
- The retained snapshot is diagnostic/test-only and samples Adapter and Worker state under separate locks; it is not an atomic production reconciliation interface.

## Outcome
Stage 5.5 code-path classification: COMPLETE (inspection/evidence only).
No source modification and no build/test rerun were needed for this inspection-only activity.
Next: Stage 5.6 TERMINAL_HELD path review.
