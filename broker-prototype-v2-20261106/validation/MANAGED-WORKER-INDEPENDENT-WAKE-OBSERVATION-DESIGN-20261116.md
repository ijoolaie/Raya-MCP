# Managed Worker wake-failure observation design note — 2026-11-16

## Scope
Read-only design investigation following the test-only reproduction of a transferred-but-degraded handoff. No source files were changed and no build or test was run for this note.

## Current behavior confirmed
- `managed_worker_handoff_internal` holds `worker->lock`, commits integrated handoff and owner handoff, then signals `wake_event`.
- If that signal fails, it marks `RAYA_RECOVERY_CAUSE_WAKE_SIGNAL`; the operation is already transferred and queued.
- The Worker thread checks `core.lifecycle == RAYA_REAPER_DEGRADED` only after it enters the top of its loop under `worker->lock`.
- The thread builds a wait plan containing the wake event plus up to 63 operation completion events, releases the lock, then calls `WaitForMultipleObjects(..., INFINITE)`.
- If the failed signal was the only event, the Worker can remain blocked indefinitely and cannot reach its degraded-state check.
- `raya_reaper_waitplan_resolve` already maps `WAIT_TIMEOUT` to `RAYA_WAITPLAN_TIMEOUT`; the enum and resolver test exist. The Managed Worker currently always waits with `INFINITE` and does not handle the timeout result.
- On an actual `WAIT_FAILED`, the Worker already marks WAIT_FAILED and exits. A periodic timeout is aimed specifically at missed wake/degraded observation, not at making WAIT_FAILED recoverable.

## Preferred minimal design candidate (not implemented)
Replace the infinite wait with a finite, conservative observation interval (initial candidate: 100–250 ms). When the wait returns `WAIT_TIMEOUT`:
1. Reacquire `worker->lock`.
2. Do not resolve an operation; timeout carries no operation index.
3. If lifecycle is DEGRADED, release the lock and exit through the existing fail-closed path.
4. Otherwise release the lock and restart the loop, rebuilding the wait plan from the current registry snapshot.

This uses the already-defined timeout result and does not require a second signaling primitive that could fail in the same way. It bounds observation latency to approximately the timeout interval plus scheduling delay. It adds periodic idle wakeups (4–10 per second for 250–100 ms), so the interval should be chosen and measured rather than treated as free.

The top-of-loop degraded check must remain before promotion or any operation processing. A timeout must never be interpreted as completion, trigger `GetOverlappedResult`, release ownership, acknowledge a submitter request, or remove a retained item.

## Lock-order review
- The test-only quarantine callback currently nests Worker lock -> Adapter lock.
- Production `retain_envelope` takes Adapter lock, releases it, then takes Worker lock; it does not nest them.
- Other inspected adapter paths call Worker APIs without holding Adapter lock. Worker release callbacks execute after the Worker lock has been released.
- Submitter invokes its publish callback outside the Submitter lock; it reacquires Submitter lock only after publish returns.
- The read-only snapshot also takes Adapter, Worker, and Submitter locks sequentially, not simultaneously.
- No reverse nested Adapter -> Worker lock path was found in the inspected production call paths. This supports the test-only Worker -> Adapter order, but is not a formal whole-program proof; any future path that nests Adapter -> Worker would invalidate it.

## Safety limits
- Finite wait timeout only solves independent observation of sticky DEGRADED state after a missed wake. It does not repair the operation, complete the I/O, or recover the Worker.
- The transferred operation must be quarantined before the Worker can observe DEGRADED and exit. The existing deterministic test hook proves publication-before-observation under the Worker lock, but production quarantine remains unimplemented.
- A timeout must not be used to infer that an overlapped I/O is terminal. Completion still requires the original operation event and the existing terminal path.
- WAIT_FAILED, invalid wait sets, uncertain completion, handle-close failure, and ownership-invariant failures remain separate fail-closed cases.
- Do not add an auxiliary timer handle until there is a demonstrated need; the finite timeout on the existing wait is simpler and avoids another resource and wait-set entry.

## Recommendation / next validation
Prototype the finite-timeout branch in an isolated test-only derived build first. Test:
1. Inject the first wake SetEvent failure after a real overlapped operation is transferred.
2. Do not provide any second wake signal.
3. Verify the Worker exits DEGRADED within the documented bound.
4. Verify operation remains queued/quarantined, registry and ownership resources remain intact, no callback/ACK/release occurs, and the timeout did not claim completion.
5. Verify normal operation-event completion and shutdown still work with finite waits.
6. Add a negative control proving the timeout path does not collect an unsignaled operation.

Only after these pass should a production design change be considered. No production behavior change is authorized or claimed here.

## Environment and boundaries
- Working copy only: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`
- Source project and Production untouched.
- No commits, resets, TLC runs, builds, or tests in this investigation.
- Disk free at start: C 9,727,135,744 bytes; D 2,113,290,240 bytes.
