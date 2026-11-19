# NtCreateFile absolute vs RootDirectory-relative ACL comparison — 2026-11-02

## Purpose

Test whether the unresolved FILE_READ_ATTRIBUTES denial behavior depends on opening a file by an absolute NT path or by a leaf name relative to an already-open parent directory handle, as used by the native prototype. Keep the target file, DACL, desired access, sharing, disposition, and core create options identical.

## Harness

Script: `.native-probe/metadata-acl-absolute-vs-relative-20261102.ps1`.

The script creates one disposable file on the local temp volume and applies a protected DACL:
- explicit user DENY FILE_READ_ATTRIBUTES (0x80);
- explicit user ALLOW SYNCHRONIZE (0x00100000);
- SYSTEM Full Control.

It verifies AccessCheck(FILE_READ_ATTRIBUTES), opens the parent directory once, then compares absolute-path and RootDirectory-relative NtCreateFile opens against the same file and DACL. Both paths request FILE_READ_ATTRIBUTES|SYNCHRONIZE (0x00100080), FILE_OPEN, and identical share/access settings. NtQueryObject reports the actual granted access.

The prototype's object attributes are 0x1040 (OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE) and its final-file create options are 0x00200060 (FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | FILE_NON_DIRECTORY_FILE). The harness also toggles OBJ_DONT_REPARSE, OBJ_CASE_INSENSITIVE, FILE_OPEN_REPARSE_POINT, and FILE_SYNCHRONOUS_IO_NONALERT individually while keeping each absolute/relative pair identical.

## Harness validation

- PowerShell parser: PASS.
- Script execution under Raya MCP High-integrity token: PASS.
- Target fixture was disposable and removed by the script's finally block.
- No workspace or persistent ACL was changed.

## High-integrity smoke results

- Integrity: High (S-1-16-12288).
- AccessCheck(FILE_READ_ATTRIBUTES): denied, granted 0.
- Parent directory handle: opened.
- All six flag configurations opened successfully through both absolute and RootDirectory-relative paths.
- Every successful handle reported GrantedAccess 0x00100080.
- No absolute-vs-relative difference was observed in this High-integrity run.

This is only a harness smoke/characterization run. It is not a substitute for the user's Medium-integrity acceptance run.

## Required Medium-integrity acceptance

Run from the already-verified ordinary Medium-integrity PowerShell session. The output must show S-1-16-8192. Compare the same six rows:
- absolute and relative open status;
- NTSTATUS;
- actual GrantedAccess.

If Medium also shows no difference, the absolute-vs-relative distinction is unlikely to explain the observed metadata ACL anomaly on this host. The result remains host/filesystem-specific and does not establish a general Windows rule.

## Safety boundary

The script changes only the DACL of its own disposable temporary file and removes the fixture directory in a finally block. It does not modify the native prototype, Production handlers, workspace roots, services, tunnels, runtime settings, or persistent ACLs. Do not change Production based on these results.
