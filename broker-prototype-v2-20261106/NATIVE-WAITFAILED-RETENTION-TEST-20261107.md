# Native WaitForMultipleObjects WAIT_FAILED Retention Test — 2026-11-07

## Scope
A disposable native Win32 test exercises an actual `WaitForMultipleObjects` failure at the call site and verifies that the Integrated Reaper ownership state is retained. It is an isolated failure-path harness, not the production Worker and not a runtime test of `pipe-io-v2.c`.

## Safe failure construction
- Create one operation event and one separate wake event.
- Admit, reserve, hand off, and promote one synthetic operation into the Integrated Registry.
- Build the real WaitPlan containing wake + operation event.
- Close only the disposable wake event immediately before calling `WaitForMultipleObjects`.
- The operation event is not closed and is not associated with active overlapped I/O.
- `WaitForMultipleObjects` returns `WAIT_FAILED`; `GetLastError()` is captured immediately and consistently returns `ERROR_INVALID_HANDLE` (6).
- A zero-time `WaitForSingleObject` probe of the still-valid operation event returns `WAIT_TIMEOUT` (258).

## Assertions
The harness verifies:
- the failure is a native API result, not a mocked return;
- the exact error is captured before any intervening Win32 call;
- WaitPlan resolution classifies the result as FAILED and produces no operation/event pair;
- Integrated Core transitions to sticky `DEGRADED`;
- Registry membership remains 1;
- outstanding ownership remains 1;
- reservation count remains 1;
- operation state remains `WORKER_PENDING`;
- reservation remains `HANDED_OFF`;
- collection state remains `PENDING`;
- the operation event remains valid and unsignaled;
- clean exit is denied.

The operation event is intentionally not closed by the harness after ownership transfer. It remains retained until process exit, matching the conservative ownership rule.

## Results
- MSVC x64 build: PASS with `/W4 /WX`.
- Independent process runs: 25/25 PASS.
- Assertions: 28 per run; 700 total.
- Failures: 0.
- Native wait error: `ERROR_INVALID_HANDLE` (6) on all runs.
- Operation-event probe: `WAIT_TIMEOUT` (258) on all runs.
- Build script removes executable and object files after the repeated run loop.

## Limitations
- One synthetic operation and one invalid wake handle; this is not a multi-operation stress test.
- No active kernel I/O is attached to the retained operation event.
- No production Worker thread is exercised; the test validates the native failure result and the integrated state-retention policy at the call site.
- It does not prove attribution/recovery when the failed handle could be any one of many active operation events.
- It does not implement polling-only recovery or zero-time probing across a full Registry.
- It does not exercise IOCP, cancellation, shutdown under concurrent callers, CloseHandle failure, or the full 28-group acceptance matrix.
- `pipe-io-v2.c` remains unchanged and unaccepted.

## Gate
This adds one focused native failure-path result only. The lifecycle-managed Reaper and all required acceptance groups remain incomplete. Broker Production status remains **NO-GO**.
