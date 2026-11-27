# Root-relative read-only adapter research

**Status: TEST-ONLY RESEARCH — NOT A PRODUCTION ADAPTER.**

This isolated addon characterizes a Windows HANDLE-relative root/parent API. It performs a point-in-time check that regular files have `FILE_STANDARD_INFO.NumberOfLinks == 1`. The check rejects pre-existing multi-link files, but the synchronized race test demonstrates that it does **not** exclude a new hard link being inserted after the check and before the read. Do not treat this as an enforceable hard-link security boundary.

The addon is not imported by Raya MCP runtime packages and does not authorize Production integration.

## Build

Requirements: Windows x64, Node.js 22.x, Python 3, Visual Studio C++ Build Tools.

From this directory:

```powershell
npx --yes node-gyp@latest rebuild
```

The addon is generated under `build/Release/` and must remain a local build artifact.

## Tests

```powershell
node handle-relative-hardlink-guarded-rootapi-test.cjs
node handle-relative-readonly-adapter-test.cjs
```

The hard-link harness checks ordinary single-link behavior and rejection of pre-existing outside-backed hard links for read, tail-read, metadata and directory enumeration. It also uses a test-only callback inserted after the native link-count check and before `ReadFile`; it creates a second link in that window and demonstrates that the read still returns bytes.

The read-only adapter harness checks UTF-8 parity, 256-KiB bounds, metadata mapping, error categories, injected close/open failures, HANDLE accounting, reparse behavior, root/parent rename-junction swaps, and rejection of pre-existing outside-backed hard links for read and metadata.

## Revalidation — 2026-11-27

On one Windows x64 / Node.js 22.23.2 / local NTFS host:
- node-gyp build completed with exit code 0 using Visual Studio 2026 Build Tools 18.7.3.
- `handle-relative-readonly-adapter-test.cjs` completed with exit code 0.
- `handle-relative-hardlink-guarded-rootapi-test.cjs` completed with exit code 0.
- The synchronized race test successfully created a second hard link after the NumberOfLinks check and before ReadFile; the read returned the canary bytes while the file's link count was 2.

## Important limitations

- The JavaScript root registry in the adapter harness is a model only. It is not wired into the MCP server's configured-root registry or registered Production handlers.
- The point-in-time link-count check is raceable. The synchronized test proves a hard link can be inserted after the check and before bytes are read. This prototype is therefore **not acceptable** as the final enforcement boundary for the approved strict single-link policy.
- Only local NTFS on one Windows host was exercised. ReFS, SMB/network shares, removable media, cloud/sync providers, other Windows token configurations and other operation families remain untested.
- The tests do not prove race-free behavior for every filesystem operation or establish an accepted Production security boundary.

**Phase 1 remains IN PROGRESS / NOT ACCEPTED. Production integration remains NO-GO.**
