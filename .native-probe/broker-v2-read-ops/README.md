# Broker v2 relative-path and HANDLE-chain research

This directory contains test-only research components. Nothing here is connected to the
Raya Local MCP production handlers or installed as a persistent service.

## Lexical path gate

`relative-path.c` rejects rooted paths, alternate separators, empty components,
dot components, Win32-invalid characters, trailing dots/spaces, alternate data
stream syntax, and reserved DOS device names. It is a lexical check only.

## HANDLE-relative directory chain

`handle-chain.c` takes a borrowed, already-authorized root directory HANDLE and a
validated relative directory path. It opens each component individually using
`NtCreateFile` with the preceding directory HANDLE as `OBJECT_ATTRIBUTES.RootDirectory`.
Each open requests a directory, uses `OBJ_DONT_REPARSE` and
`FILE_OPEN_REPARSE_POINT`, and validates directory/reparse/delete-pending state
through the returned HANDLE. The chain retains every opened directory HANDLE until
the caller closes it. Intermediate handles are opened with read sharing only so a
concurrent rename/delete requiring incompatible access is denied while the chain
is retained.

## HANDLE-relative directory enumeration

`raya_enumerate_directory_page` enumerates one 64 KiB page from an already-open
directory HANDLE using `GetFileInformationByHandleEx(FileIdBothDirectoryInfo)`.
It validates each record's offset, alignment, filename length, supported leaf-name
policy, and nonnegative size metadata before returning any entries. It returns
bounded metadata and names only; it does not open or traverse listed directories.
Reparse entries are reported by attributes and must not be traversed without a
separate handle-relative open and same-HANDLE revalidation.

- Entry cap: 1..1024 per returned page. If the OS page contains more entries than
  requested, it fails explicitly with `ERROR_MORE_DATA`; the cursor may have
  advanced, so the caller must restart on the same handle with a larger cap.
- `ERROR_NO_MORE_FILES` becomes explicit `done=true`.
- `restart=TRUE` uses `FileIdBothDirectoryRestartInfo` to reset the same handle's
  cursor.
- The page must be zero-initialized before first use. Always call
  `raya_free_directory_page` after use. Serialize enumeration calls on one
  directory HANDLE because its cursor is shared.
- Enumeration is not a snapshot. On the current NTFS host, creating a new child
  succeeded while the directory HANDLE was open, even though renaming the
  directory itself was blocked. Concurrent create/delete/rename can therefore
  change which entries appear across pages; callers must not assume snapshot
  semantics or use returned names as authorization.
- `build-directory-enumeration-tests.cmd`: 30 base assertions (plus four when
  directory symbolic-link creation is permitted) over 1,200 files across multiple
  pages, restart, over-cap failure, empty-directory end, invalid handles, and
  directory rename pinning. Directory symbolic-link creation was explicitly
  skipped on this host because Windows returned ERROR_PRIVILEGE_NOT_HELD (1314);
  CI's symbolic-link-capable runner exercises reparse-entry reporting.

## Final regular-file open

`raya_open_single_link_regular_file` opens one validated path component relative
to a pinned parent directory HANDLE. It requests a non-directory open, does not
follow reparse points, and validates the resulting HANDLE for disk-file type,
non-directory/non-reparse status, non-delete-pending state, nonnegative bounded
size, and exactly one hard link. The returned HANDLE is the one intended for the
subsequent read; the caller owns and must close it.

The link count is an observation made on the opened file HANDLE. Windows permits
a hard link to be created after that check, even while this read HANDLE remains
open. The test demonstrates that the existing HANDLE continues to reference the
same file object and that a subsequent open rejects the now-multi-link file.
This helper therefore does not claim to prevent all future link-count changes or
provide a volume-wide hard-link prohibition. Whether that residual behavior meets
the final product policy remains an explicit security acceptance question.

The root HANDLE is borrowed, must already refer to an authorized directory, and is
validated again by the helper. The helper does not authorize a caller, choose a
workspace root, or provide a complete file-operation policy. It is not yet
integrated into Broker v2 or any application handler.

## I/O cancellation, deadlines and reaper experiments

- `build-io-lifecycle-model.cmd`: pure C state-machine tests (34 assertions) for
  synchronous/pending completion, cancellation races, late completion, exactly-once
  reaping, unresolved-operation retention, registry admission caps, and shutdown admission.
- `build-deadline-tests.cmd`: 20 deterministic assertions for one monotonic
  request deadline, per-frame budgets capped by the remaining overall time, zero
  timeout rejection, and overflow-safe deadline creation.
- `build-overlapped-pipe-lifetime-tests.cmd`: disposable local named-pipe experiment
  using real Windows overlapped ReadFile and CancelIoEx (23 assertions). It verifies
  pending-read cancellation reaches ERROR_OPERATION_ABORTED, normal completion returns
  the exact payload, late CancelIoEx after completion returns ERROR_NOT_FOUND, a
  cancel/completion race reaches one terminal outcome, and two timed-out pending reads
  are canceled/reaped using a shared 500 ms request deadline with 200 ms per-frame caps.
- `build-overlapped-pipe-reaper-tests.cmd`: four independent pending named-pipe
  reads backed by heap-allocated OVERLAPPED/buffer/event/pipe-handle records
  (42 assertions). The request side requests cancellation and returns without
  touching those resources. A single reaper thread observes all completions, validates
  terminal cancellation, closes each event and both pipe handles, and releases each
  record exactly once. If the reaper's five-second grace wait expires, it retains
  ownership and waits for actual completion rather than freeing pending resources.
- The real named-pipe tests use disposable in-process pipes only. They do not install
  a service, touch a workspace, integrate with Broker, or establish behavior for
  every Windows driver or every supported Windows release.
- The reaper prototype uses per-operation events and a bounded four-operation
  registry. This demonstrates a simple ownership path for the experiment, not a
  final throughput architecture decision. A production cap and whether IOCP is
  preferable remain to be determined.

## Bounded read protocol

- `read-frame.h/.c` implements the 16-byte RYA2 envelope with explicit
  little-endian field encoding; no packed-struct casts or untrusted pointer casts.
- The decoder rejects bad magic/version/type, truncated headers/payloads, trailing
  bytes, and payloads above 16 KiB.
- REQUEST payload: 8-byte fixed header (operation u16, rootId length u16, UTF-16
  path code-unit length u16, reserved u16=0), followed by a 1..64-byte ASCII rootId
  and relative UTF-16LE path. RootId syntax is [A-Za-z0-9_-]. READ_FILE requires
  a nonempty relative path; LIST_DIRECTORY may use an empty path to list the root.
  The parser validates UTF-16 surrogate pairs and delegates path syntax to the
  relative-path gate.
- RootId parsing does not bind the ID to configured ROOTS or authorize it. That
  registry and request-scoped HANDLE lifecycle remain separate acceptance gates.
- ERROR payload: numeric code u32, retryable u8, three reserved zero bytes. Codes
  are BAD_REQUEST, UNKNOWN_ROOT, NOT_FOUND, ACCESS_DENIED, UNSUPPORTED_OPERATION,
  RESOURCE_LIMIT, and INTERNAL. ERROR is a terminal sequence-zero frame and is
  acknowledged with the matching sequence-zero ACK; no raw OS error or path is sent.
- Response TX/RX implements RESPONSE_HEADER, sequential DATA, END, and one ACK per
  frame. ACKs echo the acknowledged sequence in both the ACK frame header and its
  fixed four-byte little-endian payload.
- Response bounds: 256 KiB body, 16 KiB chunk, no more than 16 DATA frames. All
  chunks must have the exact expected size, including the final short chunk.
- `build-read-frame-tests.cmd`: 57 pure C assertions for wire layout, malformed
  frames, response boundaries, ACK validation and response sequencing.
- `build-request-error-tests.cmd`: 44 assertions for REQUEST/ERROR payload
  contracts, rootId syntax, relative path and UTF-16 validation, reserved fields,
  frame sequencing, error-code bounds, and terminal ERROR acknowledgement.
- `build-message-pipe-frame-transport-tests.cmd`: 28 actual Windows message-mode
  pipe assertions. It transfers zero bytes, chunk-plus-one, and exactly 256 KiB
  through one-message-per-frame with an ACK for each frame; exercises REQUEST and
  terminal ERROR over the pipe; verifies ERROR_MORE_DATA on an undersized read,
  rejects a truncated frame, and fails closed on disconnect between frames.
- The message-mode transport itself is still a synchronous test harness. The
  request/per-frame deadline calculation is tested against real overlapped pipe
  reads, but is not yet wired into the message-frame transport/reaper.

## Test commands

Run from an x64 Native Tools Command Prompt:

- `build-and-test.cmd`: MSVC x64 lexical path tests (25 cases).
- `build-handle-chain-tests.cmd`: nested HANDLE-relative directory opens,
  traversal rejection, rename blocking, final-file open, size cap, directory
  rejection, hard-link detection, active-handle stability after link-count change,
  and directory-junction rejection.
- `build-directory-enumeration-tests.cmd`: HANDLE-relative multi-page directory
  enumeration (30 base assertions; four additional if symlink creation is permitted).
- `build-io-lifecycle-model.cmd`: synthetic lifecycle model (34 assertions).
- `build-deadline-tests.cmd`: request/per-frame deadline arithmetic (20 assertions).
- `build-overlapped-pipe-lifetime-tests.cmd`: actual named-pipe cancellation and
  deadline experiment (23 assertions).
- `build-overlapped-pipe-reaper-tests.cmd`: heap-owned operation/reaper
  experiment (42 assertions).
- `build-read-frame-tests.cmd`: bounded read-frame codec and response state
  machine (57 assertions).
- `build-request-error-tests.cmd`: REQUEST/ERROR payload contracts (44 assertions).
- `build-message-pipe-frame-transport-tests.cmd`: message-mode pipe framing
  experiment (28 assertions).

## Not yet established

This is not production acceptance. Remaining work includes wiring the deadline
budget into the actual asynchronous message-frame transport/reaper, disconnect
tests at every protocol phase, rootId-to-ROOTS binding, request-scoped HANDLE
ownership, directory enumeration consistency under concurrent mutation,
write/edit/move semantics, broader adversarial race testing, and a decision on
the residual hard-link mutation behavior. None of the native I/O or frame-codec
experiments is integrated into the Broker service or Raya Local MCP handlers.
