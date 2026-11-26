# Development Guide

## Prerequisites

- Windows for supplied launch scripts and Windows-specific filesystem probes.
- Node.js with ES module support and npm.
- Install locked dependencies in a fresh clone with `npm ci`.

## Entry points

- STDIO: `npm start`.
- HTTP: `npm run start:http`.
- Static syntax checks: `npm run check` or `npm run lint` (`lint` is an alias, not a separate lint engine).

## Architecture and source map

Read `docs/architecture/MODULAR-PRODUCT-ARCHITECTURE.md`, `ARCHITECTURE.md` and `PROJECT-STATUS.md` first. Core and Local MCP are separate npm workspace packages under `packages/`; Local MCP depends on Core. Bridge is a separate workspace package that depends on Core, not Local MCP. Its programmatic connector supports STDIO and Streamable HTTP; the Admin Panel lists/connects/disconnects preconfigured endpoints, discovers tools and invokes them; profile editing remains a local configuration-file operation. Run `npm run check`, `npm --prefix packages/raya-mcp-core test`, `node core-integration-smoke.mjs`, and (after packing/installing Core + Local MCP tarballs in a clean consumer folder) `node local-mcp-http-consumer-smoke.mjs`; pack Core + Bridge tarballs together, install them into a clean consumer and verify `@ijoolaie/bridge/connector`; `npm --workspace @ijoolaie/bridge test` covers connector STDIO/HTTP, endpoint configuration/security and Admin Panel UI script parsing; `bridge-consumer-smoke.mjs` covers authenticated endpoint management and tool invocation. `node bridge-consumer-smoke.mjs` covers dashboard/API token enforcement and the authenticated configured-endpoint lifecycle (connect, tools/list, tools/call, disconnect). `npm --workspace @ijoolaie/stack test` validates the Stack compatibility manifest and component-selection profiles. Run `npm run verify:modular` for the complete safe package suite, dependency-boundary audit and four-profile clean-install acceptance. Read `docs/IMPORT-REVIEW.md` before moving files or changing imports; follow the architecture migration gates and do not claim independent installation until clean-install acceptance tests pass.

## Checks and cautions

- Review `package.json` before running checks. The check script includes syntax checks for selected `.native-probe/` files.
- The aggregate `npm test` includes a format smoke test that persists `search.maxDepth=8` to root `raya-config.json` without restoring the previous value; the HTTP smoke test starts a local listener. Do not run it against a user's live configuration.
- Native probes and broker research are experimental. Run only individually, in a disposable test workspace, after reviewing each script.
- Never run destructive filesystem tests against real user workspaces.
- Never commit local configuration, panel tokens, logs, archives, generated TLC traces or compiled native binaries.
- The Admin Panel requires local configuration files and a separately installed Tunnel Client for its configuration and tunnel-management actions. See `raya-admin-panel/README.md`.

## Contributor workflow

1. Read `README.md`, `PROJECT-STATUS.md`, and `docs/operations/START-HERE.md`.
2. Read `docs/operations/SECURITY-BOUNDARIES.md` before changing filesystem tools.
3. Keep production changes separate from research.
4. Run the smallest relevant non-destructive check.
5. Update the relevant docs and test inventory.
