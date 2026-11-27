# Raya Personal Agent — Architecture and Delivery Roadmap

Status: architecture proposal; no runtime behavior changed.
Branch: `feature/personal-agent-architecture`
Base: `main`

## Executive decision

Extend the existing private `ijoolaie/Raya-MCP` repository. Do not create a second repository or duplicate the MCP bridge: the repository already exists and is the source of truth.

Keep Raya MCP as the local capability/control plane. Add a separately permissioned agent layer that plans tasks and calls narrowly scoped MCP tools. The model is replaceable: OpenAI can be the high-capability reasoning provider, while an optional local model can handle eligible low-risk tasks. The software can be free to run locally, but OpenAI API inference is usage-billed; a ChatGPT subscription does not include API credits.

## Verified baseline (reconciled 2026-11-27)

- Private GitHub repository: `ijoolaie/Raya-MCP`, default branch `main`.
- Local Windows checkout: `C:\Users\Ali\local-mcp-bridge`.
- The modular Local MCP acceptance check on 2026-11-27 completed MCP initialize and `tools/list` successfully and returned 47 tools. This is an isolated local acceptance result, not a claim that every tool has independent security acceptance.
- Implemented tool families include workspace/file operations, bounded process sessions, search, configuration/diagnostics, HTTPS text retrieval, and PDF/DOCX/XLSX adapters.
- The local test run passed: 42 core checks, 34 format/config checks, HTTP unauthenticated/authenticated initialization checks (401/200), and 36 URL-security checks. These are suite counts, not proof that every tool has exhaustive acceptance coverage.
- The local checkout has uncommitted changes on `security/hardlink-alias-policy-probe`. Preserve them; do not reset, stash, overwrite, or mix them into this feature branch.
- Phase 1 filesystem confinement is explicitly **IN PROGRESS / NOT ACCEPTED**. The isolated native prototypes are test-only and are not approved for production integration. Path-based production handlers still have unresolved race/TOCTOU risk.
- PowerShell execution is disabled by default. When enabled it runs with the Windows account's privileges and is not a sandbox.
- Dependency remediation is now present on `main` after PR #8; PR #9 records the remediation and CI evidence. Dependabot alert #1 still requires a direct GitHub Security status check before it can be declared closed.
- PR #10 merged on 2026-11-27 and restricts the Admin Panel Tunnel Client child-process environment to required Windows runtime variables plus the explicit `CONTROL_PLANE_API_KEY`; this is Bridge hardening, not filesystem-confinement acceptance.
- PR #11 merged on 2026-11-27 as `c967dcb` after its updated GitHub Actions CI completed successfully. It extracted the hard-link and live ACL-denial probes from the superseded stacked PR #2. Both native addons compiled and both isolated NTFS tests passed on one Windows x64 / Node.js 22.23.2 host. This remains one-host research evidence only; ReFS/SMB/removable/cloud providers and other operation families remain untested. The probes remain research/acceptance assets and are not connected to production handlers.
- PR #7 was closed without merge because its root-level Admin Panel and pre-modular architecture conflicted with the current package layout. Its useful environment-isolation work is in PR #10; current curation rules are in merged PR #12.
- PR #2 and PR #6 were closed without merge because they were stacked on the closed PR #1 and contained superseded/mixed baselines; PR #11 preserves only the narrow NTFS research assets.

## Target architecture

1. **Raya MCP Core (local capability plane)**
   - Owns device-local tools, allowlisted roots, process/session lifecycle, bounded I/O, structured errors, and audit metadata.
   - Tools remain narrow and schema-validated. Do not add a generic unrestricted shell or unrestricted filesystem tool.
   - Enforce authorization and risk classification inside the local server, not only in model prompts.

2. **Raya Agent Runtime (planner/orchestrator)**
   - Maintains task state, plans, tool-call budgets, cancellation, retries, and user-visible progress.
   - Uses OpenAI Responses API with MCP/tool calling when configured.
   - Supports a pluggable local inference provider for eligible tasks; provider selection must not weaken tool authorization.
   - Persist only explicit, useful memory with retention/deletion controls; never persist credentials or raw secrets.

3. **Desktop interaction adapter (Windows)**
   - Add a dedicated, opt-in UI automation adapter using Windows UI Automation / approved accessibility interfaces.
   - Expose semantic actions (inspect window, locate control, click, type, key chord, screenshot) with target validation, timeouts, and bounded output.
   - Keep browser automation as a separate adapter with domain/session boundaries.
   - Do not treat arbitrary PowerShell or process execution as a substitute for safe UI automation.

4. **Mobile companion (Android first)**
   - Use an explicit companion app and Android user-granted permissions (for example, Accessibility only where essential and clearly disclosed).
   - Pair devices using short-lived, revocable credentials and encrypted transport. Never expose the local MCP endpoint directly to the public internet.
   - Keep phone actions user-visible and permission-scoped. iOS is a later, more restricted target.

5. **Remote connectivity**
   - Prefer outbound authenticated tunnel or a mutually authenticated private channel. Bind local HTTP to loopback by default.
   - Add device pairing, replay protection, rate limits, revocation, and an audit trail before remote actions are enabled.
   - Do not reuse or modify unrelated tunnels/services.

## Risk tiers and confirmation policy

- **Tier 0 — Read-only:** inspect approved workspace metadata, read selected files, list windows, inspect page content.
- **Tier 1 — Reversible local changes:** create/edit a new file, type into a selected field, run an allowlisted test. Require narrow scope and undo/backup where practical.
- **Tier 2 — External or consequential actions:** send messages, publish content, install software, modify system settings, or interact with accounts. Require a clear user confirmation immediately before execution.
- **Tier 3 — High impact:** payments/transfers, credential changes, broad deletion, security-control changes, destructive disk operations. Never execute autonomously; require explicit confirmation and, where appropriate, keep the action outside the agent's authority.

Treat page text, files, emails, and tool output as untrusted data, not as instructions. Protect against prompt injection by separating user intent from retrieved content and requiring policy checks before every action.

## Delivery phases and gates

### Gate 0 — Preserve and establish a clean baseline
- Keep all existing dirty local work untouched.
- Resolve/merge the dependency remediation through its own review path.
- Reconcile local and remote commit state before any implementation branch is checked out or merged.
- Keep the native filesystem prototype isolated until its acceptance gate is complete.

### Phase 1 — Filesystem confinement (blocking prerequisite)

**Source-to-contract audit — 2026-11-27:** the current `main` handlers were compared with the accepted handle-relative / strict single-link contract. All 15 registered filesystem operations/modes remain **NOT ACCEPTED** for confinement: `workspace_roots`, `workspace_list`, `file_read`, `file_read_chunk`, `file_write`, `file_write_chunk`, `create_directory`, `get_file_info`, `read_multiple_files`, `list_directory`, `move_file`, `edit_block`, `start_search` filename mode, `start_search` content mode, and `read_file_tail`. The primary gaps are path-based check/use races, missing same-HANDLE type/reparse/link-count validation, path-based enumeration, and the absence of accepted identity-safe rename/edit semantics. This is a source-to-contract assessment, not a claim that every operation is exploitable under every schedule. It does not change production code or the Phase 1 gate.

- Complete root identity binding and request-scoped handle lifecycle.
- Complete root identity binding and request-scoped handle lifecycle.
- Resolve the strict hard-link alias policy with an explicit documented decision.
- Complete operation-family race/cleanup tests for read, write, create, directory, metadata, search/enumeration, rename/move, and delete/publish as applicable.
- Exercise permission-denied behavior and provider boundaries; record unsupported providers explicitly.
- Integrate no prototype into production until the full phase acceptance suite passes.
- Required outcome: production filesystem operations have evidence-backed containment guarantees and fail closed.

### Phase 2 — Authorization and policy kernel
- Add per-tool risk metadata, user-granted capabilities, expiring grants, cancellation, and policy enforcement.
- Add audit events that exclude secrets and sensitive file contents.
- Test prompt-injection, confused-deputy, path escape, replay, and privilege-escalation scenarios.

### Phase 3 — Windows desktop interaction (first user-visible agent capability)
- Build a read-only window/control inventory first.
- Add click/type/key operations with strict target binding and confirmation tiers.
- Add screenshot capture only on explicit task need; bound image size and retention.
- Acceptance: tests for stale window handles, focus changes, modal dialogs, timeouts, cancellation, and unintended-target prevention.

### Phase 4 — Browser and web workflows
- Add isolated browser sessions, domain allowlists, download quarantine, and credential isolation.
- Treat page instructions as untrusted; require confirmation for submissions/purchases/messages.
- Acceptance: cross-origin, prompt-injection, download, authentication-expiry, and session cleanup tests.

### Phase 5 — Agent runtime and model providers
- Add bounded plan/act/observe loops, budgets, checkpoints, retries, and human handoff.
- Integrate OpenAI Responses API / MCP with explicit cost limits and a kill switch.
- Add optional local model provider; make offline behavior and capability differences explicit.
- Acceptance: tool-call schema, model timeout, provider failure, cost cap, cancellation, and loop prevention tests.

### Phase 6 — Android companion and secure pairing
- Implement read-only device status first, then user-approved UI actions.
- Use revocable pairing, encrypted transport, permission diagnostics, and remote wipe/revoke.
- Acceptance: unauthorized pairing, revoked token, offline device, permission removed, duplicate/replayed request, and device loss tests.

### Phase 7 — Voice, memory, and release
- Add push-to-talk/wake interaction only with explicit microphone controls and visible recording state.
- Add user-inspectable memory with edit/forget workflows and retention limits.
- Package installation, updates, recovery, telemetry opt-out, and clean-machine acceptance.

## Immediate next actions

1. Preserve the current dirty local checkout and its native/security experiments.
2. Review the existing handoff and phase-1 acceptance evidence; do not advance production filesystem code while Phase 1 is not accepted.
3. Confirm GitHub Security alert #1 and document the default-branch dependency graph.
4. Prepare a clean implementation worktree from the chosen reviewed base before code changes.
5. Start Phase 2 only after the Phase 1 gate is accepted; until then, architecture/design and isolated tests only.

## Non-goals for the first release

- Unrestricted control of every application or every phone permission.
- Publicly exposing a local MCP server or storing API keys in source/config files.
- Autonomous payments, credential changes, broad deletion, or security-setting changes.
- Promising zero cost while using paid OpenAI API inference.
- Claiming iOS parity with Android or Windows desktop control.

## Acceptance definition

A capability is not complete because a tool exists or a happy-path demo works. Each phase needs documented positive, negative, boundary, permission, concurrency/race, failure/cleanup, cancellation, and compatibility evidence appropriate to its threat model. Report `planned`, `implemented`, `tested`, and `verified` distinctly.
