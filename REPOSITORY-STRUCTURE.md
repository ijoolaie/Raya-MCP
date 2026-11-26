# Raya MCP Stack — Repository Structure

The target modular product architecture and migration gates are defined in [docs/architecture/MODULAR-PRODUCT-ARCHITECTURE.md](docs/architecture/MODULAR-PRODUCT-ARCHITECTURE.md). The current tree contains separate Core, Local MCP and Bridge workspace packages plus the Stack meta-package. The logical map below is not evidence that the packages have been published to a registry or that the full Stack release is complete.

```text
Raya-Local-MCP/
├── packages/raya-mcp-core/     # Core library package
├── packages/raya-local-mcp/     # Local MCP server package
├── packages/raya-bridge/         # Bridge/Admin Panel/connector package
├── packages/raya-stack/          # Stack meta-package and profile manifest
├── server.mjs, advanced-tools.mjs, format-tools.mjs, url-tools.mjs  # compatibility wrappers
├── package.json, package-lock.json, *.mjs tests
├── start-*.cmd / *.ps1 / *.vbs
├── raya-admin-panel/
├── docs/ and PROJECT-STATUS.md
├── assets/
├── .github/agents/ and workflows/
├── .native-probe/             # experimental filesystem/lifecycle probes
└── research/
    └── broker-prototype-v2-20261106/
```

## Target products and current code mapping

- **Raya MCP Core:** extracted package at `packages/raya-mcp-core/`; owns MCP server construction and transport factories. Core is distributed as a library package (not an end-user CLI); its packed tarball passed an isolated consumer install/import check. A publish/release workflow and the broader explicit transport lifecycle policy remain pending.
- **Raya Local MCP:** package at `packages/raya-local-mcp/`; owns the local server runtime and capability handlers and depends on Core. Root paths are compatibility wrappers.
- **Raya Bridge:** package at `packages/raya-bridge/`; Admin Panel, tunnel lifecycle manager and programmatic MCP connector; depends on Core client/transport APIs, not Local MCP.
- **Raya MCP Stack:** meta-package at `packages/raya-stack/` pins the component versions and declares the four supported install profiles. Clean-install acceptance and release automation remain pending.

Core, Local MCP and Bridge now have current npm workspace package boundaries. The Bridge connector is implemented for STDIO and Streamable HTTP; its Admin Panel endpoint-management UI, publication and Stack distribution remain future work.

## Exclusions

Not part of this repository: wardrobe/Blender `tools/`, local `workspaces/`, unrelated Agent/Control Center sources, local configuration, credentials, backups, logs, downloads, caches, build output and compiled artifacts.

See `IMPORT-MANIFEST.md`, `docs/IMPORT-REVIEW.md`, and `PROJECT-STATUS.md` for the curated-tree scope and validation state.
