# Dependency security review — 2026-11-01

## Status

**Candidate remediation implemented and locally verified on branch `security/minimatch-11-override`; not yet merged.** Keep GitHub Dependabot alert #1 open until the change is merged and the default-branch alert is confirmed resolved.

- Repository alert: https://github.com/ijoolaie/Raya-MCP/security/dependabot/1
- Advisory: https://github.com/advisories/GHSA-g3hv-75wg-m249
- CVE: CVE-2026-118764
- Affected range: `minimatch < 11.0.1`
- Patched version: `11.0.1`
- Impact: potential CPU/memory denial of service through globparts amplification when attacker-controlled glob patterns are processed under the affected optimization path.

## Initial dependency path

Before remediation, `npm ls minimatch glob archiver exceljs --all` showed:

```text
exceljs@4.4.0
└── archiver@5.3.2
    ├── archiver-utils@2.1.0
    │   └── glob@7.2.3
    │       └── minimatch@3.1.5
    ├── readdir-glob@1.1.3
    │   └── minimatch@5.1.9
    └── zip-stream@4.1.1
        └── archiver-utils@3.0.4
            └── glob@7.2.3
```

`npm audit` reported eight moderate transitive package records in this dependency chain, zero high and zero critical.

## Remediation selected

A global minimatch-only override to 11.0.1 produced `npm audit: 0 vulnerabilities`, but it was rejected: an executable `glob@7` sync smoke test failed because that old major version depends on the legacy Minimatch internal structure. The failed experiment was not retained as the final solution.

The working candidate upgrades the legacy transitive consumers alongside minimatch:

- `archiver` overridden from 5.3.2 to 8.0.0.
- `unzipper` overridden from 0.10.14 to 0.12.5.
- `minimatch` overridden to patched 11.0.1.
- Existing `uuid` override 11.1.1 retained.

This removes the old `glob@7` / `rimraf@2` dependency path from the installed ExcelJS tree. The resulting dependency tree is:

```text
local-mcp-bridge
└── exceljs@4.4.0
    ├── archiver@8.0.0 (overridden)
    │   └── readdir-glob@3.0.0
    │       └── minimatch@11.0.1 (overridden)
    └── unzipper@0.12.5 (overridden)
```

## Local verification

Environment: Windows, Node.js v22.23.2, npm 10.9.8.

- `npm install --ignore-scripts`: completed successfully and updated the lockfile.
- `npm ls minimatch glob archiver archiver-utils readdir-glob unzipper rimraf --all`: shows only the upgraded Archiver 8 / readdir-glob 3 / minimatch 11 / Unzipper 0.12.5 path for ExcelJS.
- `npm audit`: **0 vulnerabilities**.
- XLSX round-trip smoke: created a workbook, wrote a 6,440-byte XLSX buffer, reopened it, and verified cell B2 equals numeric value 42.
- `npm run lint`: passed (the project's Node syntax-check chain).
- `npm test`: passed: 42 core checks, 34 format/config checks (including XLSX write/read/range/search/formula paths), HTTP unauthorized/authenticated-initialize checks, and 36 URL-security checks.
- No Production application source or native filesystem handlers were changed.

## Remaining acceptance gates

1. Review the PR diff and run a clean-lockfile installation in CI or an isolated clean checkout before merge.
2. Verify Dependabot alert #1 is resolved on the default branch after merge; do not dismiss it manually as a substitute for remediation.
3. Keep this advisory documented in release notes/handoff until GitHub confirms the alert is closed.

## Important scope

This is a dependency-chain remediation. It does not establish whether the vulnerable glob pattern path was reachable from untrusted user-controlled input in the previous ExcelJS workflow; that reachability was not proven. The remediation removes the affected versions from the installed tree rather than relying on an unverified exploitability claim.

The local uncommitted change in `.native-probe/hardlink-sharing-experiment.c` is unrelated and intentionally preserved outside this dependency PR.
