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
- `build-directory-enumeration-tests.cmd`: 30 base assertions (plus four when`r`n  directory symbolic-link creation is permitted) over 1,200 files across
  multiple pages, no duplicate/omitted names in a stable fixture, restart,
  over-cap failure, empty-directory end, invalid handles, and directory rename
  pinning. Directory symbolic-link creation was explicitly skipped on this host
  because Windows returned ERROR_PRIVILEGE_NOT_HELD (1314); reparse-entry
  reporting is therefore not claimed as runtime-verified by this test.

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

## I/O cancellation and reaper experiments

- `build-io-lifecycle-model.cmd`: pure C state-machine tests (34 assertions) for
  synchronous/pending completion, cancellation races, late completion, exactly-once
  reaping, unresolved-operation retention, registry admission caps, and shutdown admission.
- `build-overlapped-pipe-lifetime-tests.cmd`: disposable local named-pipe experiment
  using real Windows overlapped ReadFile and CancelIoEx (17 assertions). It verifies
  pending-read cancellation reaches ERROR_OPERATION_ABORTED, normal completion returns
  the exact payload, late CancelIoEx after completion returns ERROR_NOT_FOUND, and a
  cancel/completion race reaches one terminal outcome.
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

## Bounded read frame codec and transport experiment

- `read-frame.h/.c` implements the 16-byte RYA2 envelope with explicit
  little-endian field encoding; no packed-struct casts or untrusted pointer casts.
- The decoder rejects bad magic/version/type, truncated headers/payloads, trailing
  bytes, and payloads above 16 KiB.
- The response TX/RX state machines implement RESPONSE_HEADER, sequential DATA,
  END, and one ACK per frame. ACKs echo the acknowledged sequence in both the ACK
  frame header and its fixed four-byte little-endian payload.
- The response contract enforces a 256 KiB body cap, 16 KiB maximum chunk, and no
  more than 16 DATA frames. All DATA chunks must have the exact expected size,
  including the final short chunk.
- `build-read-frame-tests.cmd`: 57 pure C assertions covering wire layout,
  malformed frames, boundaries (zero, one, one chunk, chunk plus one, exact 256 KiB),
  ACK validation, sequencing, duplicate/stale/future ACKs, invalid ordering, and
  terminal END behavior.
- `build-message-pipe-frame-transport-tests.cmd`: 12 actual Windows message-mode
  pipe assertions. It transfers zero bytes, chunk-plus-one, and exactly 256 KiB
  through one-message-per-frame with an ACK for each frame; it verifies
  ERROR_MORE_DATA on a deliberately undersized read, rejects a truncated frame,
  and fails closed on disconnect between frames.
- The transport experiment uses synchronous test I/O and does not yet implement
  bounded per-frame ACK timeouts, an overall request deadline, or full disconnect
  coverage at every protocol phase. REQUEST payload schema and ERROR semantics
  are also not implemented. Production transport must treat ERROR_MORE_DATA as
  malformed/truncated and fail closed.

## Test commands

Run from an x64 Native Tools Command Prompt:

- `build-and-test.cmd`: MSVC x64 lexical path tests (25 cases).
- `build-handle-chain-tests.cmd`: nested HANDLE-relative directory opens,
  traversal rejection, rename blocking, final-file open, size cap, directory
  rejection, hard-link detection, active-handle stability after link-count change,
  and directory-junction rejection.
- `build-directory-enumeration-tests.cmd`: HANDLE-relative multi-page directory
  enumeration (30 assertions).
- `build-io-lifecycle-model.cmd`: synthetic lifecycle model (34 assertions).
- `build-overlapped-pipe-lifetime-tests.cmd`: actual named-pipe cancellation
  experiment (17 assertions).
- `build-overlapped-pipe-reaper-tests.cmd`: heap-owned operation/reaper
  experiment (42 assertions).
- `build-read-frame-tests.cmd`: bounded read-frame codec and sequence state
  machine (57 assertions).
- `build-message-pipe-frame-transport-tests.cmd`: message-mode pipe framing
  experiment (12 assertions).

## Not yet established

This is not production acceptance. Additional work is required for bounded transport
deadlines, REQUEST/ERROR contract, directory enumeration consistency under
concurrent mutation, resource limits, broader adversarial race testing across
supported Windows versions, and a decision on the residual hard-link mutation
behavior described above. None of the native I/O or frame-codec experiments is
integrated into the Broker service or Raya Local MCP handlers.
