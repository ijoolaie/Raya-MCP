# Dependency security review — updated 2026-11-27

## Current status

**The dependency remediation is present on the default branch; the GitHub alert state is not independently verified.**

The default-branch `package.json` contains these overrides:

- `archiver`: `8.0.0`
- `unzipper`: `0.12.5`
- `minimatch`: `11.0.1`
- Existing `uuid`: `11.1.1`

The changes were first validated on `security/minimatch-remediation`. The modular package update was subsequently merged through PR #8 on 2026-11-26 (merge commit `736baad87491ddabfa3c775e6add07e7e3754aef`). The PR's Windows / Node.js 22 workflow completed successfully, including its `npm audit` step.

This is evidence that the merged change passed the PR CI path. It is **not** a direct query of the current GitHub Dependabot alert, and this document does not claim that alert #1 is closed. Confirm its state under GitHub Security before recording the advisory as fully resolved.

- Repository alert: https://github.com/ijoolaie/Raya-MCP/security/dependabot/1
- Advisory: https://github.com/advisories/GHSA-g3hv-75wg-m249
- CVE: CVE-2026-118764
- Affected range: `minimatch < 11.0.1`
- Patched version: `11.0.1`

## Remediation rationale

A global `minimatch@11.0.1` override by itself was rejected after an executable `glob@7` sync smoke test failed because that older major depends on the legacy Minimatch internal structure. The selected remediation upgrades the legacy transitive consumers together:

- `archiver@8.0.0`
- `unzipper@0.12.5`
- `minimatch@11.0.1`

This removes the old `glob@7` / `rimraf@2` path from the ExcelJS dependency tree while retaining the existing `uuid@11.1.1` override.

## Historical verification evidence

Environment: Windows, Node.js v22.23.2, npm 10.9.8.

The remediation branch recorded the following checks:

- `npm install --ignore-scripts` completed successfully and updated the lockfile.
- `npm ls minimatch glob archiver archiver-utils readdir-glob unzipper rimraf --all` showed the upgraded Archiver 8 / readdir-glob 3 / minimatch 11 / Unzipper 0.12.5 path for ExcelJS.
- `npm audit` reported zero vulnerabilities on the candidate branch.
- XLSX round-trip smoke passed: create workbook, write buffer, reopen, read B2=42.
- `npm run lint` and `npm test` passed on the candidate branch.
- A clean-checkout install verified `npm ci --ignore-scripts`, `npm audit`, lint and tests on that branch.
- PR #8 Windows CI also completed successfully after the modularization merge.

These results should be read with their stated branch and run context; they are not a live query of Dependabot's alert state.

## Remaining action

Check GitHub Security alert #1. If it is still open, inspect its affected package instances and confirm the default-branch dependency graph before dismissing or closing it. Do not manually dismiss it solely because the package overrides are present.

## Security boundary

This dependency remediation does not change the filesystem-security assessment. Phase 1 filesystem confinement remains **IN PROGRESS / NOT ACCEPTED**; native filesystem prototypes remain test-only; Production integration remains **NO-GO** pending the separate filesystem acceptance gates.
