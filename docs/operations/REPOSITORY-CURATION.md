# Repository Curation and Safe Local Development

**Reviewed:** 2026-11-27  
**Repository:** `ijoolaie/Raya-MCP`  
**Architecture baseline:** Raya MCP Stack (Core, Local MCP, Bridge)

## Purpose

Keep the Git repository limited to reviewed product source, reproducible tests, durable documentation and explicitly classified research. A local working directory may contain user data, credentials, experiments and unrelated projects; its contents must never be bulk-staged or pushed merely because they exist beside the repository.

## Preserve local working state

- Before any local operation, inspect the exact repository path, branch and working-tree status.
- If a working tree is dirty, do not run `git reset`, `git clean`, broad checkout, stash, bulk stage, or broad formatting operations.
- Do not overwrite, move, rename or delete modified or untracked files without explicit authorization.
- Perform new work on a clean branch/worktree or through reviewed GitHub branch changes. Keep acceptance fixtures in isolated temporary directories and remove only fixtures created by that test.
- `.gitignore` is a safety net, not a cleanup tool. Ignored files remain on disk and must not be deleted automatically.

## Product and architecture boundaries

- The product family is Raya MCP Stack: Raya MCP Core, Raya Local MCP and Raya Bridge. Preserve their package boundaries and dependency direction as documented in the current root README and package manifests.
- Admin Panel and tunnel integration belong to the Bridge package under `packages/raya-bridge/`; do not reintroduce the superseded pre-modular root-level `raya-admin-panel/` implementation.
- The external Tunnel Client is a separately distributed dependency. Do not vendor its executable, archive, generated logs or third-party source. Record verified version, provenance, checksum and license information in dependency documentation.
- Native filesystem probes and broker prototypes are research/acceptance artifacts, not Production enforcement. Keep them clearly labelled and out of runtime packages until their security gates are independently accepted.

## Never commit

- Live configuration, credentials, tokens, environment dumps, cookies or DPAPI material.
- User-specific workspace root lists, local absolute paths, runtime state, logs, diagnostic exports or process snapshots.
- `node_modules`, compiled native binaries, generated build output, temporary acceptance fixtures, checkpoints, backups or unrelated workspaces.
- Third-party Tunnel Client binaries or archives.

Use sanitized example templates only after reviewing their schema and confirming that they contain placeholders rather than real roots, credentials or machine-specific paths.

## Testing and validation

- Prefer isolated tests with disposable fixtures and explicit cleanup.
- Before running any test suite, inspect its scripts and side effects. Do not run a command against a live user configuration if it can rewrite that configuration.
- A passing syntax check or general CI workflow does not establish native filesystem security acceptance. Native NTFS/ACL/hard-link claims require the corresponding test to execute on a supported Windows/NTFS host.
- Record evidence precisely: distinguish CI success, local test execution, manual observation, unknown state and historical evidence.
- Do not describe a research prototype as Production-ready or claim race-free filesystem confinement until the complete acceptance gates and independent review pass.

## Current curation rule

This document records safe handling policy only. It does not authorize deleting or moving local files, does not assert that any dirty working tree is clean, and does not certify Production security acceptance.
