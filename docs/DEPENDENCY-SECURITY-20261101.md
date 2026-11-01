# Dependency security review — 2026-11-01

## Status

**Open / not remediated.** GitHub Dependabot created alert #1 after the initial repository push. It is a moderate-severity transitive npm dependency advisory. Do not treat the earlier 2026-10-26 zero-vulnerability audit recorded in README as current evidence.

- Repository alert: https://github.com/ijoolaie/Raya-MCP/security/dependabot/1
- Advisory: https://github.com/advisories/GHSA-g3hv-75wg-m249
- CVE: CVE-2026-118764
- Affected range: `minimatch < 11.0.1`
- Patched version: `11.0.1`
- Impact: potential CPU/memory denial of service through globparts amplification when attacker-controlled glob patterns are processed under the affected optimization path.

## Local evidence

Environment: Windows, Node.js v22.23.2, npm; project directory `C:\Users\Ali\local-mcp-bridge`.

`npm ls minimatch glob archiver exceljs --all` returned this dependency tree:

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

- `npm audit --json`: 8 moderate vulnerable package records, 0 high, 0 critical. The records are in the same transitive chain (archiver, archiver-utils, exceljs, glob, minimatch, readdir-glob, rimraf and zip-stream).
- GitHub Dependabot groups the minimatch advisory as one open alert.
- `npm audit fix --dry-run --json` did not produce a compatible update. npm proposed a semver-major downgrade of ExcelJS to 4.1.1; this was not applied.
- No dependency, lockfile, or application source was modified during this review.
- The vulnerable pattern-processing path's reachability from Raya's actual XLSX workflows has not been established. Do not claim either confirmed exploitability or confirmed non-exploitability.

## Required remediation

1. Inspect upstream release compatibility for `exceljs`, `archiver`, `archiver-utils`, `glob`, and `readdir-glob`.
2. Test any candidate override or dependency upgrade in an isolated copy first. Avoid blindly forcing minimatch 11 into consumers that declare older major-version ranges.
3. Preserve supported XLSX behavior: workbook create/read, bounded range read, workbook search, cell write, and formula write.
4. Run the full `npm run lint` and `npm test` suites, plus `npm audit`, after a candidate change.
5. Keep Dependabot alert #1 open until the affected dependency instances are upgraded or a documented, technically justified mitigation is accepted.

## Baseline before remediation

- `npm run lint`: passed.
- `npm test`: passed (42 core checks, 34 format/config checks, HTTP unauthorized/authenticated initialize, 36 URL-security checks).
- These test results predate any dependency remediation; no remediation has been applied yet.
