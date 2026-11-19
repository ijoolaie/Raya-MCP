# Pipe I/O Submission Semantics — Preliminary Runtime Evidence (2026-11-07)

Status: preliminary isolated test evidence only. This is not Reaper acceptance, transport acceptance, or Production approval.

## Purpose

Validate the documented named-pipe split for overlapped ReadFile/WriteFile:
- TRUE: consume the immediate byte-count output from the initiating call.
- FALSE + ERROR_IO_PENDING: ignore the immediate output and collect the result after the event is signaled with GetOverlappedResult.
- FALSE + another error: classify as submission failure; do not wait for or enqueue a pending operation.

The test uses a disposable local byte-type named pipe created in the test process. It does not create a Windows service, protected root, or persistent named-pipe endpoint.

## Host and build

- OS: Microsoft Windows 11 Pro, version 10.0.26300, build 26300.
- Compiler: Visual Studio 2026 Build Tools 18.7.3, x64.
- Compile flags: MSVC C mode, /W4 /WX.
- Build entrypoint: build-pipe-io-submit-semantics-test.cmd.
- Source: pipe-io-submit-semantics-test.c.
- The test creates a local byte-type pipe with FILE_FLAG_OVERLAPPED on the server endpoint. Each operation uses a unique manual-reset event and sets the low-order bit in OVERLAPPED.hEvent; the raw event handle is retained for waiting and closing.

## Covered cases

1. Immediate ReadFile: data is primed from the client first; ReadFile must return TRUE, the immediate byte count must equal the expected length, and the payload must match.
2. Immediate WriteFile: a short write to an empty pipe must return TRUE; the immediate byte count and bytes read by the client must match.
3. Pending ReadFile: ReadFile is issued before client data exists; it must return FALSE with ERROR_IO_PENDING. After the client writes, the event must signal and GetOverlappedResult must return the expected count and payload.
4. Immediate submission failure: ReadFile with INVALID_HANDLE_VALUE must return FALSE with ERROR_INVALID_HANDLE; the operation event must remain nonsignaled.
5. CancelIoEx accepted: issue a pending read, cancel using the exact issuing handle and OVERLAPPED, require the event, then collect ERROR_OPERATION_ABORTED with GetOverlappedResult.
6. Completion race / ERROR_NOT_FOUND: complete a pending read and wait for its event before calling CancelIoEx; require ERROR_NOT_FOUND, then collect the successful result and verify payload.
7. Non-ERROR_NOT_FOUND API failure classification: CancelIoEx with INVALID_HANDLE_VALUE must return ERROR_INVALID_HANDLE. This validates error capture/classification only; it does not simulate a non-ERROR_NOT_FOUND failure against a still-pending operation.
8. Pending WriteFile: submit a 1 MiB write against the 4 KiB outbound pipe buffer before the peer reads; require FALSE + ERROR_IO_PENDING, drain and byte-compare the complete payload at the peer, then require the event and GetOverlappedResult to report the full byte count. An immediate completion is INCONCLUSIVE, not PASS.

The harness returns a nonzero exit code if either immediate-success branch is not observed. It does not silently classify an asynchronous fallback as a pass.

## Results

- Initial compile: PASS, exit code 0, no compiler warnings under /W4 /WX.
- Initial pre-cancellation run: PASS, the then-existing four cases.
- Repeated run: 10 separate harness process executions, all exit code 0.
- Repeated after adding cancellation cases: 10 separate harness process executions, all exit code 0, no inconclusive runs.
- Across those repeated runs: 10 immediate ReadFile successes, 10 immediate WriteFile successes, 10 pending WriteFile completions, 10 pending ReadFile completions, 10 accepted pending-read cancellations collected as ERROR_OPERATION_ABORTED, 10 completion-wins CancelIoEx/ERROR_NOT_FOUND races followed by successful result collection, 10 invalid-handle CancelIoEx failures classified as ERROR_INVALID_HANDLE, and 10 immediate non-pending submission failures.
- Pending WriteFile was deterministically induced on this host by issuing a 1 MiB write before peer reads against the test pipe's 4 KiB outbound buffer; all 1 MiB were drained and byte-compared before collecting the overlapped result.
- No stderr was emitted.

## Limitations — not yet covered

- Pending WriteFile is covered only by this isolated harness and this host/OS build; it is not a runtime test of `pipe-io-v2.c`.
- The harness tests one accepted cancellation and one completion-wins ERROR_NOT_FOUND race. It separately classifies ERROR_INVALID_HANDLE from CancelIoEx on INVALID_HANDLE_VALUE, but this is not a non-ERROR_NOT_FOUND failure against a real pending operation. It does not test cancellation drain expiry, handoff to a Reaper, or the implementation's cancellation path.
- The test does not associate the pipe with an I/O completion port; low-bit hEvent use is exercised, but completion-port packet suppression is not yet proven.
- Invalid-handle submission failure is one representative immediate failure only; broader error injection remains required.
- Only this Windows 11 build was tested. No cross-version support claim is made.
- This test does not test the existing pipe-io-v2.c implementation; it validates the proposed byte-count calling convention in an isolated harness.

## Decision

The Microsoft named-pipe-specific immediate byte-count strategy, pending WriteFile completion path, and two CancelIoEx outcomes (accepted cancellation and completion-wins ERROR_NOT_FOUND) have preliminary isolated-harness support on the current host. On 2026-11-07, `pipe-io-v2.c` was updated to use a non-NULL byte-count pointer and consume it only on TRUE, while retaining `GetOverlappedResult` exclusively for FALSE + ERROR_IO_PENDING. The modified source compiled under MSVC /W4 /WX as part of `build-protocol-v2-tests.cmd`; parser (18), frame (34), and session (33) tests passed. The 28-group Reaper acceptance matrix remains open; this result closes only the initial investigation for the immediate ReadFile/WriteFile byte-count branch on the tested OS build.

Broker Production status remains NO-GO.
