# Phase 1 filesystem operation acceptance matrix

Date: 2026-11-27

Status vocabulary:
- **Research slice exists**: a test-only native primitive or operation contract exists; it is not Production acceptance.
- **Partial**: only a subset of the production operation's behavior is represented.
- **Not started**: no accepted native operation contract/implementation exists.

No operation is Production-accepted by this matrix. All 15 registered production filesystem operations/modes remain NOT ACCEPTED.

| # | Production operation/mode | Current native research evidence | Status | Required acceptance work |
|---:|---|---|---|---|
| 1 | workspace_roots | Test-only WORKSPACE_ROOTS operation (code 6) returns stable root IDs from configured registry; message-mode request/response and ACK path tested; no absolute paths disclosed | Partial | Bind the actual named-pipe peer PID/token/session to trusted client.policy before returning IDs; integrate the tested owner/protected-DACL validator into the real service startup; define and test policy-approved display metadata |
| 2 | workspace_list | Test-only workspace_list aggregate adapter enumerates each configured root through an independent identity-verified HANDLE view; bounded first page, rootId/relative names only, per-root error isolation and truncation signal | Partial | Continuation/session token and stable pagination semantics; define concurrent mutation behavior (enumeration is not a snapshot); integrate authenticated peer and root authorization; complete metadata/reparse filtering and production response contract |
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
2. **Root authorization**: a test-only validator now checks owner and exact protected DACL on the pinned root HANDLE, and secure registry initialization fails atomically on mismatch. Separate evidence exists in PR #19 for service-token SID verification and named-pipe client TokenUser/TokenSessionId authentication, and in PR #17 for OS-reported peer PID plus process-token/session comparison. These probes are not yet connected to this registry adapter or to one shared trusted policy source. Still required: bind those identity results to registry startup and load the authorized client identity from trusted service configuration rather than accepting a mutable workspace-root policy file. Root paths remain trusted service configuration, never request data.
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

## 2026-11-27 evidence update

- workspace_roots remains **Partial**: the rootless WORKSPACE_ROOTS request and bounded rootId-only response now exist in the native protocol/registry test harness. The message-mode pipe harness sends the request and receives the response through RESPONSE_HEADER/DATA/END with ACKs. It returns stable IDs only, not absolute paths. It is not authenticated, does not validate root owner/DACL, and is not wired to production ROOTS or a UI.
- file_read has a test-only end-to-end synchronous message-pipe vertical slice. The full production async frame transport and caller authorization are not integrated.
- The overlapped message ACK prototype now has a focused TX/RX path: normal header ACK read, DATA ACK timeout, heap-reaper handoff, and terminal completion cleanup. The reaper now waits with GetOverlappedResult(..., TRUE) before freeing pending resources. This remains a per-timeout thread prototype.
- Local test counts: root registry/request contexts 72; REQUEST/ERROR contracts 69; message-mode frame transport 123; overlapped message-mode ACK/deadline/reaper 84. All thirteen native test commands passed locally on Windows x64/MSVC.
- Current PR #20 head at time of this note is 4e53bf6231719db35449a965893a8c393ee4886b. CI run #69 (45980705461) is still in progress for preceding code head 4aae5b6348d8f8d3e18346296899da57d542ee9b; docs-only head may have a separate CI run.
- No operation has crossed the Production acceptance rule. All 15 registered production operations/modes remain NOT ACCEPTED; Phase 1 remains IN PROGRESS / NOT ACCEPTED.

- workspace_list now has a test-only aggregate adapter: independent identity-verified HANDLE view per root; bounded first page (up to 64 entries per root); rootId and relative names only; per-root failures normalized and isolated; active registry reference prevents shutdown during enumeration. Truncation is reported, but no continuation/session token exists.
- Root registry/workspace_list suite now passes 83 assertions. This extends workspace_list to Partial evidence only; it is not production protocol dispatch or acceptance.
- Cross-cutting root security now has a native test-only validator for owner SID and exact protected DACL on the already-open HANDLE; secure registry init fails atomically on a bad root. 31 local assertions cover valid and adversarial descriptors. This is Partial evidence only: actual service identity verification, trusted client.policy loading and pipe peer token/session binding are not integrated.
## 2026-11-27 superseding root-security and workspace-list evidence

- Root owner/DACL validator now has 31 native assertions, including a restricted-token integration check: inherited child-file read succeeds, file write is denied, the client can create an unrelated file in a separate writable directory, and hard-link creation into the protected root is denied with the original file remaining single-link.
- This still does not bind the configured client SID to the OS-authenticated pipe peer. The test uses a synthetic service/client SID policy fixture; actual service token, client PID/token/session, and client.policy integration remain open.
- workspace_list now has independent per-root HANDLE views, identity checks, bounded first pages, per-root failure isolation and truncation indication. The missing continuation/session semantics and concurrent mutation policy remain acceptance blockers.
- No production filesystem operation is accepted. Phase 1 remains IN PROGRESS / NOT ACCEPTED; Production remains NO-GO.
- Identity evidence cross-reference: PR #19's isolated service probe verifies its service SID in the actual service token and impersonates the named-pipe client to compare TokenUser and TokenSessionId against its policy; PR #17 separately verifies the OS-reported named-pipe peer PID against an owned child process and compares both process tokens/sessions. These remain separate test probes, not one integrated authorization path.
- Review note: PR #19 currently reads client.policy from the pinned workspace root before the final exact root-DACL validation. Although the later validation rejects a broad/unknown DACL before serving requests, the policy source is not yet an independently trusted service configuration. Resolve this trust chain before any production use.