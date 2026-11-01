# Root-chain hardening — isolated prototype next step

Date: 2026-10-26
Scope: `C:\Users\Ali\local-mcp-bridge` only.
Status: implemented in `.native-probe` only; production handlers and dependencies unchanged.

## Implementation update

The isolated native probe now opens the drive root first, then walks configured-root components relative to held directory handles using `NtCreateFile`, `OBJ_DONT_REPARSE`, directory-only options, and opened-handle attribute checks. The configured root is not opened with one full-path `CreateFileW(root)` call anymore. This is still an experimental prototype, not production integration or a security acceptance claim.

## Implemented isolated algorithm (drive-absolute paths only)

1. Parse and validate the configured root as a drive-absolute local path. Reject UNC, device namespace, volume GUID, alternate data stream syntax, embedded NUL, empty components, `.`, `..`, and ambiguous slash forms. Normalize separators only after validating the accepted syntax.
2. Open the drive root (for example `C:\\`) as a directory handle with directory semantics and reparse-point inspection enabled. Confirm it is a directory and retain the handle.
3. Split the remaining configured-root path into components. For each component, call the same reviewed handle-relative open primitive with the current directory handle as `RootDirectory`, `OBJ_DONT_REPARSE`, and directory-only open options. Query attributes from the returned handle; reject reparse points and non-directories.
4. Keep the child handle before closing the previous handle. On every error, close every acquired handle exactly once and fail closed.
5. The final handle is the configured root identity. Descendant operations must use this handle, not reconstruct the configured root as a path.
6. Record the volume identity for the opened root and verify that any later cross-handle operation obeys the documented same-volume policy. Do not treat a path-derived drive letter as volume identity.
7. Keep this in `.native-probe` until tests pass; do not wire it into production handlers or package scripts.

## Required tests before calling root-chain walking acceptable

- Ordinary drive root and nested configured root, with and without a trailing separator.
- Reject relative, UNC, device, volume-GUID, ADS, dot, dot-dot, repeated-separator, and malformed paths.
- Place a junction at each ancestor level in a disposable fixture where the platform permits it; verify the walk rejects it before the final configured-root handle is returned.
- Root path missing, inaccessible component, non-directory component, dangling junction, and path component replaced during the walk. The configured-root-chain component-swap case is now covered by a synchronized isolated test: after the `root-race` directory handle is opened, the test renames the next `branch` component and installs an outside junction at its old name; the walk must fail closed.
- Verify all handles are closed on every failure path and repeated failure does not leak handles.
- Confirm that the configured root's final handle refers to the expected fixture identity; do not use a fresh path lookup as the sole assertion.
- Repeat the synchronized descendant-junction swap test after root walking is introduced.
- Deterministically replace a later component of the configured root after the native walk has opened its parent component; require the walk to fail closed and never read the outside target.
- Run the isolated native tests plus `npm test` and `npm run lint`; report each result separately.

## API review cautions

- `NtCreateFile` is an NT native API; structure layouts, status handling, object attributes, and file options require code review against supported Windows/Node versions.
- `OBJ_DONT_REPARSE` and `FILE_OPEN_REPARSE_POINT` are not substitutes for checking the opened object's attributes and type.
- A drive root is a volume boundary and may be represented specially by Windows. Confirm actual behavior on supported Windows editions; do not silently broaden support to UNC/device paths.
- Handle-relative root walking reduces ancestor path lookup, but it does not by itself implement safe write/publish/rename/delete/enumeration semantics.
- The current bounded read is not an atomic content snapshot against in-place writers.
- Test fixtures must be disposed in a finally-style cleanup path even if the child exits, times out, or synchronization fails.

## Isolated validation run — 2026-10-26

The MSVC build script completed with exit code 0 and no compiler warnings on the final run. The native test harness reported all of the following as passing:

- Nested file opened through relative handle walk; configured root accepted with a trailing separator.
- Relative, repeated-separator, dot-dot, and ADS root paths rejected; UNC, device namespace, volume-GUID, missing-root, and non-directory-root inputs rejected as well.
- One hundred repeated invalid-root parser calls were rejected, and `GetProcessHandleCount` showed no process-handle-count increase across that loop. A separate loop of 100 failures after opening multiple configured-root components (missing suffix) also showed no handle-count increase. These are focused leak regression checks, not proof that every failure path is leak-free.
- Bounded content read and over-limit read rejection.
- Dot-dot descendant traversal, missing target, and directory-as-file rejected.
- Static junction, configured root junction, and junction in a configured-root ancestor rejected.
- Synchronized descendant ancestor-swap test returned the expected in-root bytes after the junction was installed.
- Synchronized configured-root-chain race: after the native walk opened the `root-race` parent component, the test renamed the next `branch` component and installed a junction to an outside directory at its old name. The native root walk rejected the replaced component and failed closed; it did not open or read the outside target.

Latest validation after adding the configured-root-chain race hook/test: the MSVC build and native harness exited 0 with all 13 outcomes reported as passing (including the new root-chain swap case). `npm test` and `npm run lint` also exited 0. The npm smoke test reported a 45-tool inventory, 30 basic checks, 34 format/document checks, successful authenticated HTTP initialization, and three URL redirect-security checks. The native harness is separate from the project npm scripts.

## Acceptance boundary

Even with these isolated tests passing, status remains **NOT ACCEPTED as race-free** until the native code receives careful API/security review and remaining failure-path, handle-lifetime, volume-identity, and operation-family semantics are tested. This is prototype evidence, not proof of complete filesystem confinement and not production integration.
