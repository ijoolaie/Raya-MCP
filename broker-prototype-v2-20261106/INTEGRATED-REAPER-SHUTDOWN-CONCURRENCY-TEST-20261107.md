# Integrated Reaper Concurrent Shutdown Admission Test — 2026-11-07

## Harness
Files:
- `reaper-integrated-shutdown-concurrent-test.c`
- `build-reaper-integrated-shutdown-concurrent-test.cmd`

A caller acquires an admission lease and operation-bound reservation before shutdown. Two real threads are then released concurrently after the main thread moves the Core to STOPPING:
- the pre-admitted caller attempts the ownership handoff and signals the operation event;
- a late caller attempts a new admission.

The main test thread then promotes, claims and terminally collects the pre-admitted operation under the shared lock.

## Assertions
- Pre-admitted operation can hand off after STOPPING.
- New admission after STOPPING is rejected.
- Handoff and completion event are observed.
- Registry, outstanding count, reservation and caller lease all drain.
- Clean exit and STOPPED transition succeed.

## Results
- MSVC x64 `/W4 /WX`: PASS.
- 25 independent process runs.
- 22 assertions per run; 0 failures.
- 550 assertions total.
- All threads and event handles closed; critical section deleted.

## Limits
This is a focused two-caller race, not a high-contention shutdown stress test. It does not test cancellation, a real overlapped I/O operation, or injected Win32 failures. It does not solve abandoned pre-handoff reservation cancellation.

No changes to `pipe-io-v2.c`, Production transport, service provisioning, tunnel, or IranPriceScout. Production remains **NO-GO**. No commit/reset.
