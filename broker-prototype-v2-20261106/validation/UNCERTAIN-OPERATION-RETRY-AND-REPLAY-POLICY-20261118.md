# Uncertain Operation Policy Review — 2026-11-18

## Scope
Reviewed completion classification, managed Worker control flow, integrated Reaper state transitions, Submitter issue boundary, Adapter handoff behavior, existing uncertain-path tests, and the recovery/reconciliation contract. Policy review only; no source, header, test, build script, configuration, or README changed. No test rerun.

## Normative policy

1. **NOT_ISSUED / positively rejected before OS acceptance**
   - May use caller-owned cleanup only when the source path proves Issue was not accepted and the Submitter admission/request has been withdrawn.
   - Finalizer close, owner release, reservation unreserve, Worker finish, callback and free remain ordered and individually checked.
   - Any failed/unknown cleanup substep moves to retained quarantine; do not guess or skip ahead.

2. **INCOMPLETE**
   - The sole ordinary completion retry class.
   - Keep operation, registry membership, outstanding count, reservation, and HANDED_OFF ownership intact.
   - Restore COLLECT_PENDING after the owner records an incomplete observation.
   - Retry completion observation with bounded backoff; current Worker delay is 1, 2, 4, 8, 16, then 32 ms with retry counter saturation.
   - Do not resubmit ReadFile/WriteFile. This is completion observation retry, not I/O replay.

3. **TERMINAL_SUCCESS / TERMINAL_ABORTED**
   - Only a positively classified terminal completion enters terminal hold.
   - Remove exact operation/event registry pair, retain accounting during finalizer work, close both owned handles, commit finalizer, release owner, release integrated accounting, then invoke callback.
   - Abort is a terminal result, not a retry.

4. **UNCERTAIN**
   - Treat as a control-flow/degradation decision, not proof of terminal I/O outcome.
   - Mark owner/integrated state uncertain, preserve ownership/resources/accounting/registry as the path requires, suppress callback, and stop the Worker completion loop.
   - No automatic retry of completion, no transition back to COLLECT_PENDING, no manual handle close/free, and no Worker/Adapter/Submitter teardown through ordinary success path.
   - Do not replay READ or WRITE. In particular, never replay uncertain WRITE absent a recipient-side stable idempotency key and authoritative recipient status/receipt protocol; neither exists in the inspected live path.

5. **PARTIAL_COMMIT / ownership invariant failure**
   - Preserve all remaining ownership and record already committed substeps.
   - Do not roll back a successful close/release or repeat a prior step blindly.
   - Keep callback suppressed and component teardown blocked until a future evidence-specific reconciliation contract can prove a safe action.

## Retry matrix

| State | Completion observation retry | Resubmit I/O | Release/free/callback |
|---|---|---|---|
| NOT_ISSUED, cleanup proven | Not applicable | No | Only after ordered cleanup commits |
| INCOMPLETE | Yes, bounded backoff | No | No, until terminal result |
| TERMINAL_SUCCESS | No | No | Ordered terminal release, then callback |
| TERMINAL_ABORTED | No | No | Ordered terminal release, then callback |
| UNCERTAIN | No | No | No; quarantine |
| PARTIAL_COMMIT / UNKNOWN | No | No | No; quarantine |

## Existing evidence reviewed

- Completion classifier and managed Worker branch: only INCOMPLETE returns to ordinary completion polling; UNCERTAIN returns from the Worker loop before finalizer/owner/integrated release and callback.
- Integrated Reaper claim/collect/terminal_hold guards reject a subsequent claim once collect_state is UNCERTAIN.
- Existing uncertain tests assert retained registry/outstanding/reservation state, no callback, degraded lifecycle, and failed clean Join/Destroy.
- Terminal abort native test asserts one terminal collection, two successful closes, finalizer release and callback only after owner/integrated release.
- Terminal collection failure review confirms INCOMPLETE is the only ordinary retry path and no safe replay/reconciliation path exists.
- Retained recovery contract confirms production enumeration/reconciliation/release/replay APIs are absent.

## Conclusion
The current policy is consistent and must remain conservative: retry only completion observation classified INCOMPLETE; never resubmit accepted/unknown I/O; terminal success and abort release in strict order; UNCERTAIN and partial commit remain quarantined with callback suppressed. No recovery behavior is added by this policy review.

No source changes, no build artifacts created, no cleanup performed, no Commit/Reset. Stage 5.12 review complete (policy consolidation only).
