# Release Contents and Exclusions

## Included
- MCP Core, Local MCP and Bridge workspace packages (`packages/raya-mcp-core/`, `packages/raya-local-mcp/`, `packages/raya-bridge/`, `packages/raya-stack/`), plus root compatibility entry points, package manifests and lockfile.
- Main smoke/security checks, `scripts/verify-modular-packages.mjs`, modular package acceptance and Raya Windows launch helpers.
- Raya Admin Panel source, launcher, smoke test and README, including the non-secret MCP endpoint configuration example.
- Raya operational, architecture, security and development documentation, including the approved modular product architecture and migration plan.
- Raya branding assets and GitHub workflows/agent guidance.
- Experimental native filesystem probes, isolated from runtime.
- Selected broker-prototype source/specification material under `research/`.

## Excluded
- Tunnel Client source (external dependency; not present in the source repository).
- Wardrobe/Blender/3D-character `tools/`; local `workspaces/`.
- `raya-config.json`, `raya-local.yaml`, `panel-config.json`, `mcp-endpoints.json`, panel tokens and endpoint credentials.
- Backups, logs, tunnel log archive, downloads, temporary captures and unrelated market probes.
- `node_modules`, caches, build outputs, compiled binaries, model-checker state traces, large graph dumps and execution logs.
- Raya Agent / Control Center project source.

This is a curated staging copy, not a validated release or Git commit. The source working tree and GitHub repository have not been modified.
