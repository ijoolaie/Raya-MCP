# Live DACL permission-denied acceptance — 2026-11-01

## Scope

A disposable Windows NTFS fixture was used to test real access checks against the current process token. The harness applies explicit deny ACEs only to the temporary fixture, exercises the existing isolated native addon, removes the deny ACEs, and verifies normal operations recover. No global, user-profile, workspace-root, service, or inherited parent ACL was changed.

Harness: `handle-relative-acl-denial-test.cjs`.

Reproducible build entry point: `build-raya-phase1-security-probes.cmd`. It discovers the installed Visual Studio C++ x64 Build Tools via `vswhere.exe` and uses the Node-gyp cache matching the active Node version. Set `RAYA_NODE_GYP_CACHE_ROOT` only when the standard per-user cache is elsewhere.
Build both required isolated addons with `.native-probe/build-raya-phase1-security-probes.cmd`; the pinned ACL addon is compiled from the already-reviewed `.native-probe/handle-relative-probe-phase1-dircreate-pinned.c`. Generated binaries remain local build artifacts and are not Production components.

## Test sequence and results

1. Create a temporary root/parent directory and a file containing `ACL-READ-CANARY`.
2. Use the current process SID to apply a temporary deny ACE on the parent directory for file-add and subdirectory-add rights (`WD,AD`).
3. Apply a temporary deny ACE on the test file for read-data (`RD`).
4. Open the parent through `openParentRelative`; parent traversal/listing remains allowed.
5. Verify:
   - `createChildExclusive` fails with `NTSTATUS 0xC0000022` (STATUS_ACCESS_DENIED).
   - `createDirectoryChildExclusive` fails with `NTSTATUS 0xC0000022`.
   - `readChildFromParent` fails with `NTSTATUS 0xC0000022`.
6. Close the parent HANDLE, remove only the explicit deny ACEs from the temporary file/directory, reopen the parent and verify read, file create and directory create succeed.
7. Verify native process HANDLE count returns to baseline and remove the disposable fixture.

The standalone harness exited 0. All three denied operations returned the expected access-denied NTSTATUS; after ACL restoration, the corresponding operations succeeded. The fixture cleanup completed.

## Interpretation and limits

This closes the live DACL denial check for these three isolated operations on the current local NTFS host. It is stronger than a mocked error: the Windows access check was enforced by the actual DACL and current process token.

It does not establish:
- ACL behavior on ReFS, SMB/network, removable or cloud/sync providers;
- all Windows privilege/token configurations or inherited/conditional ACE combinations;
- access-denied behavior for every production MCP tool or filesystem operation family;
- race-free Production integration or complete Phase 1 acceptance.

No global/user-profile ACLs were changed. No Production handler, configured ROOTS, runtime setting, service or tunnel was changed. Phase 1 remains IN PROGRESS / NOT ACCEPTED; Production remains NO-GO until all operation-family, root binding, lifecycle, provider and independent review gates pass.

## Additional isolated revalidation — 2026-11-27

Rebuilt both test addons from this branch on a clean isolated Windows checkout using Node.js 22.23.2 x64 and Visual Studio 2026 Build Tools 18.7.3. The portable build script exited 0. The live ACL harness then exited 0: file create, directory create and read each returned `STATUS_ACCESS_DENIED (0xC0000022)`; after removing only the temporary deny ACEs, read and both create operations succeeded. Native HANDLE count returned to baseline and the disposable fixture was removed. This revalidation is limited to the current Windows/NTFS host and the three operations listed above.
