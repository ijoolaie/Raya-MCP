# Managed Worker Destroy wake_event CloseHandle Failure and Retry — 2026-11-16

## Result
PASS — 100/100 iterations, 1,200 counted assertions, actual Managed Worker start/shutdown/join/destroy APIs, x64 MSVC `/W4 /WX /TC`.

## Method
A disposable copy of `reaper-managed-worker-v2.c` changed exactly one Destroy call site so that closing `wake_event` uses the already-configured `w->close_handle(w->wake_event, w->close_context)` callback. This is a test-only seam in the disposable copy; the Working Copy source was not edited.

For each iteration:
1. Start actual Worker.
2. Configure the existing close callback before shutdown.
3. Shutdown and Join actual Worker.
4. Inject one failure for the first `wake_event` close.
5. Verify Destroy returns failure and preserves `initialized`, `joined`, and the exact same non-NULL `wake_event` handle value.
6. Retry Destroy; callback closes the handle, and Worker becomes uninitialized with `wake_event == NULL`.
7. Verify exactly two close callback calls (one failure, one success).

## Limits
- Worker-only lifecycle test; no Adapter/Submitter cursor integration in this run.
- Does not test concurrent Destroy callers, concurrent late API callers, or startup rollback.
- No claim that the existing public Destroy implementation currently uses the callback: current Working Copy Destroy still directly calls `CloseHandle`. The callback routing existed only in the temporary source copy.

## Integrity
- No Working Copy source/header/test/build file changed.
- Temporary copy and compiler outputs inspected then removed.
- No Production change, Commit, or Reset.
