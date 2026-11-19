# Native harness degradation-cause assertions — 2026-11-07

## Scope

Classified only native harness transitions whose injected/observed cause is explicit.
This does not change the recovery policy or enable promotion while DEGRADED.

## Cause assignments

- Native WAIT_FAILED retention, single-operation polling recovery, 70-operation
  multi-window polling, and concurrent polling harnesses now record
  RAYA_RECOVERY_CAUSE_WAIT_FAILED at the observed WaitForMultipleObjects failure.
- Concurrent polling classifies WaitPlan construction failure as WAITSET_INVALID
  and Registry resolution/claim mismatch as OWNERSHIP_INVARIANT. Its compound
  completion/data/accounting failure branch remains conservatively generic.
- Native wake-failure retention records WAKE_SIGNAL for the injected failed
  SetEvent after handoff. A separate impossible handoff failure remains
  OWNERSHIP_INVARIANT.
- Each harness asserts the exact degradation cause bitset after the transition.

## Verification (MSVC x64 /W4 /WX)

- WAIT_FAILED retention: 29 assertions/run, 25/25 runs passed.
- Single-operation polling harness: 35 assertions/run, 25/25 passed.
- 70-operation multi-window harness: 1,207 assertions/run, 10/10 passed.
- Concurrent 70-operation polling harness: 715 assertions/run, 10/10 passed.
- Native wake-failure retention: 381 assertions/run, 10/10 passed.
- All build scripts completed successfully and removed their generated artifacts.

## Limits

- The polling harnesses are still isolated/test-only. This classification does
  not integrate a production lifecycle-managed Worker or establish the 28-group
  acceptance gate.
- Native multiproducer shutdown now classifies observed handoff/promotion/
  Registry mismatches as OWNERSHIP_INVARIANT, failed wake SetEvent as WAKE_SIGNAL,
  ResetEvent/WaitPlan failures as WAITSET_INVALID, and native WAIT_FAILED as
  WAIT_FAILED. Uncertain completion is retained through Integrated collection
  as COMPLETION_UNCERTAIN. Its successful path asserts a zero cause bitset;
  MSVC x64 /W4 /WX passed 10/10 runs (241 assertions/run; 2,410 total).
- The concurrent-poll harness retains one compound completion/data/accounting
  branch as generic fail-closed because its cause is not uniquely established.
  Several older integrated concurrency/live-pipe harnesses also retain generic
  fail-closed injections pending context-specific review.
- No recovery promotion was enabled. DEGRADED remains non-promotable.
- pipe-io-v2.c resource lifetime defects remain unresolved.

Production remains NO-GO. No service, production, tunnel, or IranPriceScout
changes were made.

## Integrated concurrency and live-pipe cause rollout

- Integrated concurrent-producer worker harness: wake SetEvent -> WAKE_SIGNAL; ResetEvent -> WAITSET_INVALID; promotion/claim/resolve/accounting mismatches -> OWNERSHIP_INVARIANT; WaitForMultipleObjects WAIT_FAILED -> WAIT_FAILED; WaitPlan construction -> WAITSET_INVALID. Successful path asserts zero cause bits. MSVC x64 /W4 /WX: 541 assertions, 0 failures; 256/256 handoffs and collections.
- Reset/enqueue race: the same event, waitset, and ownership distinctions are now explicit. Successful path asserts zero cause bits. MSVC x64 /W4 /WX: 543 assertions/run, 25/25 passed (13,575 total); 256/256 handoffs, promotions, and collections per run.
- Concurrent shutdown admission race: wake SetEvent failure maps to WAKE_SIGNAL; successful path asserts zero cause bits. MSVC x64 /W4 /WX: 23 assertions/run, 25/25 passed (575 total).
- Integrated live named-pipe worker: ResetEvent/WaitPlan -> WAITSET_INVALID; promotion and Registry claim/collection mismatches -> OWNERSHIP_INVARIANT; native WAIT_FAILED -> WAIT_FAILED; uncertain completion is verified as COMPLETION_UNCERTAIN. Successful 70-success/1-aborted run asserts zero cause bits. MSVC x64 /W4 /WX: 545 assertions, 0 failures.
- Concurrent 70-operation polling harness now separates non-terminal/uncertain completion from payload/accounting mismatch: COMPLETION_UNCERTAIN versus OWNERSHIP_INVARIANT. Its original WAIT_FAILED, WAITSET_INVALID and Registry cause checks remain. MSVC x64 /W4 /WX: 715 assertions/run, 10/10 passed (7,150 total), 70/70 collected each run; lifecycle remains DEGRADED and non-promotable.
- Worker sync terminal accounting failure now records OWNERSHIP_INVARIANT. Worker callback failure remains COMPLETION_UNCERTAIN. MSVC x64 /W4 /WX worker regression passed all 16 checks.

These are test-harness classifications and focused regression evidence only. Generic calls remaining in core/fault-injection tests intentionally exercise the legacy fail-closed default; they are not observed native failure paths. The recovery policy remains unchanged and DEGRADED promotion remains disabled. `pipe-io-v2.c` lifetime defects and the 28-group acceptance gate remain blockers.
