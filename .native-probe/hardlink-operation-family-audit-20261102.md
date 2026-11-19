# Hard-link policy operation-family audit — 2026-11-02

## Decision
The user approved a strict regular-file single-link policy: a regular file is admissible only when `FILE_STANDARD_INFO.NumberOfLinks == 1` on the opened file HANDLE. Unknown, unavailable, or greater-than-one counts must fail closed. Legitimate in-workspace hard links are intentionally unsupported.

This audit is source inspection only. It does not claim acceptance and does not modify Production.

## Production source reviewed
File: `advanced-tools.mjs`.

The current implementation still resolves workspace paths in JavaScript and then calls path-based Node filesystem APIs. No native HANDLE-relative adapter or NumberOfLinks check is present in these handlers.

| Tool / helper | Current Production behavior | Strict hard-link requirement | Status |
|---|---|---|---|
| `get_file_info` | `securePath` then `fs.lstat(target)`; returns metadata | Open final regular file relative to held parent HANDLE; reject nlink != 1 before exposing metadata | NOT IMPLEMENTED |
| `read_multiple_files` | `securePath`, `fs.stat`, then `fs.readFile(path)` | Open by held parent HANDLE, validate regular/non-reparse and nlink == 1 on same HANDLE, enforce size/read limits, read through that HANDLE | NOT IMPLEMENTED |
| `read_file_tail` | `securePath`, `fs.stat`, then `fs.open(path)` and positional read | Same HANDLE must supply type, nlink, size and bytes; reject unknown/multi-link | NOT IMPLEMENTED |
| `list_directory` | `walk` uses path-based `fs.readdir` and Dirent type flags | Handle-relative directory enumeration; for every regular-file entry admitted/reported, open and validate nlink == 1; reject/omit only under an explicit documented error contract, never silently misclassify | NOT IMPLEMENTED |
| `start_search` | Uses `walk`; content mode reads matching files by path | Filename and content results must follow an explicit policy; content reads require same-HANDLE nlink validation; filename/metadata results must not bypass the approved regular-file policy | NOT IMPLEMENTED |
| `move_file` | `securePath` + ancestor checks + destination preflight + `fs.rename(path,path)` | Open source relative to held source parent; if regular file, validate nlink == 1 on source HANDLE and prove link-count stability through commit; directories use separate directory semantics | NOT IMPLEMENTED |
| `edit_block` | Path-based stat/read, then temporary path write/rename | Existing-file atomic identity-conditional replacement is currently unsupported; fail closed. Any future in-place contract requires separate user approval. Strict nlink validation alone does not make replacement atomic | NOT IMPLEMENTED |
| `create_directory` | Path-based `fs.mkdir(target,{recursive})` | No regular-file leaf is created, but recursive traversal is still a path-race issue; use component-by-component held-parent HANDLE creation. Any regular-file metadata encountered must obey the strict rule | NOT IMPLEMENTED |
| `walk` helper | Path-based recursive `fs.readdir`; descends based on Dirent flags | Must be replaced by handle-relative traversal for race-safe list/search; Dirent flags are not a held file identity or link-count proof | NOT IMPLEMENTED |

## Isolated candidate coverage found
File: `.native-probe/handle-relative-hardlink-guard.c`.

Source inspection found NumberOfLinks checks in:
- final component of the candidate root-relative open chain;
- `inspectChildFromParent`;
- `readChildFromParent`.

The candidate uses a separate root-pinning build for `openRelative`. The recorded local NTFS tests reject pre-existing multi-link and outside-backed aliases and detect a synchronized link addition before returning read bytes. These are limited to those three APIs and their test schedules; they do not prove all interleavings race-free.

The file-only rename candidate (`.native-probe/handle-relative-rename-probe.c`) opens the source with DELETE and metadata access, but does not query or enforce NumberOfLinks. The directory-capable rename clone also lacks an explicit regular-file NumberOfLinks check. No hard-link policy acceptance is recorded for rename/move.

A new, separately named file-only rename candidate was built at `.native-probe/handle-relative-rename-hardlink-probe.c`, with a separate addon output and harness. It uses FILE_SHARE_READ, rejects a source whose initial FileStandardInfo.NumberOfLinks is not 1, and rechecks the same source HANDLE immediately before the rename syscall. The 18-check harness passed on local NTFS, including:
- pre-existing two-link source rejected without mutation;
- a hard link inserted after initial open/validation detected by the pre-commit recheck, with no rename commit;
- ordinary same-parent and cross-parent rename behavior, no-overwrite collisions, reparse rejection, C:-to-D: rejection, failure/commit reporting and HANDLE baseline.

**Critical acceptance failure reproduced:** a test-only callback inserted a hard link after the final NumberOfLinks query but immediately before NtSetInformationFile. The rename syscall then committed; the destination and outside alias both referenced the same two-link file. This proves the current check/recheck approach has a check-to-syscall race. FILE_SHARE_READ does not close it; the prior cross-process experiment already showed hard-link creation can succeed while a read HANDLE is held. Therefore the new rename candidate is a characterization artifact only and is NOT ACCEPTED for regular-file move/rename. Do not integrate it. A filesystem/authorization mechanism that excludes link creation across the commit boundary, or an explicitly narrower threat model supported by evidence, is required.

The exclusive create and directory-create candidates create new entries; they do not by themselves cover reads, metadata, enumeration, rename, delete, or existing-file mutation. Atomic create-if-absent publish is a separate safe subset; it does not replace existing files.

## 2026-11-02 — share-mode and DACL exclusion investigation

The earlier cross-process experiment was independently rerun and expanded with a new standalone native reproducer:
- `.native-probe/hardlink-share-repro-v2.c` and `.native-probe/hardlink-share-repro-v2.cmd`.
- MSVC x64 build and execution completed with exit code 0 on the local NTFS volume.
- Across five cases, a child process successfully created a hard link while the parent held the source file with share mode 0 and access GENERIC_READ; share mode 0 and FILE_READ_ATTRIBUTES|SYNCHRONIZE; share mode 0 and FILE_READ_DATA|SYNCHRONIZE; FILE_SHARE_READ; and FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE. FileStandardInfo / BY_HANDLE_FILE_INFORMATION link count changed from 1 to 2 in every case.
- This directly conflicts with the CreateHardLinkW documentation statement that a handle opened without sharing prevents another application from sharing the file by creating a hard link. The local NTFS behavior is empirically repeatable; do not rely on share modes to exclude hard-link creation. See Microsoft Learn CreateHardLinkW remarks: https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createhardlinkw.

A second disposable-fixture experiment:
- `.native-probe/hardlink-acl-repro-v1.c` / `.cmd` built and ran successfully on NTFS.
- Adding an explicit current-user DENY FILE_WRITE_ATTRIBUTES ACE caused cross-process CreateHardLinkW to fail with Win32 error 5 (ACCESS_DENIED); no link was created.
- The same-identity child could then replace the fixture DACL with a NULL DACL and create the hard link successfully. The parent restored the original DACL (SetNamedSecurityInfo returned 0) and removed the fixture.
- The parent also verified that the same token could open the file with WRITE_DAC. Thus a per-file DENY WRITE_ATTRIBUTES ACE is a useful access gate only while the attacker cannot rewrite the DACL or take ownership; it is not a durable boundary against the file owner/current identity as presently configured. Do not apply ACL changes to workspace files or Production as part of this phase.

Conclusion: neither share-mode pinning nor a simple per-file DENY WRITE_ATTRIBUTES ACE currently proves exclusion across the commit boundary for Raya's same-user threat model. A separate service identity/ownership model with protected DACLs could be investigated as an architectural option, but it would materially change file ownership and access semantics and requires explicit user approval. Until then, regular-file rename/move remains NOT ACCEPTED; strict NumberOfLinks checks at HANDLE admission remain mandatory for read/inspect/open paths, with the documented race limitation. Production remains NO-GO.

## 2026-11-02 — publish, read-tail, metadata and enumeration policy gaps

A separately named publish characterization candidate was built and run:
- `.native-probe/handle-relative-publish-hardlink-probe.c`, `build-raya-phase1-publish-hardlink.cmd`, and `handle-relative-publish-hardlink-test.cjs`.
- The candidate deliberately opens the staging file with ShareAccess=0, then a test-only callback creates a hard link to the staging leaf while the native HANDLE remains open.
- MSVC x64 build exit code 0; the 10-check harness exit code 0. The candidate atomically published the staged bytes, but both the published destination and the outside alias referenced the same file and NTFS reported NumberOfLinks=2. Therefore atomic create-if-absent publication is not yet accepted under the approved strict hard-link policy. ShareAccess=0 does not save it.

A separate existing rootapi candidate characterization:
- `.native-probe/handle-relative-rootapi-hardlink-policy-test.cjs` used the already-built isolated rootapi addon and an outside file hard-linked into the workspace.
- `readChildFromParent` returned the outside-backed file bytes; `readTailChildFromParent` also returned those bytes.
- `inspectEntryMetadata` returned metadata with `numberOfLinks=2` rather than rejecting it.
- `enumerateDirectoryPage` returned the alias as an ordinary non-directory, non-reparse file entry and did not include NumberOfLinks.
- Harness exit code 0. This is a characterization of missing policy enforcement in that isolated rootapi candidate, not a Production test or acceptance. The separate hardlink-guard candidate's guarded read/inspect/openRelative APIs have their own narrower tests, but do not cover all these adapter variants.

Consequences:
- Read-tail is not covered by the currently accepted hardlink-guard API set.
- Metadata must reject a regular file when the same opened HANDLE reports NumberOfLinks != 1; merely returning the count is insufficient.
- Directory enumeration must open each regular-file entry relative to the held directory and validate the same file HANDLE before reporting it; FileIdBothDirectoryInfo records do not supply NumberOfLinks.
- Publish must validate the staging HANDLE and still address the post-check race; the current candidate is not accepted.
- These results expand the operation-family matrix; they do not change Production status. No Production handler or workspace ACL changed.

## Required next implementation/test slices (test-only)
1. Define one shared native helper that queries FileStandardInfo.NumberOfLinks from the already-open regular-file HANDLE and fails closed on query error or count != 1. Keep directory handling separate.
2. Extend the isolated file rename candidate: reject pre-existing multi-link source files and characterize link creation between open, validation, and commit. If the race cannot be excluded, keep rename of regular files NOT ACCEPTED rather than relying on a stale count.
3. Review delete and publish adapters, including any source/temp/destination regular-file handles; prove where the strict rule applies and where newly created files are single-link by construction.
4. Build held-HANDLE directory enumeration and search adapters; every returned regular-file result must have a verified single-link identity. Do not trust Dirent or path-based stat.
5. Keep existing-file edit unsupported / fail closed until identity-conditional atomic replacement is proven.
6. Add positive single-link, pre-existing multi-link, outside-backed alias, synchronized concurrent hard-link insertion, ACL denial, invalid/reparse, cancellation, cleanup, HANDLE accounting, and worker-process stress tests per applicable operation.
7. Retest on supported providers before claiming portability. Current evidence is local NTFS only.

## Production and phase status
No Production handler was changed. Production filesystem operations remain path-based and are NOT ACCEPTED as race-free. The metadata FILE_READ_ATTRIBUTES denial remains FAIL / UNRESOLVED even under the 2026-11-02 Medium-integrity matrix; the hard-link policy decision does not waive it. Phase 1 remains IN PROGRESS / NOT ACCEPTED; Production remains NO-GO.
