# Raya MCP Stack — Architecture

The approved target architecture is a modular product family: Raya MCP Core, Raya Local MCP and Raya Bridge. The full decision, dependency rules, adoption profiles and migration gates are documented in [MODULAR-PRODUCT-ARCHITECTURE.md](docs/architecture/MODULAR-PRODUCT-ARCHITECTURE.md).

**Implementation status:** Core (`@ijoolaie/mcp-core@0.1.0`), Local MCP (`@ijoolaie/local-mcp@0.1.0`) and Bridge (`@ijoolaie/bridge@0.1.0`) are separate npm workspace packages, and `@ijoolaie/stack@0.1.0` is the integrated meta-package. Local MCP and Bridge both depend on Core's public APIs; Bridge does not depend on Local MCP. Bridge now provides a programmatic MCP client connector for STDIO and Streamable HTTP; registry publication and full Stack release remain pending.

## Current package and source mapping

### Raya MCP Core target

The protocol construction/transport factory is extracted to `packages/raya-mcp-core/src/index.mjs`. Local MCP consumes it through the `@ijoolaie/mcp-core` package API; Core is a reusable library, not an end-user CLI.

### Raya Local MCP target

The Local MCP package is under `packages/raya-local-mcp/src/`: `server.mjs`, `advanced-tools.mjs`, `format-tools.mjs` and `url-tools.mjs`. Root-level files are compatibility wrappers.

### Raya Bridge target

The Admin Panel server/UI and generic MCP connector are in `packages/raya-bridge/src/`; the package depends on Core, not Local MCP. Root `raya-admin-panel/` files and Windows launchers preserve compatibility. Tunnel Client remains external. `packages/raya-stack/` pins the three component versions and publishes the supported install profiles.

## Existing logical responsibility map (current code)

### 1. MCP Protocol Integration

**Responsibility:** MCP SDK lifecycle, tool registration/contracts, transport selection, protocol-facing errors and request context.

**Current implementation:** `packages/raya-mcp-core/src/index.mjs` owns MCP server construction and STDIO / Streamable HTTP transport factories. `packages/raya-local-mcp/src/server.mjs` composes that Core API with Local MCP tool registration and runtime setup.

### 2. Local MCP Server

**Responsibility:** local workspace operations, process/session management, search, configuration/diagnostics, URL retrieval and document formats.

**Current implementation:**
- `packages/raya-local-mcp/src/server.mjs` — Local MCP runtime, local tool registration and transport selection through Core.
- `packages/raya-local-mcp/src/advanced-tools.mjs` — filesystem, process, search, configuration and diagnostics handlers.
- `packages/raya-local-mcp/src/format-tools.mjs` — XLSX, DOCX and PDF handlers.
- `packages/raya-local-mcp/src/url-tools.mjs` — outbound HTTPS retrieval and URL safety checks.
- Root-level `server.mjs`, `advanced-tools.mjs`, `format-tools.mjs` and `url-tools.mjs` preserve old entry paths.

### 3. Bridge & Connectivity

**Responsibility:** local startup, panel lifecycle, process handoff and connectivity orchestration.

**Current implementation:**
- `packages/raya-bridge/src/admin-panel/` — loopback dashboard/API and tunnel lifecycle manager.
- Root `raya-admin-panel/` — compatibility server wrapper, optional Local MCP STDIO adapter, and legacy static asset copy.
- Root `start-*.cmd`, `start-*.ps1`, and `start-*.vbs` — Windows startup helpers.
- Tunnel Client — separately installed/maintained external dependency; its source is not in this repository.

## Runtime dependency direction

```text
Raya Local MCP (packages/raya-local-mcp)
        | depends on public API
        v
Raya MCP Core (packages/raya-mcp-core)

Local MCP package owns the filesystem, process, search, format and URL capability modules.

Raya Bridge (packages/raya-bridge)
        | depends on public client/transport API
        v
Raya MCP Core (packages/raya-mcp-core)

Bridge package and compatibility launchers
  ├─ programmatic connector (STDIO / Streamable HTTP)
  ├─ Admin Panel and tunnel lifecycle manager
  ├─ root compatibility wrappers and Windows launchers
  └─ external Tunnel Client (not vendored)
```

The Local MCP source now resides in its own package and depends on Core through the public package API. The root wrapper paths remain for compatibility with existing launchers and tests. Bridge is extracted as a workspace package that depends on Core's client/transport APIs but not Local MCP. Its programmatic connector supports arbitrary configured STDIO and Streamable HTTP MCP servers; Admin Panel now lists configured profiles, connects/disconnects, discovers tools and invokes them; profile editing remains a local file operation.

## Research boundary

- `.native-probe/` contains experimental filesystem and lifecycle probes.
- `research/broker-prototype-v2-20261106/` contains broker prototype source/specification material.
- Neither is imported by the production MCP runtime. The syntax-check script may inspect selected probe files; that does not make them runtime dependencies.
- Generated TLC traces, graph dumps, binary outputs and run logs are excluded.

## Release prerequisites

See `PROJECT-STATUS.md`, `docs/IMPORT-REVIEW.md`, and `docs/ACCEPTANCE-RESULTS.md`. A curated source tree is not a configured installation: local panel config, root runtime config and the external Tunnel Client must be supplied separately.