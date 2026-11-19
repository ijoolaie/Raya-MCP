# Architecture Decision — Raya MCP and Raya Agent

Date: 2026-11-05
Status: Accepted direction; implementation not started
Decision owner: User

## Decision

Keep **Raya MCP** as an independent, maintained project. Create **Raya Agent** as a separate project with its own repository, release/version lifecycle, configuration, runtime, tests, and roadmap.

Raya Agent's initial baseline should reuse the full existing Raya MCP codebase where technically applicable. This is a bootstrap/reuse decision, not a decision to merge the two products permanently or make Raya MCP subordinate to Raya Agent.

## Product boundaries

### Raya MCP
- Remains independently usable as the local MCP server/bridge.
- Owns MCP protocol handling, tool catalog and schemas, local host capability adapters, bounded execution, session/search lifecycle, and MCP-specific diagnostics.
- Retains its own security acceptance gates and release policy.
- Must continue to work without Raya Agent installed.

### Raya Agent
- Owns the agent runtime: planning, task decomposition, context and memory policy, approval/consent flow, tool selection/orchestration, multi-step execution, user interaction, and future device UX.
- Initially reuses the existing Raya MCP implementation rather than rewriting working capabilities.
- Treats Raya MCP as a tool provider through a documented/versioned MCP interface. It must not reach into MCP private internals at runtime.
- Must be independently installable, configurable, testable, and releasable.

## Initial code reuse strategy

1. Preserve the existing Raya MCP repository and working tree. Do not rename it, move it, or repurpose it.
2. Create a separate Raya Agent repository from a reviewed snapshot/import of the Raya MCP codebase, retaining applicable source, tests, docs, and build assets as an initial bootstrap. Record the source repository, source commit/tree state, imported paths, excluded secrets/local artifacts, and provenance.
3. In Raya Agent, classify imported code into:
   - reusable shared capability/core modules;
   - MCP-server-specific transport, protocol and packaging;
   - Agent-specific orchestration and UX (new);
   - local experiments, generated outputs, credentials, machine-specific config and unsafe prototypes (exclude unless separately reviewed).
4. Avoid maintaining two silently diverging copies. As the first refactor, establish explicit ownership and dependency direction. Where code should remain shared, extract a versioned package or a clearly bounded shared module only after tests and licensing/provenance checks. Raya Agent must consume the public MCP contract, not import MCP server internals as a runtime shortcut.
5. Keep the first Agent milestone deliberately small: launch/connect to Raya MCP, discover tools, request approval for consequential actions, execute a bounded multi-step task, report results, and clean up sessions.
6. Preserve all existing Raya MCP security gates. In particular, Phase 1 filesystem confinement remains NOT ACCEPTED; the Agent must not advertise or expose unsafe filesystem operations as secure merely because they are reachable through MCP.

## Dependency and version policy

- Initial bootstrap may contain the full applicable source snapshot, but ownership must be explicit.
- Runtime dependency direction: Raya Agent -> Raya MCP public MCP interface. Raya MCP must not depend on Raya Agent.
- Version the MCP tool contract and record the minimum/maximum compatible MCP server versions in Agent releases.
- Any shared library extraction requires independent tests, explicit API ownership, and a migration plan; do not introduce a monorepo or submodule solely for convenience.
- Keep secrets, environment files, credentials, user data, local tunnel profiles, machine-specific paths, build artifacts, and disposable security-test fixtures out of the Agent repository.
- Do not create a public repository. New repository visibility should match the existing private project unless the user explicitly decides otherwise.

## Non-goals for the first milestone

- No rewrite of Raya MCP.
- No automatic execution of destructive or privileged actions.
- No production filesystem-security bypass or fallback.
- No Android/mobile implementation before the Windows agent lifecycle and approval model are validated.
- No production service installation, workspace ACL/owner migration, or MCP production-handler switch as part of the repository bootstrap.

## Implementation sequence

1. Freeze and inventory the Raya MCP source baseline without altering unrelated dirty work.
2. Produce an import manifest and classify source, tests, docs, generated files, secrets, local configuration, and native prototypes.
3. Create the private Raya Agent repository and import the reviewed baseline, preserving provenance.
4. Establish separate package identity, app entry point, configuration namespace, logs, and tests.
5. Add the Agent orchestration layer and connect it to Raya MCP through the public MCP protocol.
6. Verify both projects independently: Raya MCP regression/security suite and Raya Agent integration/lifecycle/approval suite.
7. Document version compatibility and release independently.

## Acceptance criteria

- Raya MCP remains buildable, testable, and usable independently.
- Raya Agent builds and tests from its own repository without relying on untracked files from the Raya MCP working tree.
- Imported files have a documented provenance and exclusion manifest; no credentials or machine-specific secrets are present.
- Agent runtime communicates through the versioned MCP contract; no private in-process dependency on MCP internals.
- Agent has explicit approval gates, bounded calls/timeouts/output, cancellation, and cleanup.
- Existing Raya MCP tests remain green; new Agent tests cover tool discovery, approval/denial, timeout, cancellation, partial failure, reconnect, and session cleanup.
- Security status is stated truthfully: Raya MCP Phase 1 remains NOT ACCEPTED until its own gate passes.

## Immediate next action

Do not copy the current dirty working tree blindly. First create a read-only inventory/import manifest from the existing repository and identify a clean, reproducible source baseline. Then review the manifest before creating or populating the new private repository.
