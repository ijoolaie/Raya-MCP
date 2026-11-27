# Phase 1 filesystem operation acceptance matrix

Date: 2026-11-27

Status vocabulary:
- **Research slice exists**: a test-only native primitive or operation contract exists; it is not Production acceptance.
- **Partial**: only a subset of the production operation's behavior is represented.
- **Not started**: no accepted native operation contract/implementation exists.

No operation is Production-accepted by this matrix. All 15 registered production filesystem operations/modes remain NOT ACCEPTED.

| # | Production operation/mode | Current native research evidence | Status | Required acceptance work |
|---:|---|---|---|---|
| 1 | workspace_roots | Configured root registry binds rootId to pinned root HANDLE; no list-roots request/response yet | Partial | Authenticated client binding; return stable root IDs and only policy-approved display metadata; no raw absolute paths in unauthenticated protocol responses; root owner/DACL validation |
| 2 | workspace_list | LIST_DIRECTORY can pin one configured root or one nested directory; no aggregate all-roots listing | Partial | Per-root top-level listing, per-root error isolation, bounded entry serialization, stable independent enumeration handles, identity checks |
| 3 | file_read | READ_FILE opens final regular file relative to pinned parent and reads same validated HANDLE | Research slice exists | Integrate into framed transport; UTF-8/text contract; preserve 256 KiB limit; adversarial replacement/reparse/link tests |
| 4 | file_read_chunk | READ_FILE_CHUNK validates overflow-safe offset/length and exact in-file range | Research slice exists | Integrate offsetBytes/nextOffsetBytes/totalBytes/done/base64 response semantics; test file mutation and chunk continuity |
| 5 | file_write | No native write implementation | Not started | Same-HANDLE destination identity, create-new vs explicit overwrite, bounded content, temp-file write, flush, atomic publish, cleanup and no-follow policy |
| 6 | file_write_chunk | No native upload/session implementation | Not started | Upload ID binding, ordered offsets, 32 MiB total cap, 200 KiB chunk cap, expiry/concurrency control, temp-file ownership, final atomic publish, abort/cleanup and crash recovery |
| 7 | create_directory | No native directory-creation implementation | Not started | HANDLE-relative parent open, no reparse traversal, recursive creation with per-component revalidation, rollback/partial failure semantics |
| 8 | get_file_info | GET_FILE_INFO returns metadata from same validated regular-file HANDLE | Partial | Production tool also reports directories and symlinks; define safe metadata for directories/reparse entries without following them; test link-count and deletion races |
| 9 | read_multiple_files | No batch request/response implementation | Not started | Max 20 paths, per-file errors, 256 KiB per-file and 1 MiB aggregate policy, bounded payload framing, same-HANDLE validation per file, no partial over-limit disclosure |
| 10 | list_directory | HANDLE-relative directory page enumeration exists; one directory HANDLE, bounded pages | Partial | Recursive depth/entry limits, hidden-file policy, no symlink/reparse traversal, continuation semantics, concurrent mutation behavior, aggregate limits |
| 11 | move_file | No native rename/move implementation | Not started | Source/destination opened relative to pinned parents, same-root/same-volume rules, no overwrite, identity checks, atomic rename semantics, race and cleanup tests |
| 12 | edit_block | No native edit/replace implementation | Not started | Read/verify exact match count on same file identity, preview-only mode, bounded output, temp-file replacement, atomicity, metadata/ACL preservation policy, concurrent modification detection |
| 13 | start_search filename mode | No native recursive search implementation | Not started | Handle-relative bounded traversal, max 10 roots, depth/result/time/file caps, extension/hidden filters, no reparse traversal, cancellation and continuation |
| 14 | start_search content mode | No native content search implementation | Not started | All filename-mode controls plus same-HANDLE regular-file reads, 256 KiB per-file cap, UTF-8 handling, line bounds, output redaction and per-file failure isolation |
| 15 | read_file_tail | READ_FILE_TAIL reads a bounded byte suffix from same validated file HANDLE | Partial | Production tool is line-count based (1..2000 lines) and bounded to 256 KiB; define newline/UTF-8 boundary semantics and test long lines/truncated leading partial line |

## Cross-cutting gates for every operation

1. **Caller identity**: authenticate the local client/session before root selection or data access. Bind request identity to the configured client identity; do not trust request-supplied identity.
2. **Root authorization**: validate root owner and protected DACL against an explicit service/client/SYSTEM/Administrators policy. Root paths are trusted service configuration, never request data.
3. **Handle-relative resolution**: every component opens relative to the preceding pinned directory HANDLE; use no-follow/reparse protections and validate the returned HANDLE.
4. **Object identity**: validate type, reparse state, deletion state, size and link policy on the same HANDLE used for the operation. The current single-link check is only an admission-time observation; the post-check hard-link mutation behavior remains unresolved.
5. **Concurrency and mutation**: specify behavior when another process creates, deletes, renames or changes a file during the operation. Directory enumeration is not a snapshot.
6. **Resource bounds**: cap path length, depth, entries, file count, bytes, frame count, active operations, deadlines and cancellation/reaper backlog.
7. **Failure cleanup**: every temporary file, upload session, HANDLE, event, OVERLAPPED, pipe and request reference has one owner and exactly-once cleanup after terminal completion.
8. **Protocol**: validate operation-specific fields before filesystem access; never expose raw OS paths or sensitive errors to unauthenticated callers; enforce frame sequencing, ACK matching and request-wide deadlines.
9. **Verification**: native unit tests, real Windows filesystem/pipe integration tests, race/negative tests, CI on supported Windows versions, and an explicit evidence record per operation.

## Acceptance rule

An operation can move from NOT ACCEPTED only when its exact production semantics are represented by a native request/response contract and the implementation passes:
- positive behavior tests;
- invalid input and authorization tests;
- same-HANDLE identity tests;
- concurrent replacement/reparse/link race tests;
- timeout/cancellation/disconnect tests;
- resource-cap and failure-cleanup tests;
- Windows CI on the supported platform matrix.

This matrix does not authorize Production integration. PR #19 and PR #20 remain Draft until the full Phase 1 gate is met.
