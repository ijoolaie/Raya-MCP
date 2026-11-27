# Root and parent HANDLE pinning probe (test-only)

Date: 2026-11-27

Status: isolated native Windows NTFS characterization. No service, Broker, or Production wiring.

## Test

The harness creates a unique disposable directory tree under the current user's temporary directory. It opens the root directory and a child directory as HANDLEs, then:

1. Renames the configured root path while retaining the root HANDLE.
2. Creates a replacement directory at the original root path with decoy content.
3. Opens a child directory relative to the retained root HANDLE.
4. Renames that child path while retaining the child HANDLE and creates a replacement child directory with decoy content.
5. Opens and reads files using `NtCreateFile` with `OBJECT_ATTRIBUTES.RootDirectory` set to the retained HANDLE and `OBJ_DONT_REPARSE` / `FILE_OPEN_REPARSE_POINT`.
6. Validates the opened regular-file HANDLE's type, reparse attribute, and `FILE_STANDARD_INFO.NumberOfLinks == 1` before reading.

The expected result is that the root-relative and parent-relative opens read the original pinned directory objects, not the replacement-path decoys.

## Validation on 2026-11-27

MSVC x64 build passed with `/W4 /WX`. Ten fresh-process runs passed; each reported 11 PASS, 0 FAIL (110 assertions total). Each run removed its uniquely named fixture tree, and a post-run scan confirmed no `RayaRootPin-*` fixtures remained. The build script removes the generated executable and object file after the runs.

## Limits

- One Windows x64 host and local NTFS only.
- The harness runs under the interactive user. It does not test the dedicated Broker service identity, protected root DACL/owner, cross-user hard-link exclusion, or concurrent malicious namespace mutation by a principal with protected-root write rights.
- It characterizes root/parent HANDLE pinning and one relative regular-file open. It does not implement the full operation matrix, directory enumeration, create/publish, rename/move, delete, edit/replace, or a Production adapter.
- The `NumberOfLinks == 1` check is admission-time only; this test does not close the check-to-use or mutation-commit hard-link race for a same-identity attacker.
- No persistent service, ACL, user, or workspace change is made.
- Phase 1 remains IN PROGRESS / NOT ACCEPTED; Production remains NO-GO.

No runtime package imports this directory.
