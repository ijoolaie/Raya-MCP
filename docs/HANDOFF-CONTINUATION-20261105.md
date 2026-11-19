# Raya MCP continuation handoff — 2026-11-05

## Accepted / verified in this continuation
- User approved a staged execution plan; see `docs/RAYA-EXECUTION-PLAN-20261105.md`.
- Tunnel restart was completed by the user. Post-restart 25/25 sequential low-risk `get_config` requests succeeded.
- Local health endpoints `/healthz` and `/readyz` returned HTTP 200; `/health/mcp` reports initialized and child running.
- The `tools_list.ok=false` health field is an observer of forwarded `tools/list` traffic, not proof that the catalog is absent. Current local npm smoke test discovers 47 tools.
- Runtime config: maxSessions=32, maxSearches=8, searchMaxResults=500, searchMaxDepth=8; text read/write 256 KiB; aggregate read 1 MiB; outputChars=131072.
- Initial npm test failure was a stale smoke-test assertion expecting maxSessions=64. Changed only that assertion to 32, matching the approved live/on-disk configuration.
- `npm_lint`: exit 0.
- `npm_test`: exit 0 after the assertion correction: 46 core smoke checks, 34 format/config checks, HTTP unauthorized blocked / authenticated initialize 200, 36 URL-security checks.
- Managed process sessions and search sessions are empty after cleanup.
- Git working tree has extensive pre-existing modified and untracked files. They were preserved; no reset, cleanup, commit, or branch change was performed.

## Current project status
- Phase 1 — filesystem confinement: IN PROGRESS / NOT ACCEPTED.
- Production filesystem handlers remain path-based and are not accepted as race-free.
- Strict NumberOfLinks==1 policy is approved, but hard-link exclusion across all operation families and mutation commit boundaries is not proven.
- Metadata READ_ATTRIBUTES authorization remains unresolved.
- Dedicated-identity filesystem broker is an isolated disposable prototype, not Production approval.
- Do not change Production handlers, install a persistent service, migrate real workspace owners/DACLs, or change unrelated tunnels without separate explicit approval.
- Never touch the separate IranPriceScout tunnel on port 8765.

## Immediate next action
Continue from the latest broker result (2026-11-05 ACK-timeout window test): design a deterministic client-disconnect test while the broker is in a pending response WriteFile. Then finish named-pipe client identity, remote-client and explicit pipe-DACL negative tests. Use disposable service/fixture only, capture exact exit codes and logs, and verify service/process/fixture/image cleanup after every run. If a test requires a new protected ownership change or exceeds the previously approved disposable prototype scope, stop and ask.

## Phase gates after that
1. Complete broker authorization and lifecycle acceptance, including crash/tamper and bounded slow/fragmented input.
2. Complete operation-family matrix: read, read-tail, metadata, enumeration/search, create, publish, edit, rename/move, delete, directory traversal; same-HANDLE hard-link policy and fail-closed behavior.
3. Resolve metadata authorization and provider support.
4. Only after all Phase 1 criteria pass, prepare a separate Production migration/rollback plan for explicit approval.
5. Then Phase 2 safe filesystem operations; Phase 3 process lifecycle; Phase 4 developer/workspace capabilities; Phase 5 MCP management/observability; Phase 6 integrated security/reliability; Phase 7 setup/compatibility; Phase 8 release/maintenance.

## Constraints
- Use sequential MCP operations during stability validation.
- PowerShell is host-privileged and not sandboxed; use predefined checks and bounded commands whenever possible.
- Do not claim a phase is complete when any required test is failed, blocked, or unverified.
