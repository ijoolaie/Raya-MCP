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
- Each request context owns its final file HANDLE and nested directory-chain HANDLEs; the configured root HANDLE is borrowed. A root-level LIST_DIRECTORY request obtains a separate root HANDLE view from the path returned by the pinned HANDLE, then verifies exact volume serial and 128-bit file ID before accepting it. This prevents concurrent root-list contexts from sharing the same enumeration cursor.
- A registry cannot shut down while any request context remains active.
- Reads and metadata queries on one context are serialized by its SRWLOCK. The caller must stop and join all users of a context before closing/destroying it; the lock is not a substitute for external lifetime ownership.
- File reads use an explicit offset with NtReadFile and the same already-validated HANDLE. They do not reopen by path.

## Local evidence

- REQUEST/ERROR schema suite: 69 assertions.
- Configured root registry/request-context/workspace-list suite: 83 assertions.
- Message-mode framed transport suite: 123 assertions, including one end-to-end READ_FILE path and disconnect checks before header ACK, between DATA frames, during final END ACK, and after END.
- Overlapped message-mode ACK/deadline suite: 84 assertions for actual ACK reads, request/per-frame deadlines, heap-owned timeout handoff, terminal reaper cleanup, four-operation cap, ERROR_MORE_DATA and disconnect. The prototype uses one reaper thread per timed-out operation; it is not a shared persistent reaper or IOCP design.
- All thirteen native test commands passed locally on Windows x64/MSVC at the current working state.
- The local host disallowed symbolic-link creation in some fixtures (ERROR_PRIVILEGE_NOT_HELD 1314); those cases are recorded as SKIP, not PASS. CI must independently exercise them.

## Not accepted / not implemented

This is not production filesystem confinement. The registry does not validate root owner/DACL or authenticate the caller, and it does not load the application's production ROOTS configuration. The full message-mode frame transport remains synchronous. A separate overlapped ACK-read experiment applies request/per-frame deadlines and transfers timed-out I/O to heap-owned per-operation reaper threads, with a four-operation cap. A focused response TX/RX test drives a header ACK through the heap-owned overlapped read, then withholds the DATA ACK to verify timeout and reaper handoff. The full application transport remains unintegrated, and the production choice between a shared reaper and IOCP remains open.

Directory enumeration is not a snapshot under concurrent child mutation. No write, edit, move, rename, delete or atomic replacement operation is implemented here. The strict-single-link threat-model decision remains open. No native component in this directory is integrated with Production.

## Workspace root discovery metadata operation

Operation code 6, WORKSPACE_ROOTS, is a rootless metadata request. Its REQUEST
must contain no rootId, no relative path and zero offset/length. The registry
returns a bounded payload containing a little-endian u16 root count followed by
u8 ASCII rootId lengths and rootId bytes. It never returns configured absolute
paths. The request-context opener rejects this metadata-only operation because
it does not open a filesystem object.

Evidence: the REQUEST/ERROR suite validates rootless encoding/decoding and
rejects caller-supplied rootId/path; the registry suite validates stable IDs,
no path disclosure, insufficient output capacity and closed-registry refusal.
The message-mode pipe harness sends WORKSPACE_ROOTS as a real REQUEST message,
gets IDs from the registry, transfers RESPONSE_HEADER/DATA/END with ACKs and
byte-compares the result. This remains a test harness only; production dispatch,
Node/UI integration and production ROOTS configuration are absent.

## Latest overlapped reaper lifetime correction

The heap-owned message reaper must call GetOverlappedResult with bWait=TRUE after
handoff. A nonblocking GetOverlappedResult(..., FALSE) can return
ERROR_IO_INCOMPLETE; freeing the OVERLAPPED, buffer or pipe at that point would
violate kernel I/O lifetime. A gated regression test now proves the reaper keeps
its operation slot until it observes terminal completion. The focused TX/RX
test verifies a delayed header ACK completes normally and a missing DATA ACK
times out, transfers ownership to the reaper, and is drained before resources
are released. The overlapped message-mode suite now passes 84 assertions.

## Workspace list aggregation

The test-only raya_root_registry_list_roots adapter models workspace_list with
no explicit path. It opens an independent identity-verified directory HANDLE
view for each configured root, enumerates each root separately, and isolates
per-root failures using bounded RAYA_ERROR codes. The result carries rootId and
relative entry names only. It returns a bounded first page (1..64 entries per
root), probes one following page to determine truncation, and reports
truncated=true without returning a continuation token. It holds a registry
active reference until all roots have been enumerated. This is not a complete
production workspace_list contract: no framed dispatch, continuation/session,
Node handler, client authentication or production ROOTS integration exists.
## Configured root owner/DACL policy

raya_validate_root_security reads owner and DACL from the pinned root HANDLE.
It requires the service SID as owner, a present protected DACL, and exactly four
inheritable allow ACEs: service/SYSTEM/Administrators full access and the
authorized client with the explicit read-only mask. Unknown/duplicate trustees,
extra ACEs, broad principals, wrong masks/flags and default inherited DACLs are
rejected. raya_root_registry_init_secure validates all opened roots and closes
the registry atomically if any root fails. Its 31-assertion test includes valid
and negative descriptors and secure-init behavior. The test uses the current
process SID as a synthetic service identity; it does not authenticate the actual
service process or pipe client. Production client.policy and peer-token binding
remain unimplemented.