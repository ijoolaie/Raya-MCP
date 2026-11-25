# Raya Local MCP — Project Architecture

**Status:** Architecture baseline  
**Last reviewed:** 2026-11-26

## Product boundary

Raya Local MCP is one product and one repository. It has three cooperating modules with separate responsibilities, not three independently released products.

### 1. MCP protocol integration

Owns the MCP-facing contract:
- MCP server construction and tool/resource/prompt registration.
- STDIO transport and the optional authenticated Streamable HTTP transport.
- Input schemas, protocol errors, request lifecycle and transport shutdown.
- Compatibility with MCP clients such as ChatGPT.

Current implementation entry point: `server.mjs`, with registration modules imported by it.

### 2. Local MCP server and capabilities

Owns execution of operations on the user's Windows machine:
- Workspace-root policy and filesystem operations.
- Search, process/session operations and diagnostics.
- Office/PDF adapters and public HTTPS text retrieval.
- Runtime limits, validation and permission boundaries.

Current implementation modules include `advanced-tools.mjs`, `format-tools.mjs` and `url-tools.mjs`.

### 3. Bridge and connectivity

Owns local startup, client connectivity and operational management:
- STDIO launch wrappers and validated launcher.
- Local Raya Admin Panel for status, configuration and Tunnel Client lifecycle.
- Tunnel profile integration and health/diagnostic visibility.

The local Admin Panel and launcher sources currently exist in the working directory under `raya-admin-panel/`; they must be reviewed and added as first-party source. Runtime configuration files and user-specific root lists are not source and must not be committed.

## External dependency boundary

The Tunnel Client is a separate executable/distribution. Its source is not part of this repository. Raya integrates with it through a documented process/profile contract. Do not copy its executable, archives, generated logs, or third-party distribution into this repository. Record version, provenance, checksum and license notices in dependency documentation instead.

## Target repository layout

The current source layout is intentionally retained until each move can be validated against launch paths and tests. The logical ownership map is:

- `src/protocol/` — MCP server and transport lifecycle (future extraction).
- `src/capabilities/` — filesystem, process, search, office/PDF and URL capabilities (future extraction).
- `src/connectivity/` — validated STDIO launcher and bridge integration (future extraction).
- `admin-panel/` — first-party local management panel.
- `docs/architecture/`, `docs/operations/`, `docs/security/` — durable project documentation.
- `tests/` — maintained automated tests and fixtures.
- `research/` — explicitly non-production security prototypes and experiments.

These are target ownership boundaries, not a claim that source files have already been moved. Existing entry points and paths remain authoritative until a dedicated migration updates all references and validates startup.

## Security posture

- Shell/PowerShell execution runs with the Windows account's permissions; it is not a sandbox.
- Production filesystem confinement is not yet accepted as race-free; path-based TOCTOU risk remains documented.
- HTTP mode must remain loopback-bound and authenticated unless a separately reviewed deployment design changes that boundary.
- Never commit API keys, bearer tokens, cookies, DPAPI blobs/plaintext, live tunnel profiles, local root lists, runtime config, logs, or user workspace contents.
- Prototype/native broker code must never be described as production enforcement until migrated and independently accepted.

## Release unit

The release unit is the integrated Raya Local MCP product. Module-level tests and ownership can be separate, but compatibility and release readiness are evaluated across the three modules together.
