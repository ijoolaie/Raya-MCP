# Broker v2 relative-path and HANDLE-chain research

This directory contains test-only research components. Nothing here is connected to the
Raya Local MCP production handlers or installed as a persistent service.

## Lexical path gate

`relative-path.c` rejects rooted paths, alternate separators, empty components,
dot components, Win32-invalid characters, trailing dots/spaces, alternate data
stream syntax, and reserved DOS device names. It is a lexical check only.

## HANDLE-relative directory chain

`handle-chain.c` takes a borrowed, already-authorized root directory HANDLE and a
validated relative directory path. It opens each component individually using
`NtCreateFile` with the preceding directory HANDLE as `OBJECT_ATTRIBUTES.RootDirectory`.
Each open requests a directory, uses `OBJ_DONT_REPARSE` and
`FILE_OPEN_REPARSE_POINT`, and validates directory/reparse/delete-pending state
through the returned HANDLE. The chain retains every opened directory HANDLE until
the caller closes it. Intermediate handles are opened with read sharing only so a
concurrent rename/delete or child creation requiring incompatible access is denied
while the chain is retained.

## Final regular-file open

`raya_open_single_link_regular_file` opens one validated path component relative
to a pinned parent directory HANDLE. It requests a non-directory open, does not
follow reparse points, and validates the resulting HANDLE for disk-file type,
non-directory/non-reparse status, non-delete-pending state, nonnegative bounded
size, and exactly one hard link. The returned HANDLE is the one intended for the
subsequent read; the caller owns and must close it.

The link count is an observation made on the opened file HANDLE. Windows permits
a hard link to be created after that check, even while this read HANDLE remains
open. The test demonstrates that the existing HANDLE continues to reference the
same file object and that a subsequent open rejects the now-multi-link file.
This helper therefore does not claim to prevent all future link-count changes or
provide a volume-wide hard-link prohibition. Whether that residual behavior meets
the final product policy remains an explicit security acceptance question.

The root HANDLE is borrowed, must already refer to an authorized directory, and is
validated again by the helper. The helper does not authorize a caller, choose a
workspace root, enumerate a directory, or provide a complete file-operation policy.
It is not yet integrated into Broker v2 or any application handler.

## Tests

- `build-and-test.cmd`: MSVC x64 lexical path tests (25 cases).
- `build-handle-chain-tests.cmd`: MSVC x64 tests for nested HANDLE-relative
  directory opens, traversal rejection, rename blocking, final-file open,
  size cap, directory rejection, hard-link detection, active-handle stability
  after link-count change, and directory-junction rejection.
- File symbolic-link creation is attempted when Windows policy permits it;
  otherwise the test reports an explicit skip. Directory junction creation is
  independently tested.

## Next protocol work

`read-protocol-design.md` records a proposed bounded multi-frame read protocol and the separate unresolved overlapped-I/O cancellation lifetime model. It is a design note only; neither is implemented or integrated.

## Not yet established

This is not production acceptance. Additional work is required for directory
enumeration, response framing, cancellation/overlapped-I/O lifetime, resource
limits, broader adversarial race testing across supported Windows versions, and
a decision on the residual hard-link mutation behavior described above.