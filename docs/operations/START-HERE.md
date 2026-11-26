# Raya MCP Stack — Operations Index

Use this directory as the first stop for troubleshooting. For repository orientation and current readiness, start with the root `PROJECT-STATUS.md`, then read this index and the relevant graph/runbook section.

## Project-level documents

- [Modular product architecture](../architecture/MODULAR-PRODUCT-ARCHITECTURE.md) — approved Core / Local MCP / Bridge product boundaries, dependency rules, adoption profiles and migration gates.
- [Project status](../../PROJECT-STATUS.md) — current staging state, architecture modules, validation and release limits.
- [Architecture](../../ARCHITECTURE.md) — three logical module boundaries and current source mapping.
- [Import review](../IMPORT-REVIEW.md) — relative import completeness, package findings and Admin Panel prerequisites.
- [Acceptance results](../ACCEPTANCE-RESULTS.md) — phase-4 commands and outcomes.

## Operational documents

- [GitHub Packages publishing](GITHUB-PACKAGES-PUBLISHING.md) — private package scope, registry metadata, publishing workflow, token permissions and first-release checklist.
1. [Runtime and dependency graphs](RUNTIME-DEPENDENCY-GRAPHS.md) — Mermaid graphs for request flow, process ancestry, key inheritance, Docker boundary, and triage decisions.
2. [Repository inventory](REPOSITORY-INVENTORY.md) — verified paths, source/tool families, launchers, security research areas, Git baseline and recorded runtime snapshots.
3. [Diagnostic runbook](DIAGNOSTIC-RUNBOOK.md) — safe, read-only diagnostic sequence and symptom-to-layer mapping.
4. [Doctor design](DOCTOR-DESIGN.md) — proposed read-only checks, safety contract, severity/confidence model, timeouts and exit codes. Design only.
5. [Fast knowledge index](KNOWLEDGE-INDEX.md) — question-to-document/source routing and stable component IDs.
6. [Tool catalog](TOOL-CATALOG.md) — source-declared tools grouped by module and behavior; re-count after code changes.
7. [Security boundaries](SECURITY-BOUNDARIES.md) — workspace roots, path-validation layers, process/shell authority, URL boundary and known limitations.
8. [Bridge MCP endpoint security](BRIDGE-MCP-ENDPOINT-SECURITY.md) — trusted STDIO commands, HTTPS/loopback policy, environment-based credentials and authenticated tool invocation.
9. [Panel, launcher and authentication audit](PANEL-LAUNCH-AUTH-AUDIT.md) — token handling, ACL enforcement, environment inheritance, panel HTTP boundary, configuration persistence, and process lifecycle.
10. [MCP transport and request-context audit](MCP-TRANSPORT-AUDIT.md) — STDIO/HTTP startup, root resolution, cancellation context, HTTP exposure and shutdown boundaries.
11. [Dependencies and tests](DEPENDENCIES-AND-TESTS.md) — runtime packages and npm script overview.
12. [Function-level source map](FUNCTION-MAP.md) — tool-to-handler/helper/API map, exact limits, lifecycle behavior and code-level caveats.
13. [Test and evidence map](TEST-COVERAGE.md) — standard test assertions, native/prototype script inventory, side effects and evidence limits.
14. [Process, search and resource lifecycle audit](PROCESS-SEARCH-LIFECYCLE-AUDIT.md).
15. [HTTP, panel concurrency and shutdown audit](HTTP-PANEL-CONCURRENCY-SHUTDOWN-AUDIT.md).
16. [Error handling and observability audit](ERROR-HANDLING-OBSERVABILITY-AUDIT.md).
17. [MCP tool result contract review](MCP-TOOL-RESULT-CONTRACT-REVIEW.md).
17. [Consolidated remediation priorities](REMEDIATION-PRIORITIES.md) — dependency-aware P0–P3 order and remaining work.
18. [Dashboard tool-count observability audit](DASHBOARD-TOOL-COUNT-OBSERVABILITY.md).

## Start a new incident

1. State whether the Connector catalog is missing, calls are failing, or only a specific tool is failing.
2. Run the read-only steps in `DIAGNOSTIC-RUNBOOK.md` in order.
3. Check the evidence state in `RUNTIME-DEPENDENCY-GRAPHS.md`; do not turn an observation into a confirmed dependency.
4. Update the baseline only after a meaningful change or a verified incident resolution.
5. Never modify the working tree or restart services as a first diagnostic step.

## Evidence labels

- **Verified:** a direct test or source inspection supports the edge.
- **Observed:** seen in a config, log, process snapshot or runtime output, but not necessarily end-to-end tested.
- **Unknown:** insufficient evidence; leave it unresolved.
- **Historical:** was true at a recorded time; re-check before relying on it.

## Current caution

The source working tree was dirty at the 2026-11-25 snapshot. Preserve existing work. Docker containers named for AI-Employee staging have not been proven to be Raya MCP dependencies. Production filesystem confinement remains NOT ACCEPTED in the project's status documents.

**Test caution:** `npm test` is not read-only. `format-smoke-test.mjs` persists `search.maxDepth=8` to `raya-config.json` and does not restore the previous value. See `TEST-COVERAGE.md` before running it.
