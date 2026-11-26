# Raya MCP Stack — Modular Product Architecture

**Status:** Approved target architecture; implementation transition in progress\
**Decision date:** 2026-11-26

## 1. Product vision

Raya MCP Stack is the product family and integrated distribution of three independently useful, composable products:

1. **Raya MCP Core** — protocol runtime and MCP integration foundation.
2. **Raya Local MCP** — local machine/workspace capabilities built on Core.
3. **Raya Bridge** — connectivity, lifecycle and orchestration capabilities that can connect to different MCP-compatible servers and services.

Users may adopt one component or combine components. The complete Stack is an integrated distribution, not a mandatory runtime dependency for every user.

## 2. Product and module boundaries

### Raya MCP Core

Owns MCP protocol lifecycle, server/client integration primitives where applicable, transport adapters, tool/resource/prompt contracts, capability registration interfaces, protocol errors and compatibility policy.

Core MUST NOT import or require Raya Local MCP or Raya Bridge. A Core-only installation must not install local filesystem/process tools or the Bridge control plane.

### Raya Local MCP

Owns local workspace and machine capabilities: filesystem, process/session, search, configuration/diagnostics, URL retrieval and document-format tools.

Local MCP depends on a supported Core contract/runtime. It MUST NOT depend on Bridge for its basic local STDIO or supported direct transport use. Bridge may optionally manage or expose it.

### Raya Bridge

Owns connectivity orchestration, endpoint/client connection management, startup/lifecycle coordination, optional tunnel integration, health/status and administrative control surfaces.

Bridge MUST be designed to work with compatible MCP servers beyond Raya Local MCP. Bridge-only use means the Bridge package can be installed and operated without Local MCP (Core resolves as a transitive dependency); actual MCP request forwarding necessarily requires at least one configured MCP-compatible endpoint. Bridge MUST NOT require Local MCP to be installed.

### Raya MCP Stack

The Stack distribution integrates Core, Local MCP and Bridge with a coordinated installer/configuration experience and a tested compatibility matrix. It MUST remain possible to omit optional components.

## 3. Dependency rules

Allowed dependency direction:

- Local MCP -> Core public contracts/runtime.
- Bridge -> Core public contracts/client interfaces where needed.
- Bridge -> Local MCP only through documented external interfaces or process/transport contracts; never by importing Local MCP internals.
- Stack packaging/integration -> the three components.

Forbidden dependency direction:

- Core -> Local MCP.
- Core -> Bridge.
- Local MCP -> Bridge for core functionality.
- Bridge -> Local MCP private implementation details.
- Any component -> Stack meta-package.

All cross-component communication MUST use documented, versioned public contracts (protocol, CLI/process contract, HTTP API, or package API). Avoid shared mutable internal state across package boundaries.

## 4. Supported adoption profiles

| User choice | Required components | Notes |
|---|---|---|
| Core only | Core | Protocol foundation without local tools or Bridge |
| Local MCP only | Core + Local MCP | Delivered as one user-facing install profile; Bridge is not required |
| Bridge only | Core + Bridge | Standalone control/connectivity plane; Bridge resolves Core automatically; Local MCP is not required |
| Core + Bridge | Core + Bridge | Bridge can manage/connect to Core-based endpoints |
| Local MCP + Bridge | Core + Local MCP + Bridge | Bridge manages/connects to Local MCP |
| Full Stack | Core + Local MCP + Bridge | Integrated distribution with optional components |

“Local MCP only” is a product selection, not a claim that Local MCP has no Core dependency. The installer resolves its required Core dependency automatically.

## 5. Packaging, versioning and compatibility

Each component MUST have:

- Its own package identity and release/version history.
- Explicit runtime and peer dependency declarations.
- A documented public API/contract surface.
- Independent build, smoke-test and release validation.
- A changelog and support/deprecation policy.
- A compatibility declaration against supported Core/Bridge contract versions.

Use independent component versions (independent versioning) and a separate Stack release version that pins a tested combination. Do not assume that equal version numbers imply compatibility.

The Stack release manifest MUST record exact component versions and compatibility ranges. Breaking contract changes require a major version increment of the affected contract/API and a migration note.

## 6. Installation and runtime requirements

- Core-only installation must not pull Local MCP or Bridge dependencies.
- Local MCP installation must resolve Core automatically and must not install Bridge.
- Bridge installation must not install Local MCP.
- Stack installation may select all components or a subset.
- Each component must have its own documented configuration, startup, shutdown, health and upgrade behavior.
- Optional tunnel software remains an external integration unless a future explicit product decision changes that boundary.
- Secrets and machine-local configuration must remain outside source control.

## 7. Current implementation baseline (2026-11-26)

The current staging tree is **partially modularized**. Core, Local MCP, Bridge and Stack have separate workspace package boundaries. Programmatic Bridge endpoint connectivity, Admin Panel operations for preconfigured profiles and the Stack profile manifest are implemented; endpoint-profile editing remains pending; GitHub Packages registry metadata and a private publishing workflow are integrated in the source working tree, with hosted validation and first publication pending:

- `packages/raya-mcp-core/` contains `@ijoolaie/mcp-core@0.1.0`, which owns MCP server construction and transport factories.
- `packages/raya-local-mcp/` contains `@ijoolaie/local-mcp@0.1.0`, which depends on Core and owns Local MCP runtime and capability handlers.
- Root `server.mjs` and tool-module files are compatibility wrappers for existing scripts and tests.
- `packages/raya-bridge/` contains the Bridge Admin Panel and tunnel lifecycle manager; root wrappers and Windows launchers preserve compatibility.
- `packages/raya-stack/` contains `@ijoolaie/stack@0.1.0`, a meta-package pinning Core, Local MCP and Bridge and declaring Core-only, Local-MCP-only, Bridge-only and Full-Stack profiles.
- The root `package.json` is a private npm workspace shell with legacy identity `local-mcp-bridge@0.1.0`; product package identities are declared in `packages/raya-mcp-core/`, `packages/raya-local-mcp/`, `packages/raya-bridge/` and `packages/raya-stack/`.
- The Admin Panel depends on machine-local configuration and an external Tunnel Client for some operations.

Core, Local MCP and Bridge are current workspace package boundaries. Bridge depends on Core public client/transport APIs but not Local MCP. Its programmatic connector supports STDIO and Streamable HTTP; the Admin Panel endpoint-profile editor remains pending; the GitHub Packages publishing workflow is integrated in the source working tree but has not yet run on GitHub.

## 8. Migration plan and release gates

### Gate 0 — Architecture baseline
- Record this decision and update repository/product documentation.
- Inventory imports, shared state, configuration, startup paths and cross-module calls.
- Define public contracts and dependency direction.

### Gate 1 — Extract Core boundary (partially implemented)
- [x] Create `@ijoolaie/mcp-core` package and initial public API contract version `0.1.0`.
- [x] Move MCP server construction and STDIO/Streamable HTTP transport constructors behind the Core API.
- [x] Integrate the root server through the Core package dependency.
- [x] Add Core unit tests and root STDIO/HTTP integration checks.
- [x] Pack and install Core in an isolated consumer project; verify package import and server factory without Local MCP or Bridge.
- [x] Prove a minimal Core-only MCP server handshake over STDIO using the included example server.
- [ ] Complete explicit lifecycle/transport management and publish compatibility policy.

### Gate 2 — Extract Local MCP package (implemented; release validation pending)
- [x] Create `@ijoolaie/local-mcp` as a separate workspace package with its own manifest and start scripts.
- [x] Move local server/runtime and capability modules into the package; retain root compatibility wrappers.
- [x] Declare Core as a semver dependency and use npm workspaces for local composition.
- [x] Pack and install Core + Local MCP in a clean consumer project without Bridge.
- [x] Start the installed Local MCP package over STDIO and verify the 47-tool inventory.
- [x] Complete clean HTTP consumer acceptance from packed Core + Local MCP tarballs (401 unauthenticated; 200 authenticated initialize).
- [x] Integrate GitHub Packages publish workflow into the source working tree; hosted execution and first publication remain pending.
- [ ] Finalize package compatibility policy.

### Gate 3 — Extract Bridge (package, programmatic connector and panel operations implemented; profile editor pending)
- [x] Create `@ijoolaie/bridge` as an independent workspace package that depends on Core public client/transport APIs and not Local MCP.
- [x] Move the Admin Panel server/UI into the package; retain root compatibility wrappers and the Local MCP STDIO adapter.
- [x] Externalize workspace/configuration paths, tunnel profile, health endpoints and headless startup through environment variables.
- [x] Pack and install Core + Bridge in a clean consumer project; verify the connector works without Local MCP and the Admin Panel enforces token protection.
- [x] Implement a programmatic MCP endpoint/client connector for arbitrary configured STDIO and Streamable HTTP servers.
- [x] Add authenticated Admin Panel endpoints and MCP page for listing preconfigured profiles, connecting/disconnecting, tool discovery and tool invocation.
- [ ] Add an endpoint-profile editor; profile creation/editing remains a local file operation for security.
- [ ] Validate optional tunnel integration and lifecycle behavior against more than the current Raya tunnel profile.
- [x] Integrate GitHub Packages publish workflow into the source working tree.
- [ ] Complete hosted release validation and compatibility policy.

### Gate 4 — Stack distribution (manifest implemented; clean-install acceptance pending)
- [x] Create `@ijoolaie/stack` as a meta-package pinning Core, Local MCP and Bridge versions.
- [x] Publish a versioned manifest describing Core-only, Local-MCP-only, Bridge-only and Full-Stack selection profiles.
- [x] Add Stack manifest tests for component pins, dependency direction and profile selection.
- [x] Prove Core-only, Local-MCP-only, Bridge-only and Full-Stack installs from four separate clean consumer folders using packed tarballs; verify dependency presence/absence for each profile.
- [x] Configure GitHub Packages registry metadata and a private publishing workflow in staging.
- [ ] Run the hosted workflow and perform the first intentional private publication.
- [ ] Finalize the published contract compatibility policy.

### Gate 5 — Continuous modularity enforcement (implemented in staging; hosted run pending)
- [x] Add `scripts/verify-modular-packages.mjs` to assert package identities, exact dependencies, workspace membership and forbidden cross-package imports.
- [x] Automate packed clean-install acceptance for Core-only, Local-MCP-only, Bridge-only and Full-Stack profiles.
- [x] Include Local MCP STDIO and Bridge connector smoke checks in the modular verification script.
- [x] Add `npm run verify:modular` to the GitHub Actions CI workflow before the legacy aggregate test step.
- [ ] Confirm the first hosted GitHub Actions run after changes are committed/pushed.

### Completion criteria

The modular architecture is complete only when each component can be installed, started, tested, versioned and released independently; dependency rules are enforced by package boundaries/CI; and the supported adoption profiles pass clean-install acceptance tests.

## 9. Non-goals and safety

- Do not physically move runtime files merely to make folders look modular.
- Do not change the source working tree as part of staging architecture documentation.
- Do not run destructive tests against user workspaces or live configuration.
- Do not include local tokens, tunnel profiles, private configuration or secrets in packages.
- Do not treat research prototypes or native probes as production dependencies without separate review.
