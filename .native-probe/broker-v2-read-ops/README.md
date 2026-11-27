# Broker v2 relative-path and HANDLE-chain research

This directory contains test-only research components. Nothing here is connected to the
Raya Local MCP production handlers or installed as a persistent service.

## Lexical path gate

`relative-path.c` rejects rooted paths, alternate separators, empty components,
dot components, Win32-invalid characters, trailing dots/spaces, alternate data
stream syntax, and reserved DOS device names. It is a lexical check only.

## Configured root registry and request-scoped HANDLEs

`root-registry.c/.h` is a test-only configured-root binding layer:

- Service configuration supplies a fixed list of 1..8 `rootId -> absolute path`
  pairs. Request data supplies only the rootId and a validated relative path; it
  never supplies an absolute filesystem path.
- Root IDs are syntax-checked and duplicates rejected. Configured paths must be
  absolute; each root is opened once with `FILE_FLAG_OPEN_REPARSE_POINT` and
  checked through the returned HANDLE for disk directory type, non-reparse and
  non-delete-pending state. The root HANDLE is retained until registry shutdown.
- A READ_FILE request resolves its rootId, opens parent components relative to the
  pinned root, opens the final regular file relative to the pinned parent, and
  transfers ownership of the final file HANDLE to the request context.
- GET_FILE_INFO, READ_FILE_CHUNK and READ_FILE_TAIL use the same final-file
  HANDLE path. Chunk reads use a checked 64-bit offset and exact bounded length;
  tail reads return up to the requested final bytes; metadata is read from the
  same validated HANDLE. NtReadFile uses an explicit byte offset, avoiding a
  mutable shared file pointer; per-context I/O and close are serialized.- A LIST_DIRECTORY request resolves its rootId and pins the requested directory
  through the HANDLE chain; an empty relative path binds to the configured root.
- Active request contexts hold a registry reference. Shutdown returns ERROR_BUSY
  until all contexts close. A context closes its file and directory-chain HANDLEs
  before releasing the registry reference.
- `build-root-registry-context-tests.cmd`: 83 assertions for two-root binding,
  read/list dispatch, root-level and nested file opens, GET_FILE_INFO, chunk/tail reads, metadata, invalid ranges, unknown root rejection,
  invalid paths, size cap, directory-vs-file rejection, WORKSPACE_ROOTS rootId-only enumeration, workspace_list per-root isolation/truncation, context close/double-close,
  shutdown while active, root namespace pinning, and atomic invalid-config rejection.
  Configured-root symbolic-link creation is explicitly skipped locally when
  ERROR_PRIVILEGE_NOT_HELD (1314); CI exercises this rejection.

This module does not validate root owner/DACL policy, authenticate the caller,
load production configuration, enforce a service SID, or integrate with Node
handlers. Configured paths are trusted service configuration and must not come
from request payloads. It is not Production acceptance.

## Phase 1 operation acceptance matrix

See docs/PHASE1-FILESYSTEM-OPERATION-ACCEPTANCE-MATRIX.md for the explicit 15-operation production acceptance matrix, current research evidence and per-operation remaining gates.

## Read-only operation contracts

See read-only-operation-contracts.md for the 20-byte REQUEST schema and the current READ_FILE, GET_FILE_INFO, READ_FILE_CHUNK, READ_FILE_TAIL and LIST_DIRECTORY research contracts, including their limits and non-acceptance boundaries.

## Workspace root discovery

The request protocol defines WORKSPACE_ROOTS as a rootless operation: its
REQUEST contains neither rootId nor relative path. The test-only registry returns
a bounded little-endian payload containing a 16-bit root count followed by
one-byte ASCII rootId lengths and rootId bytes. Only stable IDs are exposed;
configured absolute filesystem paths are never serialized. Unknown or closed
registries fail closed. The request-context opener intentionally rejects this
metadata-only operation because it does not open a filesystem object.

- build-root-registry-context-tests.cmd: 83 assertions, including root list
  encoding, stable ID order, absence of absolute paths, undersized output buffer,
  and rejection after registry shutdown.
- build-request-error-tests.cmd: 69 assertions, including rootless
  WORKSPACE_ROOTS request round-trip and rejection of caller-supplied rootId/path.

This is only the native protocol/registry seam. No UI, user-facing root picker,
Node handler, named-pipe dispatch or production workspace configuration is wired.
## Workspace listing across configured roots

raya_root_registry_list_roots is a test-only adapter for the no-path
workspace_list behavior. It snapshots the configured root IDs and pinned root
HANDLE values under the registry lock, increments the active registry reference,
then opens a separate identity-verified directory HANDLE view for each root.
Each root is enumerated independently; one root's failure is mapped to a
bounded RAYA_ERROR category and does not suppress the other roots.

- Returns stable root IDs and relative entry names only; never absolute paths.
- Each root returns at most the requested 1..64 entries from its first OS page.
- A second page probe determines whether more entries exist; the probe page is
  discarded and the result marks truncated=true. This is a bounded first-page
  summary, not a continuation/session implementation.
- The active registry reference prevents shutdown while all roots are processed.
- raya_workspace_list_free releases every page.
- The 83-assertion root-registry suite covers two-root listing, separate results,
  truncation, per-root error isolation, cleanup and active-reference release.

This is not production workspace_list integration: no framed request dispatcher,
client authentication, Node handler, UI or production ROOTS loading is connected.
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
- Each root-level LIST_DIRECTORY context owns its own verified root HANDLE view; nested contexts already own separate component HANDLEs. The page must be zero-initialized before first use. Always call
  `raya_free_directory_page` after use. Serialize enumeration calls on one
  directory HANDLE because its cursor is shared.
- Enumeration is not a snapshot. On the current NTFS host, creating a new child
  succeeded while the directory HANDLE was open, even though renaming the
  directory itself was blocked. Concurrent create/delete/rename can therefore
  change which entries appear across pages; callers must not assume snapshot
  semantics or use returned names as authorization.
- `build-directory-enumeration-tests.cmd`: 39 assertions (plus four when
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
- REQUEST payload: 20-byte fixed header (operation u16, rootId length u16, UTF-16
  path code-unit length u16, reserved u16=0, offset u64 LE, length u32 LE), followed by a 1..64-byte ASCII rootId
  and relative UTF-16LE path. RootId syntax is [A-Za-z0-9_-]. All file operations require
  a nonempty relative path; LIST_DIRECTORY may use an empty path to list the root. READ_FILE and GET_FILE_INFO require zero offset/length; READ_FILE_CHUNK requires a nonzero bounded length and checked 64-bit offset; READ_FILE_TAIL requires a nonzero bounded length and zero offset.
  The parser validates UTF-16 surrogate pairs and delegates path syntax to the
  relative-path gate.
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
- `build-request-error-tests.cmd`: 69 assertions for REQUEST/ERROR payload
  contracts, rootId syntax, relative path and UTF-16 validation, reserved fields,
  frame sequencing, error-code bounds, and terminal ERROR acknowledgement.
- build-message-pipe-frame-transport-tests.cmd: 109 actual Windows message-mode
  pipe assertions. It transfers zero bytes, chunk-plus-one, and exactly 256 KiB
  through one-message-per-frame with an ACK for each frame; exercises REQUEST and
  terminal ERROR over the pipe; and runs an end-to-end READ_FILE request through
  configured-root resolution, request-scoped HANDLE acquisition, file read,
  RESPONSE_HEADER/DATA/END frames, and an ACK for each response frame. It also
  verifies ERROR_MORE_DATA on an undersized read, rejects a truncated frame, and
  fails closed on disconnect between frames.
- The message-mode transport itself is still a synchronous test harness. The
  request/per-frame deadline calculation is tested against real overlapped reads,
  but is not yet wired into the message-frame transport/reaper.

## Overlapped message-mode reads

overlapped-message-frame-tests.c adds a real Windows message-mode pipe experiment with FILE_FLAG_OVERLAPPED. It applies one monotonic request deadline and per-frame cap to ACK reads, covers delayed success, deadline sharing, ERROR_MORE_DATA and disconnect, and tests both safe synchronous cancellation drain and heap-owned timeout handoff. On timeout the request transfers the pending OVERLAPPED, event, buffer and pipe HANDLE to a per-operation reaper thread, then returns with a caller-owned completion ticket. The reaper observes terminal completion before closing/freeing resources. The outstanding reaper-operation cap is four; a fifth operation is rejected with ERROR_BUSY. This is a test prototype using one reaper thread per timed-out operation, not a shared persistent reaper or final IOCP architecture. A focused TX/RX test now drives a real response header ACK through the heap-owned overlapped read, then forces a DATA ACK timeout and verifies reaper handoff; the full application transport remains unintegrated.

- build-overlapped-message-frame-tests.cmd: 84 assertions for overlapped message-mode ACK reads, heap-owned timeout handoff, reaper cleanup, four-operation cap, deadline sharing, ERROR_MORE_DATA and disconnect.
## Test commands

Run from an x64 Native Tools Command Prompt:

- `build-and-test.cmd`: MSVC x64 lexical path tests (25 cases).
- `build-handle-chain-tests.cmd`: nested HANDLE-relative directory opens,
  traversal rejection, rename blocking, final-file open, size cap, directory
  rejection, hard-link detection, active-handle stability after link-count change,
  and directory-junction rejection.
- `build-directory-enumeration-tests.cmd`: HANDLE-relative multi-page directory
  enumeration (39 assertions; four additional if symlink creation is permitted).
- `build-root-registry-context-tests.cmd`: configured rootId binding and
  request-scoped HANDLE lifecycle (83 assertions; one reparse-root assertion is
  skipped locally when symlink creation is unavailable).
- `build-io-lifecycle-model.cmd`: synthetic lifecycle model (34 assertions).
- `build-deadline-tests.cmd`: request/per-frame deadline arithmetic (20 assertions).
- `build-overlapped-pipe-lifetime-tests.cmd`: actual named-pipe cancellation and
  deadline experiment (23 assertions).
- `build-overlapped-pipe-reaper-tests.cmd`: heap-owned operation/reaper
  experiment (42 assertions).
- `build-read-frame-tests.cmd`: bounded read-frame codec and response state
  machine (57 assertions).
- `build-request-error-tests.cmd`: REQUEST/ERROR payload contracts (69 assertions), including WORKSPACE_ROOTS plus five read-only operation schemas and offset/length bounds.
- `build-message-pipe-frame-transport-tests.cmd`: message-mode pipe framing
  experiment (123 assertions), including end-to-end READ_FILE, rootless WORKSPACE_ROOTS discovery, and disconnects at four protocol phases.
- build-overlapped-message-frame-tests.cmd: overlapped message-mode ACK/deadline
  experiment (84 assertions).

## Not yet established

This is not production acceptance. The root registry is test-only and does not
validate owner/DACL or authenticate callers. Remaining work includes wiring the
persistent heap-owned reaper ownership and full asynchronous frame/ACK integration,
broader async disconnect/cancellation races, production root configuration and
request identity binding, directory enumeration consistency under concurrent
mutation, write/edit/move semantics, broader adversarial race testing, and a
decision on the residual hard-link mutation behavior. None of the native I/O,
root-registry or frame-codec experiments is integrated into the Broker service
or Raya Local MCP handlers.
