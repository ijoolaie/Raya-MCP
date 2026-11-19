# Live DACL permission-denied acceptance — 2026-11-01

## Scope

A disposable Windows NTFS fixture was used to test real access checks against the current process token. The harness applies explicit deny ACEs only to the temporary fixture, exercises the existing isolated native addon, removes the deny ACEs, and verifies normal operations recover. No global, user-profile, workspace-root, service, or inherited parent ACL was changed.

Harness: `handle-relative-acl-denial-test.cjs`.
Addon: existing isolated `napi_probe_raya_phase1_dircreate_pinned.node` (not Production).

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

## Metadata ACL follow-up — 2026-11-01 (not accepted)

A separate disposable-file probe applied an explicit `icacls /deny <current-process-SID>:(RA)` ACE. `icacls` confirmed the explicit `(DENY)(RA)` entry was present for the current account. Nevertheless, `inspectChildFromParent` succeeded and returned file identity/size metadata, even though its isolated native implementation requests `FILE_READ_ATTRIBUTES | SYNCHRONIZE` when opening the child. The probe's temporary ACE was removed and the fixture deleted.

This is an unresolved observation, not evidence that metadata access is denied or that the native adapter is safe under a metadata-deny DACL. The attempted extension was not retained in the acceptance harness. Do not mark metadata ACL denial as passed. Investigate why the explicit RA deny did not block this open on the current host (including effective token/privilege and Windows access-check semantics), then design a direct, independently observable test before updating acceptance. The existing three live DACL checks remain valid as recorded above.

## Direct Win32 metadata-open cross-check — 2026-11-01 (unresolved)

To determine whether the prior RA result was specific to the pinned NtCreateFile adapter, a temporary standalone N-API probe called Win32 `CreateFileW` directly with `dwDesiredAccess = FILE_READ_ATTRIBUTES` only (0x80), all share flags, `OPEN_EXISTING`, and `FILE_FLAG_OPEN_REPARSE_POINT`. Before applying a deny ACE, the open succeeded as expected. After applying `icacls /deny` first to the exact current TokenUser SID and then in a separate fixture to `Everyone` (`S-1-1-0`), the open still succeeded. The same ACLs were independently enumerated from the file DACL: the explicit deny ACE was present with mask `0x00000080`, and its SID matched the process TokenUser SID in the user-SID case. `inspectChildFromParent` also continued to succeed. `SeBackupPrivilege` was reported disabled; the process token had the Administrators group enabled. Both deny ACEs were removed from their disposable fixtures, which were then deleted.

This shows the observation is not limited to the prototype's `NtCreateFile` path: on this host, the tested Win32 `CreateFileW(FILE_READ_ATTRIBUTES)` open also succeeded despite the observed explicit RA deny ACE. It does NOT establish that Windows universally ignores RA denies. The current token is elevated and includes Administrators; the cause remains unresolved. A separate `AccessCheck` diagnostic was inconclusive because the helper returned error 1338 (invalid security descriptor), so it is not evidence either way. Do not mark metadata ACL denial as passed or generalize this result to other tokens/providers. Next: reproduce under a non-elevated standard-user token (without changing account or global permissions unless explicitly approved), and validate the security-descriptor/access-check helper independently before drawing a Windows ACL conclusion. Temporary probe sources/build outputs were removed; no new test harness or Production source was retained.
