# Raya MCP execution plan — 2026-11-05

## Mission
Build Raya into a secure personal assistant that can use the OpenAI models through the connected client and perform approved, bounded actions on the Windows host first, then Android. Do not treat local file or shell access as a security boundary by itself.

## Governing execution rules
- Follow `AGENTS.md` and `docs/PROJECT-ROADMAP.md`; work one phase at a time.
- Preserve all existing modified/untracked files. No cleanup, reset, commit, or branch change without explicit request.
- Keep operations sequential while MCP stability is being validated.
- Never modify or stop the separate IranPriceScout tunnel on port 8765.
- PowerShell is host-privileged and not sandboxed. Prefer predefined checks and bounded tools.
- No production service installation, workspace ownership/DACL migration, Production handler switch, or broad runtime change without a separate explicit approval and reviewed rollback plan.
- Keep native filesystem candidates isolated until their full acceptance gate passes.

## Product architecture decision — 2026-11-05
Raya MCP stays independent. Raya Agent is a separate project/repository, initially reusing the full applicable Raya MCP codebase through a reviewed source import. Agent-specific orchestration, approvals, planning, context and UX are added in Raya Agent. At runtime, Agent uses the versioned public MCP interface; it does not depend on MCP private internals. The MCP repo remains independently buildable and usable. The read-only import inventory/manifest is now recorded in `docs/RAYA-AGENT-IMPORT-MANIFEST-20261105.md`. Do not copy the dirty working tree blindly or include credentials, machine-specific settings, local tunnel profiles, generated outputs, or unreviewed native prototypes. Next: review the modified tracked code diffs and selectively classify untracked broker/security source before making a clean staging export. See `docs/ARCHITECTURE-DECISION-RAYA-MCP-AND-AGENT-20261105.md`.

This is a parallel product-architecture track, not permission to bypass the active Phase 1 security gate. Raya MCP Phase 1 remains NOT ACCEPTED; no production filesystem integration, service deployment, or workspace ACL/owner migration is authorized by this decision.

## Phase order and gates

### Phase 1 — filesystem confinement and broker security (ACTIVE; NOT ACCEPTED)
Goal: prove every exposed filesystem operation stays within approved roots under reparse, namespace, hard-link, ACL, race, cancellation, and crash conditions.
Next work:
1. Continue the disposable broker acceptance work; specifically design a deterministic response-write disconnect test and complete pipe ACL / client identity / remote-client negative tests.
2. Resolve whether the dedicated service identity architecture is the approved long-term boundary. The design proposal is not production approval.
3. Complete operation-family matrix for read, read-tail, metadata, enumeration/search, create, publish, edit, rename/move, delete and directory traversal. Enforce the approved NumberOfLinks==1 policy on the same opened HANDLE; reject unknown or multi-link files.
4. Resolve or explicitly fail closed on metadata READ_ATTRIBUTES authorization behavior.
5. Test crash/restart and cleanup at commit boundaries; validate on supported providers beyond NTFS before making portability claims.
6. Only after all gates pass, propose a separate production migration/rollback plan. Do not integrate current prototypes early.

Acceptance: complete positive/negative/boundary, race/concurrency, ACL/identity, crash/cancellation, cleanup/HANDLE accounting, compatibility and regression suites; npm lint/test and all applicable native suites pass; docs and risk report updated. Phase 1 stays open while any required check is blocked or failed.

### Phase 2 — safe filesystem operations (BLOCKED BY PHASE 1)
Integrate only accepted primitives and operation contracts. No path-based fallback for a security-sensitive operation. Add per-tool tests and explicit overwrite/commit semantics.

### Phase 3 — process and command lifecycle
Improve launch, bounded output, session expiry, cancellation, identity-checked termination, cleanup and process observability. Preserve the host permission model; require confirmation for destructive actions.

### Phase 4 — developer/workspace capabilities
Reliable project discovery, Git/test workflows, diagnostics and multi-step development tasks, with bounded output and workspace allowlists.

### Phase 5 — MCP management and observability
Tool schema/catalog validation, roots/config visibility, health/diagnostics, session/search lifecycle, structured errors, client compatibility and recovery.

### Phase 6 — integrated security and reliability
Cross-tool adversarial testing, resource exhaustion, concurrency, reconnect/soak, dependency review, logs and recovery acceptance.

### Phase 7 — setup, UX and compatibility
Documented install/update/uninstall, client onboarding, clean-machine setup, troubleshooting, backup and rollback.

### Phase 8 — release and maintenance
Versioned release candidate, supported environment matrix, known limitations, security review, upgrade/rollback and maintenance checklist.

## Verified baseline on 2026-11-05
- Tunnel restarted by user; 25/25 sequential low-risk get_config calls succeeded after restart.
- /healthz and /readyz returned 200; /health/mcp initialized and child running. tools_list observer remains false because forwarded discovery has not been retained; historical local protocol test returned 45 tools, and current npm smoke test enumerates 47 tools.
- Runtime config: maxSessions=32, maxSearches=8, searchMaxResults=500, searchMaxDepth=8; file read/write 256 KiB; aggregate read 1 MiB; output cap 131072 chars.
- npm_lint passed.
- First npm_test exposed a stale smoke-test expectation (64 sessions). The expected value was aligned to the configured 32; npm_test then passed: 46 core smoke checks, 34 format/config checks, HTTP auth (401 blocked / authenticated initialize 200), and 36 URL-security checks.
- Managed process sessions=0; active searches=0 after cleanup.
- Git working tree contains many pre-existing modified/untracked files; preserve them.

## Current blockers / truthful status
- Production filesystem handlers remain path-based and are not accepted as race-free.
- Strict hard-link exclusion is not proven for several operation families; share modes and same-user DACL deny are insufficient.
- Metadata READ_ATTRIBUTES denial behavior remains unresolved.
- The dedicated-identity broker is an isolated prototype only. Pipe identity/remote/DACL negatives, deterministic pending-write disconnect, crash/tamper, full operation families, provider portability and formal review remain open.
- No production integration, real workspace ACL/owner migration, or service deployment is authorized by this plan.

## Immediate next action
Continue Phase 1 from the latest broker handoff: design and run a deterministic test for client disconnect while the broker is in a pending response write, then finish the named-pipe authorization negative tests. Use only the disposable service and fixture, verify cleanup, and update the supplemental handoff. If the test requires a new service installation or protected ownership change beyond the previously approved disposable prototype, stop and ask before doing it.
