# Integrated Reaper Fault-Injection Test — 2026-11-07

## Scope
Added a focused, test-only harness for the integrated Reaper ownership core. It does not modify `pipe-io-v2.c`, the production transport, tunnel configuration, or IranPriceScout.

Files:
- `reaper-integrated-fault-injection-test.c`
- `build-reaper-integrated-fault-injection-test.cmd`

## Scenarios
1. **Signaled event / incomplete collector result**
   - A real manual-reset operation event is signaled.
   - WaitPlan + WaitForMultipleObjects observe the operation event.
   - The test injects `RAYA_INTEGRATED_INCOMPLETE` from the collector despite the signaled event.
   - Registry membership, outstanding count, reservation, and worker-pending ownership remain intact.
   - A second collection attempt claims the same exact operation/event pair and then completes terminally.
2. **WAIT_FAILED resolution**
   - The WaitPlan resolver is supplied the `WAIT_FAILED` result.
   - The test verifies the resolver returns `RAYA_WAITPLAN_FAILED`, then marks the core degraded.
   - Registry, outstanding count, reservation, and operation state remain retained; exit is rejected.
3. **Wake-signal failure after handoff**
   - The harness injects the failure outcome immediately after handoff by marking the core degraded without signaling the wake event.
   - Queued ownership, outstanding count, and reservation remain retained; promotion and clean exit are rejected.

## Results
- MSVC x64, `/W4 /WX`: PASS.
- 25 independent process runs.
- 45 assertions per run; 1,125 assertions total.
- 0 failures; command exit code 0.
- Build script removes generated executable and object files after the run.

## Limits
- This is fault injection at the integrated ownership/WaitPlan boundary, not a test of the actual Reaper worker or `pipe-io-v2.c`.
- `WAIT_FAILED` is supplied to the resolver; the harness does not force a real kernel `WaitForMultipleObjects` failure.
- Wake-signal failure is modeled at the state boundary; it does not force the Win32 `SetEvent` API itself to fail.
- The incomplete result is deliberately injected after a real event signal; it does not reproduce an actual overlapped I/O completion/result inconsistency.
- Retained operations in degraded scenarios are intentionally not released. Their handles live until the disposable test process exits.
- No randomized multi-producer stress, cancellation race, wake reset/enqueue race, or full 28-group named-pipe acceptance run is covered.

## Gate
This result improves focused ownership fault coverage only. It does not authorize Reaper integration into `pipe-io-v2.c`, Production transport, service provisioning, or deployment. Production remains **NO-GO**.
