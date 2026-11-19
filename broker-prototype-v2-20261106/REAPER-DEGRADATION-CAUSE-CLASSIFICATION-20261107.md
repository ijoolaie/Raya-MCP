# Reaper degradation cause classification — 2026-11-07

## Changes in this step

- Integrated collection result `RAYA_INTEGRATED_UNCERTAIN_RESULT` records
  `RAYA_RECOVERY_CAUSE_COMPLETION_UNCERTAIN` while retaining the operation,
  registry entry, reservation and outstanding count.
- The legacy worker callback failure path records
  `RAYA_RECOVERY_CAUSE_COMPLETION_UNCERTAIN`; the callback contract says only
  that terminal collection could not be confirmed, so the worker retains ownership.
- Core dequeue detects a queue/state accounting mismatch and now records
  `RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT` together with DEGRADED.
- Integrated promote/collect structural mismatches continue to use the
  conservative generic marker, which maps to OWNERSHIP_INVARIANT.
- Sync SetEvent, wait failure and ResetEvent paths retain their explicit
  WAKE_SIGNAL, WAIT_FAILED and WAITSET_INVALID classifications.

## Verification

- Integrated Reaper: 25 assertions, 0 failures.
- Integrated fault injection: 54 assertions per run, 25/25 runs passed.
- Worker: all assertions passed, including the new completion-uncertain cause check.
- Sync regression: all assertions passed.
- All builds used MSVC x64 with /W4 /WX.
- Build outputs created by these test runs were removed.

## Remaining classification limits

- Worker thread creation failure still uses the conservative generic marker;
  it is fail-closed and is not a polling-recovery eligible cause.
- HANDLE_CLOSE is not wired into a live degradation transition. Teardown close
  failures occur after the current STOPPED-only destroy preconditions and need
  a separate lifecycle/ownership design; do not force a cause bit into a
  stopped core.
- Native wait-failed/wake-failure harnesses and older test-only harnesses still
  contain generic cause injection. They are not production worker paths and
  should be converted as those harnesses are next maintained.
- No recovery promotion is enabled. DEGRADED still blocks promotion.
- Follow-up audit (2026-11-08): remaining generic degradation calls were reviewed. The Core legacy implementation and Core/fault-injection tests intentionally exercise the conservative OWNERSHIP_INVARIANT default; two Integrated fault-injection cases were corrected to use exact WAIT_FAILED and WAKE_SIGNAL causes. Core cause test now covers HANDLE_CLOSE as a sticky DEGRADED cause that blocks recovery and cannot transition directly to STOPPED. See PIPE-IO-LIFETIME-AUDIT-20261108.md for the source-level resource-lifetime blockers confirmed in pipe-io-v2.c; no implementation changes or recovery enablement were made.
- This work does not resolve the known overlapped-I/O resource lifetime defects
  in pipe-io-v2.c and does not constitute runtime acceptance of the real pipe
  I/O implementation.

## Safety status

Prototype only. No service provisioning, production integration, tunnel change,
or IranPriceScout change was performed. Production remains NO-GO.
