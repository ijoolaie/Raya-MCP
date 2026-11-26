# Raya Local MCP — Curated Repository Inventory

**Curated staging snapshot:** 2026-11-26\
**Source repository inspected read-only:** local Raya MCP source tree\
**Scope:** this inventory describes the sanitized staging tree, not the user's live machine configuration.

## Product identity

Raya Local MCP is a Windows-first Node.js MCP server with STDIO and optional authenticated loopback Streamable HTTP transports. It is one product with three logical responsibility modules:
- MCP Protocol Integration
- Local MCP Server
- Bridge & Connectivity

See the root `ARCHITECTURE.md` and `PROJECT-STATUS.md` for the authoritative module map and acceptance state.

## Included project structure

| Path | Role | Boundary |
|---|---|---|
| `server.mjs` | MCP entry point, SDK server, tool registration and transports | Runtime |
| `advanced-tools.mjs` | Workspace, process, search, settings and diagnostics tools | Runtime |
| `format-tools.mjs` | XLSX, DOCX and PDF tools | Runtime |
| `url-tools.mjs` | Outbound HTTPS text retrieval | Runtime |
| `raya-admin-panel/` | Loopback Admin Panel UI/API and STDIO launcher | Bridge & Connectivity |
| Root `start-*.cmd/.ps1/.vbs` | Windows startup helpers | Bridge & Connectivity |
| `package.json`, `package-lock.json` | Runtime dependencies and scripts | Build/install |
| `docs/`, root Markdown guides | Architecture, security, operations, handoff and development records | Documentation |
| `.github/` | CI workflow and project agent instructions | Repository automation |
| `assets/` | Project branding | Static assets |
| `.native-probe/` | Native filesystem/lifecycle research and tests | Experimental only |
| `research/` | Broker prototype and specifications | Experimental only |

## Excluded from this curated repository

- User workspace content and unrelated wardrobe/Blender/3D-character tools.
- Raya Agent and Raya Control Center project source.
- Local runtime configuration, panel config, tokens, API keys, tunnel profiles and environment secrets.
- Tunnel Client source/binaries; the Tunnel Client is a separately installed dependency.
- Backups, logs, generated TLC state traces, graph dumps, run CSVs, caches, dependencies, compiled outputs and build artifacts.

The exact import/exclusion decisions are recorded in `IMPORT-MANIFEST.md` and `RELEASE-CONTENTS.md`.

## Runtime and security boundaries

- The MCP server runs with the Windows account's permissions; it is not an OS sandbox.
- PowerShell and child-process execution are privileged operations and must not be described as sandboxed.
- Production filesystem handlers remain subject to the documented race-resistance limitations. Experimental HANDLE-relative probes do not establish production acceptance.
- The Admin Panel binds to loopback and requires local configuration for management operations. Its external Tunnel Client dependency is not included.
- No machine-specific process IDs, tunnel identifiers, local workspace paths, credentials or live process snapshots are part of this inventory.

## Validation pointers

- Import completeness: [Import review](../IMPORT-REVIEW.md)
- Current staging and release readiness: [Project status](../../PROJECT-STATUS.md)
- Phase-4 checks: [Acceptance results](../ACCEPTANCE-RESULTS.md)
- Operational runbooks: [Operations index](START-HERE.md)

This is a curated source inventory, not a claim that the project is production-ready or that every tool has exhaustive behavioral coverage.
