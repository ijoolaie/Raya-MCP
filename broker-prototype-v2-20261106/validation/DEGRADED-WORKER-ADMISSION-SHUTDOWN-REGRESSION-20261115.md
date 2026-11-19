# Broker v2 — degraded Worker admission and shutdown regression (2026-11-15)

## Scope
Working copy only:
`D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`

No Production/source-project changes, no commits/resets, and no historical evidence deletion.

## Inspection findings
- `raya_managed_worker_admit` delegates admission while holding the Worker lock; the Reaper core admits only in `RAYA_REAPER_ACCEPTING`.
- The Adapter allocates/duplicates its per-operation handles before Worker admission. If Worker admission is denied, it runs the caller-side finalizer and owner release. If that cleanup fails, the existing code retains the envelope and marks the Worker degraded.
- Worker main exits with `ERROR_INVALID_STATE` when it observes `RAYA_REAPER_DEGRADED`.
- Shutdown does not transition a degraded Worker to STOPPING; Join can observe the nonzero thread exit, and Destroy requires STOPPED plus zero outstanding/retained state. This is intentionally fail-closed, but leaves the Worker object and wake event retained for process lifetime absent an authorized recovery path.

## New regression test
Added `test_submit_rejected_after_worker_degraded` to `integrated-submit-adapter-v2-api-test.c`.

The test:
1. Starts Worker and Adapter.
2. Marks Worker degraded under its lock and signals its wake event.
3. Waits for Worker thread exit.
4. Attempts Adapter Submit and requires REJECTED.
5. Confirms the OS ReadFile seam was never called, both per-operation handles were closed, callback count stayed zero, and Adapter active/retained counts returned to zero.
6. Confirms Adapter and Submitter can be shut down/destroyed.
7. Confirms Worker shutdown and destroy are refused; Join records `ERROR_INVALID_STATE` and marks the thread joined.

The Worker is deliberately not destroyed because its lifecycle is DEGRADED, not STOPPED. The test process releases its process-local resources on exit; this is not a claim that the library has a recovery/cleanup path for degraded Workers.

## Validation
- Build: Visual Studio 2026 Build Tools 18.7.3 x64, `/W4 /WX /TC`.
- Existing API suite increased from 65 to 66 cases.
- 10/10 executions passed.
- 724 assertions per execution; 7,240 assertions total.
- Exit code 0.
- Build output executables and object files were absent before build and all cleaned afterward.
- One initial compile attempt exposed an invalid test-field reference (`write_calls`); corrected to assert the actual read seam count. No production source was affected by that failed attempt.

## Remaining limitations
- The test injects a deterministic degraded lifecycle state; it does not reproduce a real kernel WAIT_FAILED event or exercise recovery.
- There is no authorized Worker recovery API in this path. Degraded Worker remains fail-closed and non-destroyable.
- Adapter storage still requires caller-side lifetime/quiescence guarantees around successful Destroy.

Production remains NO-GO. Phase 1 remains NOT ACCEPTED.
