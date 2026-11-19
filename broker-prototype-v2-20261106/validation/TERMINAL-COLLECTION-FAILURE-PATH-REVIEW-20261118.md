# Terminal Collection Failure-Path Review — 2026-11-18

Scope: inspection-only review of `reaper-managed-worker-v2.c`, `reaper-integrated-v2.c`, `pipe-io-owner-v2.c`, and the integrated fault-injection, managed-worker completion, abort, close-failure, pipe-close-failure, and two-phase-release tests. No source, public API, build script, or README changed. No build/test rerun was needed for this source-path review.

## State-path findings

1. **Wait resolution / claim before collection**
   - A non-operation wait result, missing operation/event pair, or failed integrated claim is treated as an ownership invariant failure and degrades the worker.
   - These branches return before completion collection. The queued/promoted operation is not released or callback-completed. The exact residual registry/queue state depends on where the failure occurred; claim itself only changes COLLECT_PENDING to COLLECTING after finding the exact operation+event pair.
   - Existing fault-injection coverage includes wait-plan WAIT_FAILED retention and wake-signal failure retention. It does not directly inject a corrupted exact pair or a failed claim in the managed worker.

2. **Signaled event but completion API says INCOMPLETE**
   - Worker first claims the operation, then calls owner collection. Owner remains REAPER_PENDING and increments collection_attempts; terminal_collections remains unchanged.
   - Integrated collect accepts INCOMPLETE by restoring COLLECT_PENDING. Registry membership, outstanding count, reserved slot, and HANDED_OFF reservation remain held.
   - Worker backs off (1, 2, 4, 8, 16, then 32 ms; retry counter saturates at six) and continues. This is the sole ordinary retry path in the worker.
   - The fault-injection test verifies a second claim and later terminal success, with accounting preserved between observations.

3. **Completion classified UNCERTAIN**
   - Owner collection moves to REAPER_UNCERTAIN. Integrated terminal_hold changes collect_state to UNCERTAIN, marks COMPLETION_UNCERTAIN, and returns DEGRADED.
   - Worker exits without callback. Registry membership, outstanding, reserved slot, and HANDED_OFF reservation remain held. Later claim/collect is blocked by the collect-state guard; no worker retry loop.
   - Existing integrated and managed-worker uncertainty tests assert these retained facts and no release callback.

4. **Terminal success or terminal abort**
   - Owner collection first moves to REAPER_TERMINAL and increments terminal_collections exactly once.
   - Integrated terminal_hold validates the exact operation/event registry pair, outstanding/reserved accounting, and detached-list condition; then removes the registry entry, moves the core operation to TERMINAL_HELD, and changes reservation to TERMINAL_HELD. Outstanding and reserved_slots remain held.
   - Finalizer closes issuing pipe and completion event, then commits release. Owner release commits next; integrated release decrements outstanding/reserved accounting; only then is the user release callback invoked.
   - Abort is a terminal result, not a retry. Native abort test verifies one terminal collection, both handle closes, finalizer commit, and callback after all three release planes commit.

## Failure boundaries after claim

- **Owner collection rejects due to unexpected owner state:** worker marks OWNERSHIP_INVARIANT and exits. Integrated state remains COLLECTING, registry/outstanding/reservation remain held; no callback. No dedicated test directly corrupts owner state at this boundary.
- **Owner collection succeeds terminally, but integrated terminal_hold fails:** owner is already REAPER_TERMINAL, while integrated state remains COLLECTING and registry/outstanding/HANDED_OFF reservation remain held. Worker degrades and exits before finalizer or callback. This is fail-closed but cross-plane divergence; no rollback/reconciliation API or targeted fault-injection test was found.
- **Finalizer close/commit fails:** integrated is already TERMINAL_HELD and removed from registry, while outstanding/reserved accounting remains held. HANDLE_CLOSE degradation blocks integrated release; callback is suppressed. Existing close-failure and pipe-close-failure tests verify retention and callback suppression.
- **Owner release fails after finalizer commit:** finalizer is RELEASED while integrated remains TERMINAL_HELD; worker degrades and suppresses callback. No compensating rollback exists.
- **Integrated release fails after owner release:** owner is RELEASED while integrated remains TERMINAL_HELD and accounting remains held; worker degrades and suppresses callback. This is the cross-plane partial-commit hazard also identified in the terminal-held review.

## API-level distinctions / gaps

- `raya_reaper_integrated_collect` and `raya_reaper_integrated_terminal_hold` both accept INCOMPLETE and UNCERTAIN similarly, but differ for terminal outcomes: collect removes registry membership, terminalizes the core operation, and releases reservation accounting immediately; terminal_hold removes registry membership but retains outstanding/reservation accounting for finalizer-gated release. The managed worker uses terminal_hold.
- An unrecognized collection enum returns INVALID without mutating the operation's COLLECTING state. In the managed worker, the classifier only produces known classes, so this is an API misuse/corruption boundary rather than an ordinary runtime path.
- The integrated terminal path checks exact pair/accounting before mutating; if registry removal or core terminal transition fails after validation, it marks ownership degradation. No rollback is implemented for a partial mutation.
- Owner-level `raya_pipe_owner_reaper_retry` exists for REAPER_UNCERTAIN, but the managed worker does not call it; the integrated state machine also refuses another claim after UNCERTAIN. This is not an operational retry mechanism.
- No user callback is reached on any reviewed failure branch. Ordinary retry is limited to INCOMPLETE. UNCERTAIN and invariant failures retain ownership and terminate the worker.

## Validation evidence reviewed

- `reaper-integrated-fault-injection-test.c`: signaled-but-incomplete retry then terminal success; uncertain retention; wait-failed retention; wake-signal failure retention.
- `reaper-integrated-uncertain-test.c`: uncertain state/accounting/registry retention.
- `reaper-managed-worker-v2-completion-api-test.c`: injected completion API error, uncertain retention, no callback, Join/Destroy blocked.
- `reaper-managed-worker-v2-abort-native-test.c`: terminal abort collected once and release callback occurs after owner/finalizer/integrated release.
- `reaper-managed-worker-v2-closefail-native-test.c` and `reaper-managed-worker-v2-pipeclosefail-native-test.c`: finalizer close failures retain outstanding/reservation, suppress callback, and block clean Join/Destroy.
- `reaper-two-phase-release-test.c`: successful finalizer-gated release and partial-close retention.

## Conclusion / remaining risk

The ordinary terminal collection path is fail-closed and callback-gated. INCOMPLETE is retryable; UNCERTAIN and structural failures are retained, not replayed. Main uncovered test gap is deterministic fault injection between owner terminal collection and integrated terminal_hold (plus corrupted owner/registry-pair claim boundaries). The most important architectural risk remains cross-plane partial commit after terminal_hold: owner/finalizer/integrated state can diverge, with no rollback or production reconciliation API. This review does not establish a recovery capability, recipient receipt, or safe replay.

Stage 5.7 review complete (inspection only). No code changes and no build artifacts created.
