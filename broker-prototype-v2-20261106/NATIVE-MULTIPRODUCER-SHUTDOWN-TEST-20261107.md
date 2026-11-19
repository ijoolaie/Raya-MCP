# Native multi-producer admission, handoff, and shutdown test — 2026-11-07

## Result

- MSVC x64 build: PASS with `/W4 /WX`.
- Independent process runs: 10/10 PASS.
- Assertions: 241 per run; 2,410 total; zero failures.
- Real named-pipe overlapped reads: 70/70 submitted pending and collected successfully per run.
- Producers: four concurrent producer threads.
- Worker: one independent worker thread.
- WaitPlan builds: 70–85 per run; rotation observations: 62–82.
- Final registry, outstanding, queued, caller-active, and reserved-slot counts: all zero.
- Lifecycle reached STOPPING while the 70 admission leases and reservations were outstanding, then reached STOPPED only after all work was collected and the integrated exit predicate passed.

## Scenario

1. Create 70 independent byte-type named-pipe server/client pairs, each with its own OVERLAPPED, event, byte buffer, and operation record.
2. Four producer threads concurrently acquire admission leases and reserve their assigned operations while the lifecycle is ACCEPTING. Each producer then parks at a gate.
3. Start the worker and enter STOPPING while all 70 caller leases and reservations remain outstanding.
4. Verify a new admission is rejected and the integrated clean-exit predicate remains false.
5. Release all four producers. Each submits a real overlapped ReadFile, hands the operation to the worker under the shared ownership lock, signals the worker wake event under that lock, and writes one byte from its paired client.
6. The worker promotes queued operations into the Registry, builds bounded rotating WaitPlans, resolves the exact operation/event pair, claims it under the shared lock, and calls GetOverlappedResult with the exact issuing server HANDLE and OVERLAPPED.
7. Only successful terminal completion with the expected one-byte payload permits Integrated collection. The worker exits only when STOPPING and all caller, queue, outstanding, registry, and reservation predicates are drained.
8. The harness verifies all final counters and marks the lifecycle STOPPED.

## Safety properties exercised

- Admission and reservation occur concurrently from four producers under the required shared lock.
- Shutdown rejects new admission but does not invalidate already admitted/reserved callers' subsequent handoff.
- The worker does not exit while caller leases or outstanding operations remain.
- Each completion is collected against its exact operation, registered event, issuing pipe handle, and OVERLAPPED.
- An uncertain completion is retained as DEGRADED rather than treated as terminal.
- Failure cleanup is fail-closed: if threads do not stop or all I/O is not collected, the harness retains pipe/event resources until test-process teardown rather than freeing potentially kernel-visible state.

## Limits

- This is an isolated test harness, not a lifecycle-managed production Reaper.
- It uses the current Integrated Core/Registry/WaitPlan APIs and a test worker; it does not link or runtime-test `pipe-io-v2.c`.
- Four producers contend for the shared lock, but the APIs themselves remain caller-serialized as specified; this does not establish lock fairness or hard latency bounds.
- It does not cover cancellation, disconnect, uncertain completion recovery, invalid wait handles, wake-event creation/recovery failures, CloseHandle failure, IOCP behavior, or all 28 required acceptance groups.
- The successful STOPPED result applies only to this disposable test process and is not Production acceptance.

Production remains **NO-GO**. No service, protected root, tunnel, Production handler, or IranPriceScout resource was changed.

## Degradation-cause classification update

The harness now assigns explicit causes at observed failure sites: failed handoff and promotion/Registry accounting mismatches use OWNERSHIP_INVARIANT; failed wake SetEvent uses WAKE_SIGNAL; ResetEvent and WaitPlan construction failures use WAITSET_INVALID; native WaitForMultipleObjects failure uses WAIT_FAILED; uncertain GetOverlappedResult/collection remains COMPLETION_UNCERTAIN through Integrated collection. The successful path asserts degradation_causes remains zero. No injected failure-path assertions were added here; the dedicated native cause harnesses cover observed WAIT_FAILED and WAKE_SIGNAL transitions. The successful-path regression was rebuilt with MSVC x64 /W4 /WX and passed 10/10 (241 assertions/run; 2,410 total), with 70/70 handoffs and completions each run.
