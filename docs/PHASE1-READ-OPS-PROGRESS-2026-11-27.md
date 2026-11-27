# Phase 1 filesystem confinement - research progress

Date: 2026-11-27

## Current state

The full per-operation status and required evidence are tracked in PHASE1-FILESYSTEM-OPERATION-ACCEPTANCE-MATRIX.md.

**Phase 1: IN PROGRESS / NOT ACCEPTED. Production confinement: NO-GO.**

PR #20 remains Draft and is not merged. PR #19 remains a separate Draft. No Production service, real workspace, production ACL, or production handler was modified. The original dirty checkout at C:\Users\Ali\local-mcp-bridge remains untouched.

## Test-only read-only operation slice

The native research layer now defines five file operations:

- READ_FILE: complete file, bounded by the 256 KiB response limit.
- GET_FILE_INFO: metadata queried from the same validated final-file HANDLE.
- READ_FILE_CHUNK: exact offset/length range; nonzero length, at most 256 KiB, overflow checked, and a range extending past EOF is rejected.
- READ_FILE_TAIL: up to the requested final bytes, with a maximum of 256 KiB.
- LIST_DIRECTORY: pins the configured root or a nested directory HANDLE; bounded enumeration remains a separate operation.

REQUEST payload is a 20-byte little-endian fixed header, followed by rootId and UTF-16LE relative path. It carries operation, lengths, reserved-zero field, 64-bit offset, and 32-bit requested length. Both the parser and root-registry context layer validate operation-specific arguments.

All file operations resolve rootId against trusted test configuration, pin parent directories relative to the configured root HANDLE, and open the final file relative to the pinned parent. The same validated file HANDLE is used for content reads and metadata. The per-context SRWLOCK serializes read/metadata calls; the caller must stop and join all context users before closing the context. Root-level LIST_DIRECTORY now obtains an independent root HANDLE view by reopening the path obtained from the pinned HANDLE and verifying the same volume serial and 128-bit file ID. This prevents concurrent root-list contexts from sharing an enumeration cursor.

## Overlapped message-mode timeout/reaper experiment

A separate Windows message-mode pipe harness uses FILE_FLAG_OVERLAPPED and applies one monotonic request deadline plus a per-frame cap to actual ACK reads.

- Normal completion returns the decoded ACK and cleans request-owned resources.
- Timeout calls CancelIoEx and transfers the pending OVERLAPPED, event, buffer and pipe HANDLE to a heap-owned reaper operation.
- The request returns with a caller-owned completion ticket; the reaper waits for terminal completion before freeing resources.
- At most four reaper operations may be outstanding; a fifth is rejected with ERROR_BUSY.
- The prototype creates one reaper thread per timed-out operation. It is not a shared persistent reaper or final IOCP architecture, and is not wired into the complete framed TX/RX loop.

## Local validation

All twelve native build/test commands passed locally on Windows x64/MSVC:

- relative-path gate: 25
- HANDLE chain/final-file tests: passed; local file-symlink creation unavailable and explicitly skipped
- directory enumeration: 39
- configured root/request context: 67
- I/O lifecycle model: 34
- deadline arithmetic: 20
- overlapped pipe cancellation/deadlines: 23
- heap-owned reaper: 42
- frame codec/state machine: 57
- REQUEST/ERROR contracts: 62
- message-mode frame transport: 109
- overlapped message-mode frame reads/reaper: 58

The new overlapped message-mode test passed three consecutive runs after the final 58-assertion version. The local host returned ERROR_PRIVILEGE_NOT_HELD (1314) for some symbolic-link fixture creation. Those cases remain SKIP, not PASS.

GitHub Actions run #64 (ID 45975095382) completed green for code head 1acf844, and run #65 (ID 45975237832) completed green for documentation head 703c3ce. Those runs include the earlier 27-assertion overlapped message-mode version. The current 58-assertion heap-reaper expansion is not yet pushed and has no CI result.

## Security and implementation boundaries still open

1. NumberOfLinks == 1 is only an admission-time observation. Windows permits a hard link to be added afterward while the existing HANDLE remains open. This remains a security acceptance decision.
2. Directory enumeration is not snapshot-consistent under concurrent child mutation.
3. The test-only root registry does not validate root owner/DACL, authenticate the client, or load production ROOTS.
4. The overlapped message-mode reaper prototype is not integrated into the full framed transport. It uses one thread per timed-out operation; shared reaper queue versus IOCP remains undecided.
5. Disconnects are covered at four major response phases in the synchronous frame harness. Partial-write, REQUEST/ERROR and asynchronous cancellation/reconnect matrices remain incomplete.
6. workspace_roots, workspace_list, read_multiple_files, filename/content search, write/edit/create/move/rename/delete and other registered production modes remain unimplemented or unaccepted in this native slice.
7. Nothing is integrated into the Broker service or Raya Local MCP production handlers.

## Next sequence

1. Commit and push the 58-assertion overlapped message-mode reaper expansion and observe its complete CI run.
2. Integrate the tested per-operation reaper into every framed REQUEST/RESPONSE/ACK/ERROR wait; compare a shared reaper queue and IOCP against the current thread-per-timeout prototype.
3. Add asynchronous disconnect/cancellation race tests for REQUEST, RESPONSE_HEADER, DATA, END, ERROR, partial message, timeout and shutdown.
4. Define production root owner/DACL and authenticated client identity requirements; do not reuse the test registry as production authorization.
5. Continue operation-family design and acceptance for workspace roots/listing, multiple reads and search, then writes/edits/moves/renames/deletes with transaction/race/failure cleanup.
6. Resolve the hard-link mutation policy before any Production integration.

No Phase 1 completion or Production acceptance should be declared until every registered filesystem operation/mode has an explicit approved contract and passes its adversarial acceptance matrix.
