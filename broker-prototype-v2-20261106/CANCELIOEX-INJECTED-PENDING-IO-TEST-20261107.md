# CancelIoEx Injected Failure with Real Pending I/O — 2026-11-07

Status: isolated runtime contract test. It uses a real Windows overlapped named-pipe read, but injects the CancelIoEx failure through a test callback. It is not a test of a native CancelIoEx failure against a pending operation, not a test of pipe-io-v2.c, and not Reaper acceptance.

## Test sequence

1. Create a disposable local byte-type named pipe with an overlapped server endpoint and connect a synchronous client.
2. Duplicate the server handle and submit a real overlapped ReadFile into operation-owned storage.
3. Require FALSE + ERROR_IO_PENDING.
4. Invoke an injected CancelIoEx-compatible callback. It deliberately does not call the Windows API; it returns FALSE and sets ERROR_ACCESS_DENIED.
5. Capture GetLastError immediately and require ERROR_ACCESS_DENIED.
6. Probe the real operation event with a zero-time wait and require it remains unsignaled. The failed cancellation request has not completed or cancelled the read.
7. Perform a bounded 25 ms synchronous drain wait and require WAIT_TIMEOUT while the operation remains pending.
8. Transfer the operation pointer to an isolated completion thread. The caller sets its pointer to NULL and does not access operation fields/resources afterward.
8. The peer writes DATA. The completion thread waits for the real event, collects the real terminal result using GetOverlappedResult(FALSE), validates byte count and payload, then closes the duplicated pipe handle/event and frees the operation.

## Build and results

- Source: cancelioex-injected-pending-operation-test.c
- Build: build-cancelioex-injected-pending-operation-test.cmd
- Compiler: Visual Studio 2026 Build Tools 18.7.3, x64, MSVC /W4 /WX.
- Build: PASS, exit code 0, no compiler warnings.
- Runtime: 10 separate process executions, all PASS, no stderr.
- Each execution observed a real pending overlapped read, the injected ERROR_ACCESS_DENIED result, an unsignaled operation event immediately afterward, subsequent peer completion, successful GetOverlappedResult collection, payload validation, and worker-owned cleanup.
- Executable and object artifacts were removed after the repeated runs and verified absent.

## What this establishes

This provides stronger evidence than a pure state model: the operation is genuinely pending in the Windows I/O subsystem while the injected cancellation wrapper reports a non-ERROR_NOT_FOUND failure. The operation remains outstanding, and a separate owner later collects its actual terminal completion. It validates the intended rule that a non-ERROR_NOT_FOUND CancelIoEx failure must not be treated as completion or permission to free resources.

## Limitations

- ERROR_ACCESS_DENIED is injected by a callback; native CancelIoEx is not called in this case. This does not prove Windows will return this error for a valid handle/OVERLAPPED pair with an operation pending.
- The completion thread is a disposable test owner, not the lifecycle-managed single-worker Reaper. Queue locking, admission leases, wake signaling, bounded drain/handoff races, shutdown, and multi-operation fairness are not tested.
- The harness does not invoke pipe-io-v2.c. It therefore does not close the existing source-level blocker where GetOverlappedResult(FALSE) failures can reach cleanup without classifying ERROR_IO_INCOMPLETE.
- No Production or transport integration acceptance is implied.

## Decision

The injected non-ERROR_NOT_FOUND cancellation-failure contract now has a real-pending-I/O isolated harness with 10/10 repeated process runs. Native CancelIoEx failure behavior and implementation-level/Reaper acceptance remain open. Broker Production status remains NO-GO.
