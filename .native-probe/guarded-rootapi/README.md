# Guarded root API and read-only adapter research

**Status: TEST-ONLY RESEARCH — NOT A PRODUCTION ADAPTER.**

This isolated addon characterizes a Windows HANDLE-relative root/parent API with a strict regular-file policy: a regular file is rejected unless `FILE_STANDARD_INFO.NumberOfLinks == 1`. It is not imported by Raya MCP runtime packages and does not authorize Production integration.

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

The first harness checks single-link acceptance and rejection of outside-backed hard links for read, tail-read, metadata and directory enumeration.

The second is a test-only JavaScript adapter model over the native addon. It checks UTF-8 parity, 256-KiB bounds, metadata mapping, error categories, injected close/open failures, HANDLE accounting, reparse behavior, root/parent rename-junction swaps, and outside-backed hard-link rejection for both read and metadata.

## Revalidation — 2026-11-27

On one Windows x64 / Node.js 22.23.2 / local NTFS host:
- node-gyp build completed with exit code 0 using Visual Studio 2026 Build Tools 18.7.3.
- `handle-relative-readonly-adapter-test.cjs` completed with exit code 0.
- `handle-relative-hardlink-guarded-rootapi-test.cjs` completed with exit code 0.

## Important limitations

- The JavaScript root registry in the adapter harness is a model only. It is not wired into the MCP server's configured-root registry or registered Production handlers.
- The adapter functions are test-only and do not constitute production parity or error-contract approval.
- Only local NTFS on one Windows host was exercised. ReFS, SMB/network shares, removable media, cloud/sync providers, other Windows token configurations and other operation families remain untested.
- The hard-link policy is deliberately conservative and rejects legitimate multi-link files too.
- The tests do not prove race-free behavior for every filesystem operation or establish an accepted Production security boundary.

**Phase 1 remains IN PROGRESS / NOT ACCEPTED. Production integration remains NO-GO.**
