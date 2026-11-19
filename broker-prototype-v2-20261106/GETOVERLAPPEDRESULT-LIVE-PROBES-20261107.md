# GetOverlappedResult Completion Probes — 2026-11-07

Status: isolated live Named Pipe harness plus a conservative pure classifier. Neither is integrated into pipe-io-v2.c and neither constitutes Reaper acceptance.

## Documentation review

Microsoft's GetOverlappedResult documentation establishes:
- The handle argument must be the same handle used to initiate the overlapped operation.
- The OVERLAPPED pointer must be the one supplied to the initiating operation.
- With bWait=FALSE, if the operation is still pending, the function returns FALSE and GetLastError returns ERROR_IO_INCOMPLETE.
- The API documentation gives generic failure handling through GetLastError but does not provide a complete operation-independent allowlist of errors that prove terminal completion.

The Named Pipe Server Using Overlapped I/O sample confirms that after an event indicates completion, the caller collects the pending operation's result with GetOverlappedResult. An event signal alone is not the result collection step.

Sources:
- https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getoverlappedresult
- https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-server-using-overlapped-i-o

## Conservative classifier

The previous classifier incorrectly promoted several Win32 error values to TERMINAL_IO_ERROR without a documented, operation-independent guarantee. It has been narrowed to:
- API success -> TERMINAL_SUCCESS.
- FALSE + ERROR_IO_INCOMPLETE -> INCOMPLETE.
- Every other failure -> UNCERTAIN.

The generic classifier never frees resources or changes ownership. A production owner must independently collect a successful result or otherwise satisfy a separately reviewed terminal-completion contract before cleanup. In particular, ERROR_OPERATION_ABORTED and disconnect-like errors are not promoted by this generic helper merely because their names sound terminal.

A separate context-aware helper was added after correlating the live cancellation probe with Microsoft's ReadFile and named-pipe overlapped-I/O contracts. `pipe-completion-context-v2.c` accepts `ERROR_OPERATION_ABORTED` as a terminal I/O result only when the caller supplies both `event_signaled=TRUE` for that operation's dedicated event and `exact_issuing_pair=TRUE` for the exact handle/OVERLAPPED pair. It rejects the same error when either condition is absent. The helper cannot independently inspect or prove these conditions; it is only a narrow classifier at a call site that has already established them. Other failed results remain uncertain, except `ERROR_IO_INCOMPLETE`, which remains incomplete. Ten assertions passed, plus 10/10 repeated process runs under MSVC `/W4 /WX`. The live cancellation scenario now calls the context-aware helper against the actual observed event/result sequence and asserts all three decisions: exact signaled operation + exact issuing pair + `ERROR_OPERATION_ABORTED` is terminal; the same error without event evidence is uncertain; and the same error without exact issuing-pair evidence is uncertain. The combined live harness rebuilt under MSVC `/W4 /WX` and passed its initial run plus 10/10 additional process runs. This is still an isolated named-pipe probe, not runtime execution of `pipe-io-v2.c` or a Reaper.

Additional Microsoft references for this narrow contract:
- https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile (cancelled I/O completes with ERROR_OPERATION_ABORTED; event signals when the read completes)
- https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-server-using-overlapped-i-o (pending named-pipe operations signal their event on completion)

Files:
- completion-classify-v2.h
- completion-classify-v2.c
- completion-classify-v2-test.c
- build-getoverlappedresult-probes.cmd

10 classifier assertions passed; 10/10 additional process runs passed.

## Live Named Pipe runtime probes

getoverlappedresult-live-probe-test.c creates disposable byte-type local named pipes and exercises actual overlapped operations:

1. Pending read: immediate GetOverlappedResult(FALSE) returns ERROR_IO_INCOMPLETE, the event remains unsignaled, peer data completes the operation, event signals, and GetOverlappedResult(FALSE) succeeds with the exact payload.
2. Accepted cancellation: real pending read, native CancelIoEx accepted, event signals, and GetOverlappedResult(FALSE) returns ERROR_OPERATION_ABORTED. This is evidence of a collected terminal result for this specific live operation; it does not justify a generic error allowlist in the absence of the event/result collection.
3. Wrong-handle probe while the operation is pending: on this Windows host, GetOverlappedResult(INVALID_HANDLE_VALUE, &ov, ..., FALSE) returned ERROR_IO_INCOMPLETE while the real operation remained pending. The operation was then completed through the peer and successfully collected using the issuing handle. This demonstrates that a failed call must not be mistaken for proof that the underlying operation ended; it does not characterize every invalid-handle timing or OS version.

Build: MSVC x64 /W4 /WX, exit code 0, no stderr. All three live scenarios passed in the initial run and 10/10 additional separate process runs. The classifier suite also passed 10/10 additional runs. Executable and object files were removed and verified absent.

## Limitations

- This is a disposable harness, not runtime execution of pipe-io-v2.c.
- It does not inject failures into the actual GetOverlappedResult call site inside pipe-io-v2.c.
- It does not establish a complete terminal error allowlist across Windows versions or all pipe states.
- It does not test event-signal/result inconsistency, cancellation-drain expiry, Reaper handoff, IOCP behavior, or handle-cleanup failure.
- The wrong-handle probe observed ERROR_IO_INCOMPLETE on this host while pending; do not generalize that result to every handle misuse.
- No production source or service was changed.

The static blocker remains: pipe-io-v2.c still frees pending operation resources after any failed GetOverlappedResult in both the normal path and the cancellation-drain path. Keep Pipe I/O compile-only and Production NO-GO until the managed ownership design is implemented and its full acceptance matrix passes.
