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
concurrent rename/delete requiring incompatible sharing is denied while the chain
is retained.

The root HANDLE is borrowed, must already refer to an authorized directory, and is
validated again by this helper. The helper does not authorize a caller, choose a
workspace root, open a final file, or provide a complete file-operation policy.
It is not yet integrated into Broker v2 or any application handler.

## Tests

- `build-and-test.cmd`: MSVC x64 lexical path tests (25 cases).
- `build-handle-chain-tests.cmd`: MSVC x64 tests for nested HANDLE-relative
  directory opens, rejection of traversal syntax, blocking a parent rename while
  the chain is held, and rejection of a directory reparse point.

The reparse test creates a disposable directory junction using `mklink /J`.
Failure to create that fixture is reported as a skip; it does not count as a
successful reparse rejection test.

## Not yet established

This is not production acceptance. Additional work is required for final-component
file opens, same-HANDLE regular-file validation, hard-link policy, directory
enumeration, cancellation/overlapped-I/O lifetime, protocol framing, resource
limits, and adversarial race testing across supported Windows versions.
