# Production filesystem adapter contract — design gate
Date: 2026-10-31
Status: DESIGN ONLY — NOT IMPLEMENTED

## Purpose

Define the minimum contract a future Production adapter must satisfy before existing filesystem handlers are migrated from path-based Node I/O to isolated Windows HANDLE-relative primitives.

This document does not authorize a native addon dependency, handler migration, permission change, or a claim of race-free confinement. Production handlers remain unchanged.

## Security invariants

1. **Root authority is explicit.** Every operation starts from one configured workspace-root identity selected by the existing allowlist. Caller-supplied absolute paths never reach native code as authority.
2. **Access is handle-relative.** After opening the configured root chain, every descendant lookup is relative to a held parent HANDLE. A display path is output metadata only and must never be reopened to perform the operation.
3. **No reparse traversal.** Every opened descendant is checked for reparse attributes and rejected unless a future operation explicitly defines safe reparse semantics. Directory traversal never descends through a reparse point.
4. **Leaf names are one component.** Native child APIs accept a validated basename only; reject empty, dot/dot-dot, separators, ADS/colon, device/UNC forms, trailing-dot/space aliases, and reserved device names. Do not rely on JavaScript-only validation.
5. **No fallback.** If the native addon or OS primitive is unavailable, a HANDLE cannot be acquired, or a security check fails, the operation fails closed. It must not silently fall back to path-based Node fs.
6. **Ownership is single and explicit.** Every HANDLE has one owner, one consume-once close path, and a finalizer only as last-resort cleanup. Do not expose raw numeric HANDLE values or writable/aliasable external properties to JavaScript.
7. **Bounds are mandatory.** Enforce per-file bytes, aggregate bytes, entry counts, depth, page size, concurrency, and operation timeout before allocating or enumerating.
8. **Errors are explicit.** Preserve distinctions among not-found, access-denied, collision, invalid-name, reparse-blocked, limit-exceeded, I/O failure, close failure, and unsupported operation. Do not turn security failures into empty results.
9. **No ambiguous write success.** A create/write/replace operation may report success only after complete data transfer and required metadata verification. If the object may have committed but close/verification fails, return an explicit ambiguous-commit failure; never claim rollback unless verified.
10. **No cross-volume assumptions.** Rename/replace semantics must identify and enforce volume constraints; do not assume Win32 rename and NT relative-open semantics are interchangeable.

## Proposed internal API

The interface below is conceptual. Names and signatures are not committed until production implementation and tests exist.

### Root and parent ownership

- openWorkspaceRoot(rootId) -> RootHandle
  - Resolve rootId only through the server's configured allowlist.
  - Open the drive/root chain component-by-component.
  - Verify directory type, no reparse points, and stable volume/file identity.
  - Return an opaque N-API wrapped owner, not a path or raw HANDLE.
- openDirectoryChild(parentHandle, basename) -> DirectoryHandle
  - Open one child relative to the parent HANDLE.
  - Reject non-directory and reparse entries.
- closeHandle(handle) -> CloseResult
  - Consume ownership before attempting OS close; never retry a possibly reused numeric HANDLE.
  - Close is idempotent at wrapper level; close failure is observable.
- Root/parent handles must not be shared across unrelated requests unless concurrency and lifecycle rules are separately proven. A request context owns all handles it creates and closes them in finally.

### Read-only operations

- readFile(parentHandle, basename, maxBytes) -> Buffer + metadata
  - Open one regular, non-reparse file relative to parent.
  - Enforce exact size cap before allocation; verify bytes read and stable metadata as required.
  - Return bytes from that same opened HANDLE.
- readTail(parentHandle, basename, maxTailBytes) -> Buffer + size + offset
  - Preserve current tail response semantics and cap at 1 MiB unless product limits are intentionally changed.
- inspectEntry(parentHandle, basename) -> metadata
  - Report metadata of the entry itself; never follow a reparse target.
- enumeratePage(directoryHandle, maxEntries, cursor) -> entries + nextCursor + done
  - Cursor is bound to the held directory HANDLE and cannot be replayed against another directory.
  - Explicit EOF; no silent truncation; bounded page size.
- walk(rootHandle, options) -> bounded stream/results
  - Recurse only by opening enumerated directories relative to their held parent HANDLE.
  - Reparse entries are reported/skipped, not traversed.
  - Define per-entry error isolation, cancellation, depth/entry/byte limits, and deterministic cleanup.

### Write operations — not yet approved

The current prototypes demonstrate exclusive zero-byte file creation and exclusive directory creation, not a complete safe write transaction.

- createNewFile(parentHandle, basename, bytes)
  - Requires exclusive FILE_CREATE semantics and complete write through the returned file HANDLE.
  - Existing leaf must remain byte-for-byte unchanged on collision.
  - Short write, verification failure, and close failure need deterministic outcomes and cleanup rules.
- createDirectory(parentHandle, basename)
  - Single-child exclusive creation only; recursive directory creation is a sequence of separately verified operations and rollback policy.
- replaceFile(parentHandle, existingIdentity, tempBasename, bytes)
  - Not available. Must bind source and destination to verified handles and provide atomic replacement semantics without a path-based rename gap.
- moveFile(sourceParent, sourceBasename, destinationParent, destinationBasename)
  - Not available. Must specify no-replace behavior, same-volume rules, source identity revalidation, and atomicity. Existing path-based fs.rename is not an acceptable substitute.

Until these are implemented and accepted, Production write/edit/move/document handlers must remain on their existing implementation with the known TOCTOU caveat. Do not claim they are hardened by the read-only adapter.

## JavaScript adapter responsibilities

- Convert existing absolute workspace paths to {rootId, relativeComponents} using the existing allowlist policy, but pass only rootId and validated components to native code.
- Keep path normalization for user-facing validation and display; it is not a security boundary after native root acquisition.
- Keep public MCP tool schemas and output formats stable during the first migration.
- Preserve per-file error behavior for read_multiple_files and search; one failed item must not hide unrelated successes unless the error is traversal-wide.
- Convert native byte buffers using current UTF-8 behavior and preserve invalid UTF-8/error semantics; do not silently replace binary data.
- Enforce aggregate limits across multi-file reads and format parsers, not just per native call.
- Never serialize HANDLE wrappers, native external references, or raw file IDs as capabilities to clients.
- Keep process execution (run_check, powershell_execute) outside filesystem confinement claims. Child processes can independently access host files.

## Initial migration sequence

1. **Read-only vertical slice:** file_read and get_file_info only. Keep old handlers behind an explicit test-only comparison harness, not a runtime fallback.
2. **Read-family parity:** read_multiple_files and read_file_tail, including per-item errors and aggregate byte budgets.
3. **Enumeration parity:** workspace_list, list_directory, then start_search; include pagination/session TTL, cancellation, bounded handles, and per-entry error behavior.
4. **Format readers:** route file bytes through the adapter before parsing XLSX/DOCX/PDF. Parser limits and ZIP/decompression limits remain separate concerns.
5. **Write family:** only after create/write/replace/rename primitives have independent race, failure, cleanup, collision, and close-status acceptance suites.
6. **Acceptance:** production-equivalent tests, Windows permission-denied tests, supported Node/Windows build matrix, package install/uninstall validation, and regression suite.

## Build and deployment gates

- The isolated prototype dynamically resolves NtCreateFile from ntdll.dll. This is a low-level Windows Native API, not a stable high-level Win32 contract. Production adoption requires a documented supported Windows/Node matrix, explicit NTSTATUS mapping, architecture-specific builds, and a decision on unsupported systems fail closed.
- Build reproducibly from checked-in source with supported Node headers/toolchain; do not depend on manually copied .node, .obj, .lib, or .exp artifacts.
- Native binary loading must verify expected architecture and ABI. Missing/mismatched binaries must disable the hardened operation, not enable path fallback.
- Keep test-only synchronization and fault-injection exports out of production builds.
- No production dependency, build script, package entry, or handler migration until code review and acceptance tests are complete.

## Acceptance matrix

| Capability | Isolated evidence | Production adapter status |
|---|---|---|
| Root-chain handle walk | Synchronized root/ancestor replacement tests | Not implemented |
| Parent HANDLE ownership | Explicit close, identity after rename, invalid path and handle-count checks | Not implemented |
| Handle-relative child read | Synchronized in-root insertion and outside junction rejection | Not implemented |
| Exclusive file create | Collision and parent/leaf replacement tests; zero-byte only | Not implemented |
| Exclusive directory create | Parent replacement, collision, junction, close-status tests | Not implemented |
| Tail/multi-read | Isolated tail and sequential wrapper tests | Not implemented |
| Directory enumeration | Multi-page, restart, EOF, bounds, reparse and fault tests | Not implemented |
| Metadata | File/directory/reparse metadata checks | Not implemented |
| Full write and safe replace | No accepted implementation | Not available |
| Handle-relative rename/move | No accepted implementation | Not available |
| Windows ACL/permission denial | Not completed | Not accepted |
| Production-equivalent integration | Not completed | Not accepted |

## Read-only vertical-slice feasibility review — 2026-10-31

The existing isolated addon was inspected against the two first migration targets.

### Confirmed native entry points

The dirwalk prototype exports:
- `openParentRelative(rootPath, relativeParentPath)`: opens the configured root chain, then each relative parent component by held HANDLE; returns an N-API wrapped parent owner.
- `readChildFromParent(parent, childName, maxReadBytes)`: validates one leaf component, opens it relative to that parent HANDLE, rejects reparse/directory leaves, enforces the byte cap, and returns a Buffer plus byte size.
- `inspectEntryMetadata(parent, childName)`: opens one leaf relative to the held parent and returns file ID, volume serial, size/allocation size, link count, directory/delete-pending flags, attributes, reparse flag, and raw Windows FILETIME values.
- `closeParentHandle(parent)`: consumes the wrapped parent owner.

These APIs are present in the test-only addon and do not imply a production-ready adapter.

### Exact production parity requirements

- `file_read` currently accepts an absolute path, resolves it against configured `ROOTS`, checks realpath containment, calls `fs.stat`, rejects non-files and files above 256 KiB, then returns UTF-8 text from `fs.readFile`. A native adapter must preserve the 256 KiB cap, regular-file requirement, UTF-8 conversion behavior, public response shape, and current path-not-allowed/not-found/permission distinctions where safely mappable.
- `get_file_info` currently uses `securePath` followed by `fs.lstat`. It reports path, type (`symlink` / `directory` / `file` / `other`), sizeBytes, ISO creation/modified/accessed timestamps, readonly derived from the Node mode write bit, and symlink. Native metadata is richer but uses Windows FILETIME and attributes; a deliberate, tested mapping is required. A reparse point must not be followed merely to preserve old output semantics.

### Remaining adapter blockers

1. `openWorkspaceRoot(rootPath)` now exists and is tested in the isolated addon. It still accepts a path string, not a root ID. The future JavaScript adapter must map an internal root identity to a path selected only from configured `ROOTS`; no caller-supplied absolute path may become root authority.
2. Root-level child access and root-directory metadata are now supported by the isolated root HANDLE API. This does not yet establish a Production root identity/handle lifecycle.
3. Root-chain and relative-parent components in the new isolated source now share the strict leaf-name validator. Dedicated tests cover trailing-dot/space aliases, reserved device names, forbidden punctuation and traversal. Older prototype binaries/sources are not retroactively changed.
4. `readChildFromParent` returns bytes. The JS-only harness verifies `Buffer.toString("utf8")` matches Node `readFile(..., "utf8")`, including invalid UTF-8 replacement, and verifies the 256-KiB cap. This is harness parity, not Production error-category parity.
5. `inspectEntryMetadata` exposes a reparse tag and, for known mount-point/symlink tags, reads the held reparse object with `FSCTL_GET_REPARSE_POINT`. The adapter harness now maps `IO_REPARSE_TAG_MOUNT_POINT` and `IO_REPARSE_TAG_SYMLINK` to Node `type: symlink`, uses the decoded print-name UTF-8 byte length for Node-compatible `sizeBytes`, and matches Node timestamps/readonly on junction, absolute/relative symlink, and Unicode fixtures. It never follows the target. Unknown reparse tags still fail closed.
6. The JS-only harness closes all parent/root handles in `finally`, verifies HANDLE count returns to baseline after injected failures, and now covers root and parent rename/outside-junction replacement after HANDLE acquisition. Production integration still needs explicit error-category mapping, close-failure behavior and request-scoped lifecycle tests.

### Root HANDLE API and adapter harness evidence — 2026-10-31

A new isolated source/build target exports `openWorkspaceRoot(rootPath)`. It opens the root chain itself and returns an opaque N-API owner. This supports files directly under the root and root-directory metadata. The argument remains a path string, not a root ID; the JavaScript adapter must obtain it only from configured `ROOTS`. Root ID resolution is not yet implemented in Production.

The isolated root API source applies the strict leaf-name validator to both root-chain and relative-parent components. Root/parent names with trailing dot/space, reserved device names, forbidden punctuation, and traversal components are rejected before opening. The dedicated harness passed 10 checks, including a root-directory rename and outside-junction replacement after root HANDLE acquisition; a subsequent read returned the original in-root bytes and preserved the outside canary. HANDLE accounting returned to baseline.

The isolated `inspectParentHandle` result now includes size, allocation size, attributes, read-only attribute, reparse tag, and FILETIME timestamps. A JS-only comparison harness exercised configured-root selection, root-level and nested reads, UTF-8 and invalid-UTF-8 conversion, the 256-KiB cap, root/file/directory metadata, size and creation/modification timestamp parity, read-only mapping on ordinary fixtures, outside-root rejection, and HANDLE cleanup. It passed 9 checks.

The initial comparison harness intentionally rejected reparse-point metadata. Follow-up native `FSCTL_GET_REPARSE_POINT` work now verifies Node-compatible size and public symlink metadata for mount-point/symlink tags; unknown tags remain rejected. The JS-only adapter maps the known tags to Node's `get_file_info` shape without following targets. These files and scripts remain isolated test-only artifacts. No Production handler, native production source, package dependency, runtime configuration, root allowlist, or permissions changed.

## Decision for this slice

The root-handle prerequisite and JS-only read-only comparison harness now pass isolated parity, injected cleanup, and root/parent replacement schedules. Do not connect them to Production yet. Next, design and test explicit native-to-public error categories for not-found, access-denied, collision, invalid-name, reparse-blocked, size-limit, I/O and close failures. Permission-denied/ACL behavior remains untested; do not change ACLs to force a test. Resolve rootId-to-configured-ROOTS binding and request-scoped HANDLE lifecycle. Keep the harness separate from MCP registration and do not introduce runtime fallback. Only after these gates and the remaining Phase 1 acceptance tests pass should a Production adapter proposal be reviewed.

## Go / no-go

**NO-GO for Production integration today.** The isolated prototypes provide useful evidence for individual primitives, but there is no production adapter, no accepted write/replace/move transaction, no ACL/permission-denied acceptance, and no production-equivalent integration suite.

Phase 1 remains **IN PROGRESS / NOT ACCEPTED**.

## Reparse-tag parity and adapter error probes — 2026-10-31

Added `.native-probe/reparse-parity-probe.cjs` and the `test:native-reparse-parity` package script. Runtime fixtures successfully created a directory junction, directory symlink, and file symlink on this host. The native metadata tags matched the Windows definitions exactly: junction `0xA0000003` (`IO_REPARSE_TAG_MOUNT_POINT`); both symlinks `0xA000000C` (`IO_REPARSE_TAG_SYMLINK`). Node `lstat().isSymbolicLink()` returned true for all three and `lstat().isDirectory()` returned false. Native metadata correctly marked all three as reparse points, but its directory attribute was true for directory junction/symlink objects.

A material parity gap was measured: native `FILE_STANDARD_INFO` reported size `0` for all three reparse objects, while Node `lstat().size` reported 66 bytes for the directory links and 77 bytes for the file symlink in this fixture. Therefore tag-based type mapping alone is insufficient to reproduce `get_file_info`; reparse metadata remains fail-closed in the adapter until size/timestamp/mode semantics are resolved without following the target. The adapter must not copy the native `isDirectory` bit into Node's public type for reparse entries.

Extended the read-only adapter harness with missing-leaf, missing-parent, and unavailable configured-root failure checks; each fails closed and returns the process HANDLE count to baseline. Permission-denied behavior is still untested because no safe ACL fixture or approved injected access-denied path is available. No ACL changes were made.

Verification on 2026-10-31: `npm lint` passed; `npm test` passed (42 core, 34 format/config, HTTP unauthorized/authorized 401/200, 36 URL-security); all six prior native suites passed individually, and the new reparse parity suite passed. The read-only adapter comparison now includes the additional missing-path/root checks. A direct `npm` executable launch through the managed-process tool returned `spawn npm ENOENT`; the predefined local lint/test checks succeeded and are the authoritative full-suite evidence. No Production handler, native production source, configured root, dependency, runtime setting, service, tunnel, or permission changed. Phase 1 remains IN PROGRESS / NOT ACCEPTED.

## Reparse metadata mapping and adapter race verification — 2026-10-31 (supersedes earlier reparse-size blocker)

Extended the isolated root API native metadata probe to read reparse data from the held reparse HANDLE with `FSCTL_GET_REPARSE_POINT`, validate the reparse header/name bounds, convert the print name to UTF-8, and derive Node-compatible logical size. `FILE_STANDARD_INFO.EndOfFile` and `GetFileInformationByHandle` both return zero for these reparse objects; the Node-compatible size is instead the UTF-8 byte length of the stored print name after removing the Windows extended-path `\\?\` prefix where present. This parsing is limited to the known mount-point and symlink tags; other reparse tags remain unsupported/fail-closed.

Added a durable parity harness for five fixture types: directory junction (`0xA0000003`), absolute directory symlink, absolute file symlink, relative file symlink, and Unicode directory symlink. All five matched Node `lstat` for `isSymbolicLink`, public type semantics (symlink, not directory), size, creation/modification/access timestamps (within 1 ms), and readonly mapping. Native directory attributes remain true for directory reparse objects; the JS adapter correctly maps recognized reparse tags to public `type: symlink` and does not reuse the native directory bit for public type.

Updated the JS-only read-only adapter harness to map only `IO_REPARSE_TAG_MOUNT_POINT` and `IO_REPARSE_TAG_SYMLINK` to Node's symlink metadata contract, using the validated native reparse logical size. Unsupported reparse tags fail closed. File reads through a reparse point still fail closed; metadata reads inspect the link object itself and do not follow its target. Junction and symlink canaries outside the root remain unchanged.

Extended adapter fault/race schedules: injected exceptions immediately after root HANDLE acquisition and parent HANDLE acquisition both release all acquired HANDLEs; a root directory rename followed by outside-junction replacement still reads the original in-root file through the held root HANDLE; a parent directory rename followed by outside-junction replacement still reads the original in-root file through the held parent HANDLE. Process HANDLE count returns to baseline after all success and failure cases.

Build and verification on 2026-10-31: root API MSVC x64 build exited 0 with no warnings. All seven isolated native harnesses passed individually (create race, directory-create race, read-family race, directory-walk race, root API, read-only adapter, reparse parity). `npm lint` passed; `npm test` passed (42 core, 34 format/config, HTTP unauthorized/authorized 401/200, 36 URL-security).

Remaining blockers: Production error-category parity is not implemented; missing-path tests prove fail-closed cleanup but do not establish exact not-found/permission distinctions. Permission-denied/ACL behavior remains untested and no ACL changes were made. Unknown reparse tags remain unsupported. The adapter still uses a test-only configured-root array and is not wired to MCP registration or Production `ROOTS` root identity. No Production handler, native production source, configured root, dependency, runtime setting, service, tunnel, or permission changed. Phase 1 remains IN PROGRESS / NOT ACCEPTED.

## Native error categories — 2026-10-31

Added a test-only NTSTATUS classification layer to `.native-probe/handle-relative-probe-phase1-rootapi.c` and exposed it as `classifyNtStatusForTest`. The classifier distinguishes `not_found`, `access_denied`, `already_exists`, `invalid_name`, `is_directory`, `not_directory`, `reparse_blocked`, `sharing_violation`, `storage_full`, and `resource_exhausted`; unknown statuses remain `io_error`. The ten native NTSTATUS open-failure formatting sites now include the stable category prefix alongside the raw NTSTATUS for diagnostics. This is still an isolated probe API, not the Production MCP error contract.

Added `.native-probe/native-error-map-test.cjs` and `test:native-error-map`; 17 status/argument cases passed. The read-only adapter harness now asserts real `not_found` categories for missing leaf, missing parent and missing configured root, and `is_directory` when a directory is passed to the file reader. These real operations also preserve HANDLE cleanup. `access_denied` now also has a one-shot native child-open fault-injection test; collision now has a live FILE_CREATE test, invalid-name has an actual unsafe-leaf test, and reparse-blocked remains a classifier-vector test. This does not prove live ACL behavior. No ACL was modified; permission-denied ACL acceptance remains open.

Latest verification after the native error classifier and parser-bound checks: isolated MSVC x64 root API build exited 0; all eight native harnesses passed (create race, directory-create race, read-family race, directory-walk race, root API, read-only adapter, reparse parity, native error map); `npm lint` passed including the new harness syntax check; `npm test` passed (42 core, 34 format/config, HTTP unauthorized/authorized initialize 401/200, 36 URL-security).

Remaining gates: real access-denied/ACL test without bypassing safety controls; live collision and other status-path mapping assertions where practical; rootId-to-configured-`ROOTS` binding; request-scoped HANDLE lifecycle; production-equivalent integration for each filesystem operation family; write/replace/move transaction semantics; and acceptance review. No Production handler, native production source, configured root, dependency, runtime setting, service, tunnel or permission changed. Phase 1 remains IN PROGRESS / NOT ACCEPTED.

## Access-denied fault injection — 2026-10-31

Extended the isolated native probe with a one-shot `failNextChildOpenStatusForTest` injector scoped to the handle-relative child-read open. The injected `STATUS_ACCESS_DENIED` follows the same native NTSTATUS error path and is emitted as `access_denied`; the read-only adapter harness confirms the error is distinct and all root/parent HANDLEs are closed afterward. This exercises the category path without changing Windows ACLs or creating a permission-denied ACE. It is deterministic fault injection, not a live ACL/authorization test.

The same adapter harness now verifies real `not_found` for missing leaf, missing parent and unavailable configured root, and real `is_directory` for a directory passed to the file reader. The native status classifier vectors cover known status families and generic fallback; the native error harness now has 18 total checks including a real collision. The access-denied case is no longer classifier-only, but real ACL behavior remains unverified.

Rebuilt the isolated root API addon with MSVC x64 (exit 0). After this change all eight native suites passed again; `npm lint` and `npm test` both passed. No Production handler, native production source, dependency, runtime setting, root allowlist, service, tunnel or ACL changed. Phase 1 remains IN PROGRESS / NOT ACCEPTED.

## Live collision, invalid-name and close-failure categories — 2026-10-31

Extended the native error harness to exercise a real `FILE_CREATE` collision against an existing file. The OS-returned NTSTATUS is surfaced as `already_exists`; the original file bytes remain unchanged and process HANDLE accounting returns to baseline. The adapter harness now exercises an actual unsafe leaf (`bad:name`) and receives `invalid_name`; missing leaf/parent/root still produce `not_found`; a directory passed to the file reader produces `is_directory`.

Normalized close failures with the `close_failure` category prefix. A deterministic `failNextCloseForTest` is armed only after the root HANDLE has been acquired, so the injected failure is consumed by the child file HANDLE close rather than a root-chain transition. The adapter observes `close_failure` and still closes the remaining parent/root HANDLEs. This models a post-close failure status; it does not model every possible kernel close failure.

The native error harness now passes 18 checks, including the real collision case. The adapter harness now covers real not-found, invalid-name, is-directory, live injected access-denied, and injected close-failure categories with HANDLE cleanup. Access-denied remains a native fault injection, not a live ACL test; no ACL changes were made.

Rebuilt the isolated root API addon with MSVC x64 (exit 0). All eight native suites passed again; `npm lint` passed; `npm test` passed (42 core, 34 format/config, HTTP 401/200, 36 URL-security). No Production handler, native production source, configured root, dependency, runtime setting, service, tunnel or permission changed. Phase 1 remains IN PROGRESS / NOT ACCEPTED.
