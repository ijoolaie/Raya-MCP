# NTFS hard-link alias policy probe — 2026-11-01

## Purpose and scope

This is an isolated test-only characterization of the hard-link alias finding recorded in HANDOFF.md. It does not modify or replace the existing pinned prototype or any Production handler.

Files:
- `handle-relative-hardlink-guard.c` — experimental copy of the pinned native probe source.
- `build-raya-phase1-security-probes.cmd` — builds both the existing pinned ACL-denial addon and the separate hard-link addon, using the active Node version's header/import-library cache and Visual Studio discovery; no developer-specific absolute paths are embedded.
- `handle-relative-hardlink-guard-test.cjs` — disposable NTFS fixture and assertions.

## Candidate policy tested

For regular-file reads/identity inspection:
1. Open the leaf relative to the already-held parent directory HANDLE.
2. Use `FILE_SHARE_READ` only (do not share write or delete access) while the child HANDLE is open.
3. Query `FILE_STANDARD_INFO.NumberOfLinks` from that HANDLE and reject unless it equals 1.
4. For the bounded read adapter, execute the test synchronization callback after opening the child HANDLE but before the link-count check, then validate the count before reading bytes.
5. Keep the handle open through the read; close it exactly once.

The same single-link check and read-only sharing policy was added to the experimental `openRelative` final-file open. This is not yet applied to the existing pinned prototype.

## Observed results on the current local NTFS volume

The native addon compiled successfully with Visual Studio 2026 Build Tools. The standalone test exited 0 and verified:

- A normal single-link file is readable through `readChildFromParent` and `openRelative`; `inspectChildFromParent` reports `numberOfLinks: 1`.
- A pre-existing multi-link file is rejected by read, identity inspection and `openRelative`.
- An outside file hard-linked into the workspace (two names / `nlink=2`) is rejected by all three operations.
- A hard link can still be created to an already in-root file while the read HANDLE is open. The pre-read link-count check detects the count increase and rejects the read before returning file bytes. Thus sharing mode alone does not prevent hard-link creation.
- With `FILE_SHARE_READ` only, attempts to rename the in-root name outside the workspace or unlink the in-root hard-link name while the read HANDLE is open failed with Windows sharing violations surfaced as Node `EBUSY`.
- After closing the guarded HANDLE, the disposable hard link can be created; subsequent reads reject the now multi-linked file.
- Process HANDLE count returned to its baseline after cleanup.

## Interpretation and limits

This is promising evidence for a conservative single-link policy on the tested NTFS host, but it is not a race-free proof and is not Production acceptance.

- It rejects legitimate hard links even when every name is inside the workspace; this is a deliberate compatibility cost of the conservative policy.
- It does not enumerate all hard-link names or prove that every filesystem/provider enforces the same sharing semantics.
- A new hard link can be added to an already in-root file while the read HANDLE is held; the test detects the count increase before reading, but does not establish atomicity against every possible concurrent filesystem operation.
- Only the current local NTFS volume was tested. ReFS, SMB/network shares, removable media and cloud/sync providers remain untested.
- The experimental adapter covers `readChildFromParent`, `inspectChildFromParent` and `openRelative` only. Directory metadata enumeration, other read adapters, rename/move, write/publish, recursive create and all Production handlers require separate review.

**Status:** experimental candidate only. Do not merge into Production or mark Phase 1 accepted. Keep the original outside-root escape and live ACL-denial gates open. The existing uncommitted `.native-probe/hardlink-sharing-experiment.c` was not touched.

## Additional isolated revalidation — 2026-11-27

Rebuilt both test addons from this branch on a clean isolated Windows checkout using Node.js 22.23.2 x64 and Visual Studio 2026 Build Tools 18.7.3. The portable build script exited 0 and the hard-link harness exited 0. It confirmed that hard-link creation can succeed while the read HANDLE is held, while the pre-read link-count check rejects the read; rename and removal of the in-root alias were blocked with `EBUSY`. Single-link reads succeeded, pre-existing multi-link files and outside-backed aliases were rejected, and native HANDLE count returned to baseline. The disposable fixture was removed. This is one-host characterization, not a race-free proof or Production acceptance.
