# Phase 1 filesystem confinement — research progress

Date: 2026-11-27

## Current state

**Phase 1: IN PROGRESS / NOT ACCEPTED. Production confinement: NO-GO.**

PR #20 remains Draft and is not merged. PR #19 remains a separate Draft. No Production service, real workspace, production ACL, or production handler was modified. The original dirty checkout at C:\Users\Ali\local-mcp-bridge remains untouched.

## New read-only operation research

The PR #20 test-only native layer now defines five file operations plus directory listing:

- READ_FILE: complete file, bounded by the 256 KiB response limit.
- GET_FILE_INFO: metadata queried from the same validated final-file HANDLE.
- READ_FILE_CHUNK: exact offset/length range; nonzero length, at most 256 KiB, overflow checked, and a range extending past EOF is rejected.
- READ_FILE_TAIL: up to the requested final bytes, with a maximum of 256 KiB.
- LIST_DIRECTORY: pins the configured root or a nested directory HANDLE; bounded enumeration remains a separate operation.

REQUEST payload is now a 20-byte little-endian fixed header, followed by rootId and UTF-16LE relative path. It carries operation, lengths, reserved-zero field, 64-bit offset, and 32-bit requested length. Both the parser and root-registry context layer validate the operation-specific arguments.

All file operations resolve rootId against trusted test configuration, pin parent directories relative to the configured root HANDLE, and open the final file relative to the pinned parent. The same validated file HANDLE is used for content reads and metadata. The per-context SRWLOCK serializes read/metadata calls; the caller must stop and join all context users before closing the context.

## Local validation

All eleven native build/test commands passed locally on Windows x64/MSVC after the latest changes:

- relative-path gate: 25
- HANDLE chain/final-file tests: passed; local file-symlink creation unavailable and explicitly skipped
- directory enumeration: 30
- configured root/request context: 65
- I/O lifecycle model: 34
- deadline arithmetic: 20
- overlapped pipe cancellation/deadlines: 23
- heap-owned reaper: 42
- frame codec/state machine: 57
- REQUEST/ERROR contracts: 62
- message-mode frame transport: 109

The local host returned ERROR_PRIVILEGE_NOT_HELD (1314) for some symbolic-link fixture creation. Those cases remain SKIP, not PASS. GitHub Actions run #62 (ID 45972087376) is fully green for the preceding head 0d6125885286a6c9f8310c530752132cb66c94b1.

## Security and implementation boundaries still open

1. The NumberOfLinks == 1 check is only an admission-time observation. Windows permits a hard link to be added afterward while the existing HANDLE remains open. This remains a security acceptance decision.
2. Directory enumeration is not snapshot-consistent under concurrent child mutation.
3. The test-only root registry does not validate root owner/DACL, authenticate the client, or load production ROOTS.
4. Deadline arithmetic, overlapped cancellation, and reaper ownership are tested separately, but deadlines are not wired into asynchronous framed message/ACK I/O. The message-mode framed transport remains synchronous.
5. Disconnects are covered at four major response phases, but partial-write, REQUEST/ERROR and asynchronous cancellation/reconnect matrices remain incomplete.
6. Only this read-only subset has test-only native request-context implementations. Write, edit, move, rename, delete, directory creation, search, and other registered production modes remain unaccepted.
7. Nothing is integrated into Broker service or Raya Local MCP production handlers.

## Next sequence

1. Commit and push this isolated read-only operation contract/test increment; observe its complete CI run.
2. Implement an asynchronous message-mode frame/ACK transport that consumes one monotonic request deadline and per-frame caps, with heap-owned OVERLAPPED records and reaper-only cleanup.
3. Add async disconnect/cancellation race tests for REQUEST, RESPONSE_HEADER, DATA, END, ERROR, partial message, timeout and shutdown.
4. Define production root owner/DACL and authenticated client identity requirements; do not reuse the test registry as production authorization.
5. Continue operation-family design and acceptance for writes/edits/moves/renames/deletes and transaction/race/failure cleanup.
6. Resolve the hard-link mutation policy before any Production integration.

No Phase 1 completion or Production acceptance should be declared until every registered filesystem operation/mode has an explicit approved contract and passes its adversarial acceptance matrix.
