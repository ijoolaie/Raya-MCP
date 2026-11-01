# Agent Rules â€” Raya Local MCP Bridge

Mission: implement reliable Desktop Commander-like user outcomes. Follow HANDOFF.md and docs/DESKTOP_COMMANDER_PARITY.md.

1. Inspect source, tests, package scripts and live state before editing.
2. Status labels: planned, implemented, tested, verified, not applicable. Never infer verification from code existence.
3. Small reversible changes. Preserve backups and all pre-existing working-tree changes. This directory is a Git repository; inspect status/diff before editing, never overwrite or discard uncommitted changes, and never invent commits or PRs.
4. Scope changes to this directory unless authorized. Never change/stop the separate IranPriceScout tunnel on port 8765.
5. Never print/log/commit API keys, tokens, cookies, or sensitive environment values.
6. Shell/PowerShell is privileged and not sandboxed; cwd checks do not confine arbitrary commands.
7. File tools must resist traversal, symlink/reparse-point escapes and races; enforce size limits, safe overwrite and exact edit counts.
8. Never kill by process name. Verify managed session/PID identity and avoid unrelated processes.
9. Bound output, duration, concurrency, search results, session lifetime and document size.
10. Return structured errors; timeout is not success. Ensure cancellation and cleanup.
11. Every feature needs success, invalid-input, permission, limit and lifecycle tests as relevant.
12. Update README, parity matrix and handoff whenever behavior changes.
13. Destructive actions, broad edits, config mutation and forceful termination need explicit intent and narrow scope.
14. Validate MCP schemas, result shapes, transport behavior and client compatibility.
15. Report exact checks/evidence; separate verified facts from assumptions.

Definition of done: requirements/threat model reviewed; implementation/tests complete; relevant checks pass; security and failure paths tested; docs updated; no unrelated service changed.

Workflow: read handoff -> inspect -> implement one vertical slice -> test -> review for secrets/unintended changes -> update status/docs -> report evidence and next step.

## Mandatory phase execution and handoff policy (2026-10-30)
- Follow `docs/PROJECT-ROADMAP.md` as the binding phase sequence and `docs/CURRENT-STATUS.md` as the authoritative live snapshot.
- Deliver work phase by phase. Each phase must have explicit scope, outputs, dependencies, and acceptance criteria.
- Execute pragmatically and avoid unnecessary blockers or ceremony; this does not lower security, correctness, data-integrity, or acceptance standards.
- At the end of EVERY phase, run a comprehensive, phase-relevant acceptance suite: positive/negative/boundary, adversarial race/concurrency, failure/cleanup, compatibility, and regression checks as applicable.
- A phase is not complete on implementation or basic tests alone. Record exact checks, environment, results, skipped/blocked tests, limitations, and residual risks. A blocked check is not a pass.
- Continuously update `docs/CURRENT-STATUS.md`, `docs/PROJECT-ROADMAP.md`, and `HANDOFF.md`; update relevant technical/parity/user docs whenever behavior or evidence changes. At every phase boundary, update all three core records before transferring to the next phase.
- Every handoff must state accepted deliverables, evidence, unresolved defects/risks, exact next action, and constraints. The next phase starts from that handoff.
- If acceptance fails, keep the phase open, fix the findings, and rerun the complete relevant suite. Do not waive failures to advance.
- Preserve the existing scope restrictions: no unrelated workspace/service/tunnel/config changes, no secret disclosure, and no cleanup of uncertain pre-existing fixtures.

Status: planned=specified; implemented=code exists but insufficient tests; tested=automated tests pass in stated environment; verified=automated plus manual acceptance pass; not applicable=excluded with rationale.

## Current implementation/testing note (2026-10-23)
- Tool families include process sessions, filesystem operations, progressive search, persistent allowlisted runtime settings, HTTPS URL retrieval, XLSX/DOCX/PDF helpers, and HTTP authentication tests.
- Current full local test run: npm run check and npm test passed; 45 tools listed, 25 core checks and 34 format/config checks, plus HTTP 401/200 verification.
- Do not claim full parity. Formatting-preserving DOCX edits, XLS/XLSM, arbitrary PDF editing, complex-script PDF fonts, and client-specific UI actions remain gaps.
- npm reported two moderate dependency vulnerabilities; inspect advisories before production use.


