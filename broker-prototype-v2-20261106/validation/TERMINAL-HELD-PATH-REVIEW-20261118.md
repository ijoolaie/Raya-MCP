# TERMINAL_HELD Path Review — 2026-11-18

## Scope
Stage 5.6 review in the Working Copy. Inspected:
- reaper-integrated-v2.c
- reaper-managed-worker-v2.c
- pipe-io-finalizer-v2.c / .h
- pipe-io-owner-v2.c / .h
- reaper-two-phase-release-test.c
- reaper-managed-worker-v2-completion-api-test.c
- existing two-phase release report

No source, public API, build script, or README was changed.

## Verified state machine

### Terminal collection and hold
For a positively classified terminal success/abort, managed Worker:
1. records owner terminal collection;
2. calls integrated_terminal_hold under the worker lock;
3. validates the exact (operation,event) registry pair and outstanding/reservation accounting;
4. removes the operation from the wait registry;
5. changes operation state to TERMINAL_HELD and reservation to TERMINAL_HELD;
6. leaves core.outstanding and reserved_slots unchanged.

This is the intended gap between terminal I/O observation and resource/accounting release. The operation record must remain pinned while finalization runs outside the lock.

### Finalizer
pipe-io-finalizer closes issuing pipe then completion event:
- successful close nulls that handle and advances the finalizer state;
- both successful closes are required for HANDLES_CLOSED;
- commit_release requires both handles NULL, exactly two successful closes, and no previous commit;
- a close failure moves the finalizer to CLOSE_UNCERTAIN; subsequent close_handles calls fail closed rather than retrying an ambiguous close.

### Release sequence
The managed worker sequence is:
terminal_hold -> close handles -> finalizer commit -> owner release -> integrated release -> user release callback.

Integrated release requires TERMINAL_HELD state and a positive reservation; it decrements outstanding/reserved accounting and changes the operation to RELEASED. A HANDLE_CLOSE degradation cause blocks it. Worker clean exit remains impossible while the terminal-held operation is outstanding.

## Failure-path findings
- Partial handle-close failure retains terminal-held accounting and prevents clean exit. Existing test injects first-close success / second-close failure and verifies the operation remains outstanding/reserved and release is rejected.
- A finalizer close failure is intentionally non-retryable in the current finalizer state machine. This avoids blindly repeating a close operation whose outcome is treated as uncertain, but leaves no in-process repair/reconciliation mechanism.
- If finalizer succeeds but owner release or integrated release fails, the worker marks an ownership invariant degradation and does not invoke the release callback. The operation/accounting may remain retained even though some earlier ownership/finalizer transitions have committed.
- Ordering note: owner release commits before integrated release. If integrated release then fails, owner state may already be RELEASED while integrated accounting remains TERMINAL_HELD. This is a fail-closed outcome, but creates a cross-plane mismatch requiring diagnostics; no rollback API exists.
- Integrated release blocks specifically on HANDLE_CLOSE and validates its own state/accounting. It does not generically reject every possible degradation cause. This behavior should not be broadened without tracing all callers and intended fault classes.

## Tests executed
1. build-reaper-two-phase-release-test.cmd
   - MSVC 2026 Build Tools 18.7.3 x64, /W4 /WX /TC
   - 25 runs × 43 assertions = 1,075 assertions; zero failures.
2. build-reaper-managed-worker-v2-completion-api-test.cmd
   - same compiler/toolchain flags
   - 10 runs × 33 assertions = 330 assertions; zero failures.
   - Confirms uncertain completion is retained; not itself a terminal-held success test.

Both build scripts exited 0. Exact generated artifacts were checked before and after. The two-phase build generated 6 named outputs and left 0; completion API build generated 13 named outputs and left 0. No pre-existing exact outputs were present. Disk free space was checked before/after; no cleanup of unrelated files was performed.

## Limits
- The two-phase release test uses synthetic handles and a fake close function for fault injection.
- The completion API test covers the uncertain path, not all terminal-held finalizer outcomes in the real named-pipe integration.
- No proof of durable recovery, recipient receipt, reconciliation, or safe replay is added.
- No production API or behavior was changed.

## Outcome
Stage 5.6 TERMINAL_HELD review: COMPLETE (inspection plus existing regression tests).
Primary residual risk: cross-plane partial-commit on owner-release success followed by integrated-release failure, and no recovery mechanism for finalizer CLOSE_UNCERTAIN.
Next: Stage 5.7 Terminal Collection failure-path review.
