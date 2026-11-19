# Submitter Degraded Event Prototype — 2026-11-16

## Scope and isolation
A temporary copy was made under `%TEMP%\RayaSubmitterDegradedEvent_80a26a645ebc4c838277ea08a184ab06`. Only the copied `dedicated-io-submitter-v2.c/.h` were modified for the prototype; a separate temporary standalone test harness was added. The working-copy source project was not modified. No Production changes, commits, resets, or TLC runs.

## Prototype changes (temporary copy only)
- Added a third manual-reset `degraded_event` to the Submitter object.
- `degrade_locked` publishes `RAYA_SUBMITTER_DEGRADED` and best-effort signals `changed_event`, `wake_event`, and `degraded_event`.
- Submitter main waits on `{wake_event, degraded_event}` with `WaitForMultipleObjects`.
- ACK waiters wait on `{changed_event, degraded_event}`.
- Startup creates the third event before the worker thread. Failure to create it attempts cleanup of the first two handles. Thread-creation failure and normal destroy paths include the third handle.
- Existing reset-under-lock and predicate-recheck sequences were left in place.

## Standalone fault-injection results
Toolchain: Visual Studio 2026 Build Tools 18.7.3 x64, `/W4 /WX /TC`.

10 separate process executions passed. Each execution ran 3 scenarios and 27 assertions; total 270 assertions.

1. **Persistent `wake_event` SetEvent failure; `degraded_event` signal succeeds.** The Submitter thread woke through the independent event and exited degraded. The queued request remained QUEUED, queue count stayed 1, and issue callback count stayed 0.
2. **Persistent `changed_event` SetEvent failure while an ACK waiter is blocked; `degraded_event` signal succeeds.** The ACK waiter returned failure, did not change the synthetic request from ISSUING, and did not decrement the synthetic issuer lease. The Submitter thread observed DEGRADED and exited. This is a focused waiter-state fixture: request state/counters were staged directly under the lock, not produced by a real OS I/O issue.
3. **Both `wake_event` and `degraded_event` signaling fail.** The worker remained blocked (200 ms observation timeout) and the queued request remained retained. A direct test-harness SetEvent on `degraded_event` was then used only to release the test thread. This demonstrates that a third event is not a hard guarantee when its own signal also fails.

## Existing API regression on modified temporary source
The existing test-enabled Integrated Adapter API suite compiled against the temporary Submitter prototype with `/W4 /WX /TC` and passed 10/10 executions:
- 858 assertions per run, 68 cases per run
- 8,580 assertions total
- No source files from the working copy were compiled into or modified by the temporary prototype build; the copied dependency set and headers were used.

Combined prototype-specific and existing-suite evidence: 8,850 assertions across the standalone fault-injection runs and API regression runs. These are separate suites; do not interpret the total as one uniform test suite.

## Interpretation and limits
- The third event successfully isolates the common wake/changed notification failure cases when its own SetEvent succeeds.
- It does not protect against simultaneous failure of `degraded_event` signaling, invalid handle state, or broader OS/process failure. A timeout or another independent observation path would still be needed for those cases.
- The ACK waiter test is a focused state fixture, not an end-to-end real overlapped-I/O ACK waiter test.
- Creation failure, close failure, repeated handle-close faults, normal-vs-degraded simultaneous signal ordering, and full producer/reset/shutdown race permutations were not covered by this prototype run.
- The additional handle expands initialization and destruction failure states. In particular, partial handle-close failure during destroy needs a separate lifecycle/retry design review before any production adoption.
- No timeout value is approved. No event signaling was moved outside the lock. No recovery, ownership release, request replay, or ACK retry is claimed.

## Files and hashes
Working-copy source was read-only during this task:
- `dedicated-io-submitter-v2.c`: SHA256 `D8ABAFD8D3B731DA141E8B1792534A00EB888055D0F15EB0EDFCCE2D01843941`
- `dedicated-io-submitter-v2.h`: SHA256 `2D4CBB91D3400AC5416FC1DE25D03012AC2D2644D6F848A42D278F38D5191655`

Temporary prototype copy hashes before cleanup:
- `dedicated-io-submitter-v2.c`: SHA256 `E95DCDA8BC947D8C80685D536CD7AC2342669DCAF24FB4D63821B85FF9F9A3D3`
- `dedicated-io-submitter-v2.h`: SHA256 `C06483B64C1E3CC983D53D8F7DB68A925A1E9731C207051252584B568B3E4C31`

## Decision
**Prototype passes its stated narrow cases; not production-ready.** Next gate is a broader lifecycle test matrix for event creation/close failures and simultaneous normal/degraded signals, followed by real issue/ACK integration under fault injection. Keep Production NO-GO and Phase 1 NOT ACCEPTED.