# Integrated Dedicated Submitter + Managed Worker Adapter
Date: 2026-11-08
Status: initial native vertical slice — experimental, not Production-ready

## Added files

- `integrated-submit-adapter-v2.h`
- `integrated-submit-adapter-v2.c`
- `integrated-submit-adapter-v2-native-test.c`
- `build-integrated-submit-adapter-v2-native-test.cmd`
- `build-integrated-submit-adapter-v2-compile.cmd`

## Implemented path

The adapter starts the Dedicated Submitter with its own issue/publish/not-issued
callbacks and borrows an already-started Managed Completion Worker.

For each accepted public submission it:
1. Allocates a stable envelope containing the Submitter request and managed
   operation.
2. Copies write payloads or allocates a private read buffer.
3. Creates a manual-reset completion event and duplicates the issuing pipe.
4. Initializes the managed operation and finalizer.
5. Acquires the Worker caller lease, Submitter admission, and exact Worker
   reservation before publishing to the Submitter queue.
6. Issues ReadFile/WriteFile only on the long-lived Submitter thread.
7. For accepted OS I/O, hands the exact operation to the Managed Worker.
8. The Worker collects using its exact issuing HANDLE and OVERLAPPED, closes
   resources through its finalizer, and commits release.
9. The release callback acknowledges the Submitter request. ACK can block while
   publication is still returning; the Submitter's ISSUING/ISSUED barrier
   resolves that race.
10. The user's completion callback runs after successful ACK, outside subsystem
    locks. Read data is valid only during the callback.

Definitely-not-accepted immediate Win32 failures use the caller-terminal
path: terminalize the exact reservation, close both owned handles, commit
release, unreserve, finish the caller lease, then invoke the callback.

The adapter has its own retained-envelope list. An uncertain handoff, failed
cleanup, failed ACK, or failed ownership accounting retains the envelope and
prevents adapter destruction. If the OS accepted I/O but Worker handoff failed,
the request is never misreported as NOT_ISSUED.

## Native acceptance

MSVC x64, `/W4 /WX`, disposable byte-mode named pipes. The test executes 40
cases per process, then the build script launches the executable 10 times:

- 10 pending read operations; producer thread exits before peer writes.
- 10 pre-buffered read operations.
- 10 peer-disconnect reads exercising the immediate non-pending failure path.
- 10 write operations; producer thread exits before completion and peer reads
  the exact payload.

Each process reports 1,020 assertions. Ten process runs produced
**10,200 passing assertions**. All cases verify callback exactly once, expected
status/byte counts or payload, zero Submitter queue/producers/active issue/
issuer leases after completion, clean Submitter and Worker shutdown/join,
zero retained envelopes, and successful destruction of both components.

## Regression after adapter integration

All passed after the adapter changes:
- Dedicated Submitter core: 18 assertions x25 runs = 450.
- Submitter publish/ACK race: 22 x50 = 1,100.
- Submitter backpressure: 29 x25 = 725.
- Submitter multi-producer: 137 x10 = 1,370.
- Managed Worker single-operation native: 35 x10 = 350.
- Managed Worker 70-operation multi-window: 987 x3 = 2,961.
- Legacy managed-pipe-I/O regression: 88 x10 = 880.

The adapter's own 10,200 assertions are additional to these regressions.

## Important implementation correction

The first live integration run exposed an accounting error in the adapter:
`raya_reaper_handoff` consumes the Worker caller-active lease itself. Calling
`raya_managed_worker_finish` after a successful handoff double-decremented that
lease and degraded the Worker. The adapter now marks that lease consumed by
successful handoff and does not finish it a second time. The corrected native
suite and all listed regressions pass.

## Explicit limits / outstanding acceptance

- The pre-buffered read case does not instrument the ReadFile return value, so
  it does not yet prove that Windows selected the synchronous immediate-success
  return path rather than a very fast pending completion.
- No 70+ operation integrated Submitter-to-Worker WaitPlan test yet.
- No integrated concurrent-producer/backpressure saturation test.
- No injected DuplicateHandle/CreateEvent/ReadFile/WriteFile/SetEvent/
  GetOverlappedResult/CloseHandle failures across the complete adapter.
- No combined Worker handoff failure-after-OS-acceptance test.
- No handle-count baseline or long soak.
- Cancellation and timeout are intentionally not exposed by this first API.
- The adapter borrows Submitter/Worker lifetimes; coordinator shutdown ordering
  remains an external responsibility.
- The retained-envelope list is diagnostic/permanent retention, not a recovery
  mechanism.

Production remains **NO-GO**. No service, protected root, tunnel, or
IranPriceScout changes were made.

## Follow-up ownership audit — worker admission rejection
- Source audit found a short-circuit cleanup in `raya_integrated_adapter_submit`: when Managed Worker admission was rejected, `if (!CloseHandle(pipe) || !CloseHandle(event))` could skip the second close after the first close failed. That path also bypassed the shared finalizer/owner-release state machine.
- Replaced the raw close pair with the existing caller-submission-failed -> resource finalizer -> finalizer commit -> owner release sequence. Each resource close is now attempted/tracked independently by the finalizer. Any uncertain close or ownership transition retains the envelope and prevents clean adapter destruction.
- Regression after this change: integrated adapter harness MSVC x64 `/W4 /WX`, 1,020 assertions/process ×10 = **10,200 PASS**.
- Integrated 70-operation harness rerun after the same source change: **809 PASS**, including all 70 callbacks and clean Submitter/Worker lifecycle.
- This is a targeted ownership-path repair, not yet failure-injection coverage of the adapter's Win32 API boundaries. CreateEvent/DuplicateHandle/ReadFile/WriteFile/close failures still need deterministic adapter-level injection; synchronous immediate-success ReadFile/WriteFile is still not directly instrumented.

## Adapter Win32 API seam and first failure/synchronous-completion matrix

### API seam
- `raya_integrated_adapter_api` and `raya_integrated_adapter_start_with_api()` were added. The original `raya_integrated_adapter_start()` remains source-compatible and installs the real Win32 wrapper table.
- Injected boundaries: `CreateEventW`, `DuplicateHandle`, `ReadFile`, `WriteFile`, immediate `GetLastError`, and adapter-owned `CloseHandle`/finalizer close. The API table is copied into the adapter; its context remains borrowed until adapter destruction.
- Finalizer closes for this adapter now go through the adapter's close callback rather than the Worker's shared close callback. This keeps the test seam scoped to adapter-owned handles and avoids changing the borrowed Worker's global close policy.
- `ReadFile`/`WriteFile` return values are preserved. `TRUE` is accepted as synchronous completion and still handed to the Managed Worker for exact HANDLE/OVERLAPPED collection; `FALSE` captures `GetLastError` immediately, and only `ERROR_IO_PENDING` is treated as accepted pending I/O.

### API native test
- New files: `integrated-submit-adapter-v2-api-test.c`, `build-integrated-submit-adapter-v2-api-test.cmd`.
- MSVC x64 `/W4 /WX`; 61 cases per process, 10 process runs; **5,480 assertions passed**.
- Injected `CreateEventW` failure: no duplicate, no callback, active envelope count returns to zero, clean teardown.
- Injected `DuplicateHandle` failure: the already-created event is closed through the injected close callback, no callback/retained envelope, clean teardown.
- Injected immediate non-pending `ReadFile` and `WriteFile` failures (`ERROR_BROKEN_PIPE`): each follows the NOT_ISSUED terminal cleanup path, invokes exactly one callback, closes both owned handles, leaves no retained envelope, and permits clean shutdown.
- Injected adapter finalizer `CloseHandle` failure: callback is suppressed; one failed owned handle remains uncertain; `HANDLE_CLOSE` is sticky; reservation/envelope remain retained; adapter destruction is rejected. This is an intentional process-exit cleanup case, not a clean shutdown pass.
- Synchronous immediate-success proof: with pre-buffered named-pipe data, the injected wrapper observed `ReadFile == TRUE` in all 100/100 cases (10 cases per process ×10 processes); callback and clean teardown passed.
- Synchronous immediate-write proof: the wrapper observed `WriteFile == TRUE` in all 100/100 cases (10 cases per process ×10 processes); the peer read the exact payload and clean teardown passed.

### Combined verification after API seam
- API matrix: 5,300 assertions.
- Existing integrated adapter regression: 10,200 assertions (1,020 x10).
- Integrated 70-operation / 7-producer / >63-event-window test: 809 assertions, one successful full run.
- Combined: **16,489 passing assertions** across these three suites.
- `git diff --check` and generated-artifact/process checks are recorded separately in the session verification.

### Remaining failure/acceptance gaps
- Adapter-level `CloseHandle` failure is now injected on the NOT_ISSUED finalizer path. The close is sticky/uncertain; callback remains suppressed; adapter and Worker retain the envelope; `HANDLE_CLOSE` is recorded; the reservation remains held while caller-active and Submitter issuer leases drain. Adapter destruction is rejected. This test intentionally leaves the uncertain Worker/OS resources retained until process exit; it does not claim clean shutdown.
- `GetOverlappedResult` occurs in Managed Worker, not the adapter API table; injected uncertain-completion behavior and the full Adapter→Worker handoff failure-after-OS-acceptance case remain outstanding.
- No adapter-level `SetEvent`/Submitter ACK failure matrix was added here; Submitter's independent injected failure suite remains separate.
- No handle-count soak, long-duration randomized stress, cancellation/timeout API, or production lifecycle integration.
- The immediate-success tests prove `TRUE` was returned by the real Win32 API in the tested scenarios; they do not replace broader OS-version/load coverage.

Production remains **NO-GO**.

## Managed Worker completion API seam and post-acceptance handoff failure — 2026-11-08

- Added `raya_managed_worker_start_with_completion_api()`. The callback and borrowed context are copied into the Worker before its thread is created; the existing `start()` and `start_with_factory()` paths remain source-compatible and use the real `GetOverlappedResult` wrapper. No mutable runtime setter or global process state was introduced.
- The Worker now obtains completion results through this per-instance callback. Default production behavior still calls Win32 `GetOverlappedResult(file, overlapped, transferred, wait)` unchanged.
- New native completion-failure test uses a real overlapped named-pipe read, waits for the exact operation event to signal, then injects `FALSE/ERROR_ACCESS_DENIED` from the completion API. Across 10 fresh processes: 33 assertions each (330 total). Verified `COMPLETION_UNCERTAIN`, owner `REAPER_UNCERTAIN`, integrated collect state `UNCERTAIN`, outstanding/reservation/registry retained, callback suppressed, join reports non-clean exit, and destroy is refused. Operation envelope and OS resources intentionally remain retained until process exit; this is not a clean-shutdown test.
- Added a deterministic Adapter test for failure of Worker handoff *after* the real `ReadFile` returned `ERROR_IO_PENDING`. The injected read wrapper marks the Worker degraded before Adapter publish. The Adapter now explicitly moves the caller-side owner to `CALLER_UNCERTAIN` and retains the operation; it never invokes NOT_ISSUED cleanup for an OS-accepted I/O. Across 10 API-matrix process runs, this adds 20 assertions per run (200 total): caller admission drains, reservation and issuer lease remain held, Adapter/Worker retain the envelope, callback is suppressed, and destruction is refused. The retained I/O/handles remain until process exit by design.
- Latest focused verification (MSVC x64 `/W4 /WX`): completion uncertainty test 330 assertions; Worker live-I/O regression 350; Adapter API matrix 5,680 (568 × 10, now 62 cases/process); Adapter native regression 10,200 (1,020 × 10); integrated 70-operation WaitPlan test 809 (single run). Combined: 17,369 passing assertions.
- These tests do not complete the remaining Submitter ACK/SetEvent failure matrix, cancellation/timeout integration, handle-count soak, long-duration randomized stress, full 28-group acceptance matrix, or production lifecycle integration. Production remains NO-GO.

## Submitter ACK signal failure after I/O resource release — 2026-11-08

- Corrected `raya_submitter_acknowledge()`: after the request transitions to `ACKED` and the issuer lease is retired, failure to signal either `changed_event` or `wake_event` now returns failure to the caller while marking the Submitter degraded. Previously the method returned success after `SetEvent` failure, which allowed Adapter to invoke the user callback and free the envelope despite the degraded Submitter.
- Added `raya_integrated_adapter_start_with_apis()` as a source-compatible extension for tests that need both Adapter I/O injection and Submitter Win32 API injection. Existing `start()`, `start_with_api()` continue to use the normal Submitter Win32 functions.
- Adapter's completion release callback already treats a failed acknowledge as an ownership failure. With the corrected return value, it now retains the envelope, suppresses the user callback, and refuses Adapter destruction even though the operation's pipe/event handles and Worker reservation have already been terminally released.
- Added two live named-pipe integration cases: inject `SetEvent(changed_event)` failure and inject `SetEvent(wake_event)` failure, each only after both operation handles have been closed by the Worker finalizer. Each case verifies: exactly two resource closes, ACK failure injected, no user callback, Adapter envelope retained, Worker retained envelope has owner/finalizer in RELEASED state, Submitter request is logically ACKED with issuer lease zero, Submitter lifecycle DEGRADED, queue/producers/active issue/issuer leases all zero, Adapter/Submitter/Worker do not report clean destroy/shutdown. The Submitter worker is observed exiting with an error; its join reports failure. No retry of ACK is attempted because the request is already terminal ACKED. The retained envelope and degraded component state are intentionally not destroyed.
- API matrix now has 64 cases and 632 assertions per process, 10/10 processes: 6,320 assertions. Existing Adapter native test: 10,200 assertions (10 runs); 70-operation integrated WaitPlan test: 809 assertions (one run).
- Regressions after the ACK change: Submitter Win32 failure matrix 108 assertions ×25; Submitter core 18 ×25; ACK race 22 ×50; backpressure 29 ×25; multiproducer 137 ×10; Worker live-I/O 35 ×10; completion uncertainty 33 ×10. All reported PASS. The Adapter native and 70-operation tests also passed after the change.
- This closes only the ACK SetEvent failure path. It does not complete the entire Submitter SetEvent/ResetEvent/wait failure matrix, cancellation/timeout integration, handle-count soak, randomized long-duration stress, full 28-group acceptance matrix, or production lifecycle integration. Production remains NO-GO.
- Final expanded ACK matrix: 64 cases, 632 assertions per process, 10/10 runs (6,320 assertions). Both `changed_event` and `wake_event` failure paths now additionally verify that the Worker thread itself exits, join reports its non-success exit code, and its thread handle is closed; the degraded Worker/Submitter and retained envelope remain non-destroyable.

## Submitter ResetEvent/WaitForSingleObject failures while ACK waits on ISSUING — 2026-11-08

- Extended `dedicated-io-submitter-v2-failure-test.c` with two deterministic races. A blocking issue callback signals that the request has entered `ISSUING` and holds the Submitter issue thread while the caller invokes `raya_submitter_acknowledge()`.
- Case 1 injects `ResetEvent(changed_event)` failure inside the ACK wait path. Case 2 allows ResetEvent but injects `WAIT_FAILED` from `WaitForSingleObject(changed_event, INFINITE)` inside the ACK wait path.
- In both cases ACK returns failure, request storage remains `ISSUING` during the failed wait, then the issue callback is released and reports `RAYA_SUBMITTER_ISSUE_ACCEPTED` through the test seam (this does not issue a real OS I/O). The Submitter transitions to `DEGRADED`; after publish it retains the request in `ISSUED` with `issuer_leases==1`, `active_issue==0`, and an empty queue. Shutdown is refused, the thread exits with non-success, join reports failure while closing the completed thread handle, and destroy is refused. The stack request remains alive until the test returns; no request storage is freed while the Submitter thread can still reference it.
- Dedicated Submitter Win32 failure matrix now reports 146 assertions per process across 15 fault scenarios, 25/25 fresh processes: 3,650 assertions. This includes the earlier idle-loop ResetEvent/WAIT_FAILED cases and the new ACK-wait-specific cases.
- Adapter API matrix remains 632 assertions per process, 10/10 runs; Adapter native regression 1,020 ×10; 70-operation integrated WaitPlan test 809; Submitter ACK race 22 ×50; core 18 ×25; backpressure 29 ×25; multi-producer 137 ×10; Worker live-I/O 35 ×10; completion-uncertainty 33 ×10. All reported PASS after the changes.
- Scope note: the new ResetEvent/WAIT_FAILED tests are deterministic Submitter ownership/lifecycle tests around the ACK wait path. The separate attempt to add an Adapter-level ResetEvent/Wait failure scenario exposed a timing-sensitive named-pipe completion race and was not included in the passing matrix. No claim is made that those two cases are now covered end-to-end through the Adapter. Production remains NO-GO.

## Submitter double-signal failure in degrade path — 2026-11-08

- Added `test_degrade_double_signal_failure` to `dedicated-io-submitter-v2-failure-test.c`.
- The test injects failure into all three SetEvent calls in the shutdown failure path: the initial wake signal, then both `changed_event` and `wake_event` notifications attempted by `degrade_locked`.
- It verifies both events remain unsignaled, lifecycle is sticky `DEGRADED`, all queue/producer/active/issuer counters remain zero, and the Submitter thread remains blocked rather than falsely exiting cleanly.
- A direct test-only Win32 `SetEvent` (outside the injected API) is then used solely to release the blocked test thread. The thread observes `DEGRADED` and exits with an error; join reports failure, and destroy is rejected. This prevents the test harness from leaking a live thread while preserving the assertion that production signaling failed.
- Submitter Win32 failure matrix: 160 assertions/run ×25 fresh processes = 4,000 assertions, PASS.
- Regression after this addition: Adapter API 632 ×10 = 6,320; Adapter native 1,020 ×10 = 10,200; Submitter ACK race 22 ×50 = 1,100; Submitter core 18 ×25 = 450; backpressure 29 ×25 = 725; multi-producer 137 ×10 = 1,370; Worker live-I/O 35 ×10 = 350; completion uncertainty 33 ×10 = 330. All PASS.
- Scope: the double-signal failure is exercised in the Submitter shutdown/degrade path. It does not establish end-to-end Adapter behavior for real I/O under simultaneous signal failures. `degrade_locked` still deliberately makes best-effort notifications and retains `DEGRADED`; a blocked thread may require external process/service teardown if both signals fail. Production remains NO-GO.

## Degraded Submitter quarantine and bounded join — 2026-11-08

- Extended the double-SetEvent-failure test with an explicit bounded join attempt while the Submitter thread remains blocked on its unsignaled wake event.
- `raya_submitter_join(..., 25)` returns false on timeout and preserves the exact thread HANDLE, wake-event HANDLE, changed-event HANDLE, and `joined==FALSE`. A zero-time OS wait confirms the thread is still running. Only after these retention assertions does the test issue a direct test-only SetEvent to release the thread; the thread observes DEGRADED and exits with non-success. `destroy` remains rejected.
- Added the public header contract: join timeout/WAIT_FAILED preserves the thread HANDLE and all object state; retry is allowed.
- Dedicated Submitter Win32 failure matrix: 164 assertions ×25 fresh processes = 4,100 assertions, PASS.
- Regression after the header/test change: Adapter API 632 ×10 = 6,320; Adapter native 1,020 ×10 = 10,200; Submitter ACK race 22 ×50 = 1,100; Submitter core 18 ×25 = 450; backpressure 29 ×25 = 725; multi-producer 137 ×10 = 1,370; Worker live-I/O 35 ×10 = 350; completion uncertainty 33 ×10 = 330. All PASS.
- Teardown conclusion: there is no safe in-process force-termination for a thread blocked after all notifications fail. Do not call `TerminateThread`, close its event/thread handles, delete its critical section, or free the containing Submitter storage while it may still execute. The safe response is quarantine and bounded join/retry. If the thread cannot be recovered, only termination of an isolated owning process can reclaim its address space/handles; this is an operational containment policy, not an implemented Submitter API. Production remains NO-GO.
