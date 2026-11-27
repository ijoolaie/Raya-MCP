# Read-only operation contracts — research slice

Status: test-only native research. These helpers are not connected to the Broker service or Raya Local MCP production handlers.

## Request wire contract

REQUEST payload fixed header is 20 bytes, little-endian:

| Offset | Width | Field |
|---:|---:|---|
| 0 | 2 | operation |
| 2 | 2 | rootId byte length |
| 4 | 2 | UTF-16 path code-unit length |
| 6 | 2 | reserved, must be zero |
| 8 | 8 | byte offset |
| 16 | 4 | requested length |

Then comes a 1–64 byte ASCII rootId and a UTF-16LE relative path. The path is validated for paired surrogates and by the lexical relative-path gate. RootId contains only ASCII letters, digits, underscore and hyphen. The configured-root registry is the separate layer that maps rootId to a pinned root HANDLE.

## Supported file operations in this slice

| Operation | Request constraints | Result |
|---|---|---|
| READ_FILE | Nonempty relative path; offset=0; length=0 | Reads the complete file; rejects files larger than the 256 KiB response ceiling or caller buffer |
| GET_FILE_INFO | Nonempty relative path; offset=0; length=0 | Returns size, link count, attributes, creation/access/write timestamps, directory/reparse/delete-pending flags from the same validated file HANDLE |
| READ_FILE_CHUNK | Nonempty relative path; length 1..256 KiB; overflow-safe offset+length | Reads exactly the requested range; range extending beyond EOF is rejected |
| READ_FILE_TAIL | Nonempty relative path; offset=0; length 1..256 KiB | Reads up to the requested final bytes; if the file is shorter, returns the entire file |
| LIST_DIRECTORY | Relative path may be empty; offset=0; length=0 | Pins the configured root or nested directory HANDLE; enumeration is a separate bounded page operation |

All file operations use the same final-component open helper relative to the pinned parent HANDLE. It rejects reparse points and delete-pending objects, checks disk-file/non-directory type, bounded size and an admission-time link count of exactly one. The residual hard-link race remains unresolved: Windows permits a new link to be added after the check while the existing HANDLE remains open.

## HANDLE and lifetime rules

- Root paths are trusted test configuration; request payloads carry rootId and relative paths only.
- Root HANDLEs are opened once and retained until registry shutdown.
- Each request context owns its final file HANDLE and nested directory-chain HANDLEs; the configured root HANDLE is borrowed.
- A registry cannot shut down while any request context remains active.
- Reads and metadata queries on one context are serialized by its SRWLOCK. The caller must stop and join all users of a context before closing/destroying it; the lock is not a substitute for external lifetime ownership.
- File reads use an explicit offset with NtReadFile and the same already-validated HANDLE. They do not reopen by path.

## Local evidence

- REQUEST/ERROR schema suite: 62 assertions.
- Configured root registry/request-context suite: 65 assertions.
- Message-mode framed transport suite: 109 assertions, including one end-to-end READ_FILE path and disconnect checks before header ACK, between DATA frames, during final END ACK, and after END.
- All eleven native test commands passed locally on Windows x64/MSVC at the current working state.
- The local host disallowed symbolic-link creation in some fixtures (ERROR_PRIVILEGE_NOT_HELD 1314); those cases are recorded as SKIP, not PASS. CI must independently exercise them.

## Not accepted / not implemented

This is not production filesystem confinement. The registry does not validate root owner/DACL or authenticate the caller, and it does not load the application's production ROOTS configuration. The message-mode frame transport remains a synchronous experiment: monotonic deadline arithmetic, actual overlapped cancellation and heap-owned reaping are tested separately but are not wired into asynchronous frame/ACK transport.

Directory enumeration is not a snapshot under concurrent child mutation. No write, edit, move, rename, delete or atomic replacement operation is implemented here. The strict-single-link threat-model decision remains open. No native component in this directory is integrated with Production.
