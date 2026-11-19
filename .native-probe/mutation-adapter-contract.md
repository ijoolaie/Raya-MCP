# Test-only mutation adapter contract — 2026-10-31

Status: DESIGN / NOT IMPLEMENTED / NOT ACCEPTED. This document defines the minimum contract for future HANDLE-relative mutation prototypes. It does not authorize Production changes.

## Shared ownership and path rules

- The caller supplies only an opaque request-scoped parent HANDLE owner and one validated leaf name; never an absolute or relative multi-component path to a mutation primitive.
- The parent HANDLE must have been opened from the registered root chain, held for the entire operation, and verified as a plain directory (not a reparse point).
- Leaf names must reject empty, dot, dot-dot, separators, colon, alternate stream syntax, and unsupported Win32 names. No primitive may traverse a child path.
- Open the leaf with a parent-relative native open and explicit reparse-point policy. Reparse leaves fail closed.
- No global HANDLE cache. Ownership is explicit and consumed exactly once. Every temporary HANDLE closes on success, error, collision, cancellation boundary, and post-commit reporting.
- Native calls are synchronous and cannot be force-cancelled while inside the kernel. Cancellation is cooperative before acquisition and after return; a committed mutation must be reported as committed even if a later close/status check fails.

## Rename / move primitive contract

Proposed test-only export: `renameChildRelative(sourceParent, sourceLeaf, destinationParent, destinationLeaf, options)`.

- Source and destination parent HANDLEs are independently owned request-scope handles; they must resolve to the same volume. Cross-volume move is rejected before mutation (no copy/delete fallback).
- Destination policy is explicit: default is no-overwrite. Use the Windows rename information class with destination RootDirectory set to the destination parent HANDLE, not a reconstructed path.
- Source leaf is opened relative to source parent with DELETE + FILE_READ_ATTRIBUTES + SYNCHRONIZE, FILE_OPEN_REPARSE_POINT, OBJ_DONT_REPARSE; verify it is a regular file (or separately specified directory case) and not a reparse point.
- Destination collision must be atomic and preserve destination bytes. Do not emulate no-overwrite with a preflight existence check.
- The operation result distinguishes `not_committed`, `committed`, and `committed_but_cleanup_failed`. Never report a clean failure if the rename already committed.
- Tests: same-parent rename; distinct parent handles within one root; existing destination file and directory; source missing; source leaf junction; destination parent replacement by outside junction after HANDLE acquisition; source parent replacement; cross-volume rejection; injected failure before syscall; injected close failure after successful syscall; all HANDLE owners return to their pre-operation count.

## Replace / edit primitive contract

Proposed test-only exports: `openChildForUpdateRelative(parent, leaf, options)` and/or `replaceChildRelative(parent, leaf, bytes, options)`.

- Never write through a path resolved earlier in JavaScript. Open existing target relative to held parent with OPEN_REPARSE_POINT and no-follow semantics; verify regular file and reject reparse points.
- Preserve current file until commit. Write new bytes to a uniquely named temporary sibling created with FILE_CREATE relative to the same parent HANDLE; write bounded bytes, flush as required, then atomically rename the temporary sibling over the target using the explicit replacement policy.
- If the product policy requires no overwrite, use a no-replace operation. If edit semantics require replacement, replacement must be explicit and atomic; never silently overwrite a newly appeared target.
- Preserve or explicitly define metadata semantics (ACL/security descriptor, attributes, timestamps, alternate streams). Do not claim preservation until tested.
- On pre-commit error, original target bytes remain unchanged and temp sibling is removed. On post-commit cleanup/close error, report committed state and do not attempt unsafe rollback.
- Tests: existing file replacement; target appears after initial lookup; target becomes junction; temp collision; write short/failure; flush failure; rename collision; failure after commit; parent path replaced by outside junction; temp cleanup; metadata behavior; HANDLE accounting.

## Atomic publication versus existing-file replacement — 2026-10-31

Microsoft's documented rename contract exposes a destination-relative RootDirectory HANDLE and a ReplaceIfExists boolean. The extended rename information class adds POSIX replacement semantics, including replacement while other handles to the old target remain open; it does not provide an expected destination FileId / compare-and-swap condition. Therefore, opening a target, checking its FileId, and later renaming a temporary sibling over the destination still has a namespace race: another process can replace the destination between the check and commit. The prototype must not claim identity-safe atomic edit based on that sequence. References: https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info and https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_rename_information.

A safe subset is now implemented as a separate test-only native addon: `.native-probe/handle-relative-publish-probe.c`, built by `.native-probe/build-raya-phase1-publish.cmd`. Export `publishChildExclusiveRelative(parent, tempLeaf, destinationLeaf, buffer)` creates a unique temporary sibling using parent-relative `NtCreateFile(FILE_CREATE)`, rejects reparse parents and unsafe names, accepts at most 1 MiB, writes and flushes the temporary file, then publishes it with parent-relative `NtSetInformationFile(FileRenameInformation)` and `ReplaceIfExists=FALSE`. Existing destinations are never overwritten. Pre-commit failures attempt to mark the temporary HANDLE for deletion; a post-commit close/status failure is reported as `committed_but_cleanup_failed`. This is atomic create-if-absent publication, NOT replacement/edit of an existing file.

The test harness `.native-probe/handle-relative-publish-test.cjs` is registered as `npm run test:native-publish-exclusive`. It passed 9 checks: content publication and flush; existing destination collision preservation and temp cleanup; temp-name collision; injected write and flush failure cleanup; unsafe/identical names and 1-MiB bound; post-commit status distinction; parent path rename plus outside junction replacement; and parent HANDLE accounting. The write/flush faults are injected before the corresponding system calls; they do not simulate a partial kernel write or real storage failure. Added `.native-probe/handle-relative-publish-worker.cjs` and `.native-probe/handle-relative-publish-worker-stress-test.cjs`, registered as `npm run test:native-publish-workers`. The stress harness ran five rounds with four independent worker processes per round (20 workers total): exactly one worker atomically published the shared destination in every round, the other three received no-overwrite collisions, winner bytes remained intact, losing temporary siblings were cleaned, and every worker's HANDLE count returned to its own baseline. The harness reported 20 round-level assertions. This verifies create-if-absent contention only; it does not add replacement semantics or simulate partial kernel/storage failures.

Decision: do not implement or integrate atomic replacement of an existing file until an identity-conditional commit strategy is proven. The current safe publish primitive can support new-file creation only. Production `edit_block` remains path-based and unchanged; Production integration remains NO-GO.

## Directory creation boundary

The existing exclusive directory primitive is a separate operation and does not make the Production `create_directory` handler safe. Future migration must walk/create each missing component relative to the previously held parent HANDLE, never call path-based recursive mkdir.

## Acceptance gates

1. Prototype is isolated under `.native-probe`; no Production imports or handlers.
2. Deterministic race harnesses use separate worker processes and synchronized barriers.
3. Both in-root and outside canaries are verified unchanged as appropriate.
4. Every branch checks HANDLE accounting and close-failure reporting.
5. Focused tests, `npm run lint`, and `npm test` pass.
6. Live ACL permission-denied acceptance remains a separate required gate.
7. No Production migration until all required operation families and cancellation behavior are accepted.

## Current source assessment

The current addon exposes parent-relative read, exclusive file create, and exclusive directory create. It does not currently expose HANDLE-relative rename, atomic replace/edit, or general write. Production `move_file`, `edit_block`, `create_directory`, `read_multiple_files`, and `list_directory` remain path-based. Therefore the operation-family security acceptance is incomplete.

## Conditional replacement decision — 2026-10-31

Decision: **no safe identity-conditional atomic replacement primitive has been established using the documented Windows APIs reviewed for this prototype.** FileRenameInformation / FileRenameInformationEx choose the destination by parent-relative name and expose replace/no-replace policy, but no expected destination volume+FileId compare-and-swap field. ReplaceFileW also selects the replaced object by pathname; its documented open uses FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, so it is not an identity-conditional commit primitive. Do not implement a preflight FileId check followed by either replacement API and do not claim that it closes the namespace race.

A restrictive-share HANDLE to the existing target can pin the opened object and prevent later opens requesting incompatible write/delete access while that HANDLE remains open. However, that HANDLE-based strategy does not make a sibling rename-over-target commit possible while the target is locked, and writing through the pinned target HANDLE is in-place mutation, not atomic replacement. In-place writes can expose partial content to readers and can leave partial content after process/storage failure. It therefore does not satisfy the existing contract requirement that original bytes remain intact until commit.

The current strict contract consequently has no acceptable implementation for atomic edit of an existing leaf. Keep the safe subset:
- New-file publication: supported by publishChildExclusiveRelative with atomic create-if-absent semantics.
- Existing-file atomic edit: unsupported / fail closed until an actual identity-conditional primitive is demonstrated on the target filesystem.
- Existing-file in-place edit: a distinct, weaker product contract would be required; it must explicitly accept non-atomic visibility and partial-write/durability risk, and must not be represented as atomic replace.

Potential future directions are research items, not accepted solutions: a filesystem/provider-specific transactional or conditional namespace primitive with a documented identity guarantee; or an explicitly cooperative locking model where every writer participates (not sufficient against a hostile/non-cooperating process). TxF or oplock-based assumptions must not be treated as a security boundary without current platform support and adversarial proof.

This decision does not change Production. edit_block remains path-based and unaccepted against TOCTOU. Phase 1 remains IN PROGRESS / NOT ACCEPTED; Production remains NO-GO; live ACL permission-denied acceptance remains open.



## Rename ACL acceptance — 2026-11-02

Added .native-probe/handle-relative-rename-acl-test.cjs and npm run test:native-rename-acl. The test applies explicit deny ACEs only to disposable fixture entries for the current user SID and verifies the exact ACEs with icacls:
- Destination directory FILE_ADD_FILE (WD) deny: renameChildRelative fails with STATUS_ACCESS_DENIED (0xC0000022); source remains at its original name and no destination is created.
- Source file DELETE (D) deny: source open for the rename fails with STATUS_ACCESS_DENIED; source name remains and no destination is created.
- After removing only those two fixture deny ACEs, both rename operations succeed and the expected content is present at the destination.
- Explicit parent HANDLE count returns to baseline; fixture is removed.

Verification on this host: npm run test:native-rename-acl exit 0; npm run test:native-rename-relative exit 0 (15 checks); npm run test:native-rename-worker-stress exit 0 (5 rounds, 20 workers, 120 assertions); npm run lint exit 0; npm test exit 0 (42 core, 34 format/config, HTTP authentication/initialize, 36 URL-security checks).

This is evidence only for the tested native rename prototype, current Windows user SID, and local NTFS fixture. It does not validate all Windows providers/tokens or close the separate unresolved metadata FILE_READ_ATTRIBUTES issue. The strict hard-link policy remains undecided. The rename addon and production move_file handler remain separate; no Production handler was changed or accepted.


## Directory rename prototype — 2026-11-02

A separate clone of the file-only rename addon now tests directory moves without changing the original file rename addon: .native-probe/handle-relative-rename-directory-probe.c, built by .native-probe/build-raya-phase1-rename-directory.cmd as a distinct N-API binary. The source leaf is opened relative to the held parent with FILE_OPEN_REPARSE_POINT but without FILE_NON_DIRECTORY_FILE; the opened object is then checked for reparse status and its actual directory bit. The result reports sourceIsDirectory. It supports plain files and directories in this isolated candidate; no Production imports.

The registered test npm run test:native-rename-directories passed 12 checks on local NTFS:
- Empty directory rename and non-empty nested directory tree move.
- Existing destination directory and destination file are not overwritten.
- Directory reparse leaf rejected without following target.
- Missing and unsafe names fail closed.
- Destination FILE_ADD_SUBDIRECTORY (AD) deny and source directory DELETE (D) deny both return STATUS_ACCESS_DENIED; both operations succeed after fixture ACE restoration.
- Held source and destination parent HANDLEs remain bound after their pathnames are replaced by outside junctions.
- Cross-volume directory move rejected before mutation.
- Process HANDLE count returns to baseline.

This is a focused functional/ACL/namespace acceptance slice, not a multi-worker directory-rename stress result, not provider portability evidence, and not Production move_file acceptance. The file-only rename addon remains unchanged. Production handlers remain path-based; Phase 1 remains not accepted.


## Directory rename multi-process namespace stress — 2026-11-02

Added a separate worker, synchronized race test, and five-round stress runner for the directory-capable candidate:
- .native-probe/handle-relative-rename-directory-worker.cjs
- .native-probe/handle-relative-rename-directory-worker-race-test.cjs
- .native-probe/handle-relative-rename-directory-worker-stress-test.cjs
- npm run test:native-rename-directory-workers

Each round starts four independent Node workers. Every worker opens and holds both source and destination parent HANDLEs and signals readiness. The controller then renames both parent directories and installs outside junctions at their former paths before releasing the workers. All workers must move their nested directory into the original held destination directory, preserve its canary content, leave both outside sentinels unchanged, avoid publishing into outside paths, and close both parent HANDLE owners.

Results: one round passed with 4 workers / 36 assertions; the five-round stress passed with 20 workers / 180 assertions. This is local NTFS evidence for the isolated directory candidate only; it is not a Production move_file acceptance or provider portability result.


## Rename cancellation/lifecycle boundary — 2026-11-02

Added .native-probe/handle-relative-rename-cancellation-test.cjs and npm run test:native-rename-cancellation. The test-only wrapper checks AbortSignal before entering the synchronous native rename. A pre-aborted signal leaves source and destination unchanged. A setImmediate cancellation queued before the synchronous native call cannot run until the call returns; the rename commits and the wrapper preserves the committed result rather than converting it into AbortError. An injected post-commit close/status failure remains reported as committed_but_cleanup_failed even with cancellation queued. Parent HANDLE count returns to baseline.

The test passed 4 checks. This documents the actual Node event-loop boundary for this synchronous prototype; it does not prove mid-kernel cancellation (which is not available here) or MCP/HTTP transport cancellation semantics. Integration must check cancellation before starting, then preserve commit state after the native call. Never claim that an in-flight NtCreateFile/NtSetInformationFile can be force-cancelled.
