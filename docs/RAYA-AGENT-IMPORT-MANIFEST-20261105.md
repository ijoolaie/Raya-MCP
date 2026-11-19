# Raya Agent Bootstrap - Source Inventory and Import Manifest

Date: 2026-11-05
Status: Initial read-only inventory completed; per-file review and import approval remain open.
Source repository: https://github.com/ijoolaie/Raya-MCP
Source working directory: C:\Users\Ali\local-mcp-bridge

## 1. Source identity and reproducibility

- Current branch: \`security/hardlink-alias-policy-probe\`
- Current HEAD: \`ef99ccec39f3eb318573421322df5e10bdc4335e\`
- HEAD subject/date: \`test(security): verify live NTFS ACL denials\` / 2026-11-01
- The branch tracks \`origin/security/hardlink-alias-policy-probe\`.
- Current branch is 8 commits ahead of \`origin/main\`; \`origin/main\` is \`16b018109fa66de0166f36af010b3e9cf187fd76\`.
- Working tree is not clean. Do not treat HEAD alone as the complete current implementation, and do not blindly export the working directory.
- Remote is the existing Raya MCP GitHub repository. No new repository has been created by this inventory.

A reproducible Agent bootstrap must record both the selected commit and a reviewed patch/import manifest for any intended working-tree changes. Prefer a clean export from the chosen commit plus explicit reviewed changes over copying the live working directory.

## 2. Inventory totals (observed 2026-11-05)

- Tracked files at HEAD: 109.
- Tracked files under \`.native-probe/\`: 79 (44 CJS tests/helpers, 12 C sources, 5 MJS tests/helpers, 13 CMD build scripts, 3 Markdown contracts/policies, 1 PowerShell script, 1 GYP build file).
- Tracked top-level production/runtime and CI files outside \`.native-probe/\`: server, tool modules, package metadata/lockfile, smoke tests, stdio launcher, URL security test, README/agent guidance, and CI workflow.
- Modified tracked files in working tree: 15 (749 insertions, 31 deletions at inventory time).
- Untracked non-ignored files: 117.
- Of the untracked files, 47 are under \`.native-probe/broker-prototype-20261103/\`; 16 match generated executable/object/log/output artifacts; 2 are local tunnel YAML backups.
- Ignored local artifacts also exist, including native executables/objects, build caches and other generated outputs.
- Existing \`.gitignore\` excludes node_modules, build outputs, downloads, security-validation scratch data, logs, local configuration, credentials, backups, compiled native artifacts, and editor/OS files. Preserve and strengthen these exclusions in the Agent import.

Counts are a point-in-time inventory, not a claim that every file has been content-reviewed.

## 3. Import classification

### A. Import as Raya MCP foundation (reviewed source baseline)

Import the tracked runtime and its tests/docs as the initial compatibility foundation:
- \`server.mjs\`
- \`advanced-tools.mjs\`
- \`format-tools.mjs\`, \`format-smoke-test.mjs\`
- \`url-tools.mjs\`, \`url-security-test.mjs\`
- \`smoke-test.mjs\`, \`http-smoke-test.mjs\`
- \`package.json\`, \`package-lock.json\`
- \`start-mcp-stdio.cmd\`
- \`.github/workflows/ci.yml\`
- \`AGENTS.md\`, \`README.md\`, \`docs/**\`, \`HANDOFF.md\`
- tracked \`.native-probe/**\` source, tests and contracts as a segregated security research/test area, not as production runtime dependencies.

The initial Agent bootstrap should preserve this foundation and provenance. The Agent's product entrypoint/configuration and orchestration are added in separate directories/modules; the MCP server remains independently runnable.

### B. Import, but quarantine from production runtime

The tracked \`.native-probe/**\` tree is substantial and contains valuable filesystem confinement/security research, tests, build scripts, and native experiments. Keep applicable source, test harnesses, and explanatory docs in the initial source archive/repository to satisfy provenance and avoid losing work, but:
- keep it under a clearly named research/test boundary;
- do not load it from the Agent runtime;
- do not include compiled outputs;
- do not present prototypes as production-safe;
- require explicit per-test prerequisites and cleanup documentation;
- do not run service-install or privileged ACL/owner-changing scripts during bootstrap.

### C. Untracked working-tree changes - review individually before import

There are 15 modified tracked files. Their combined diff is 749 insertions and 31 deletions. These changes include recent runtime, tests, package metadata, security notes, status and roadmap work. They are not yet approved as one indivisible import patch. Review each diff and select changes by purpose; preserve the original dirty Raya MCP working tree.

There are 117 untracked non-ignored files. In particular:
- the 47-file broker prototype folder contains useful source, clients, build scripts and design/results documents, but also test output files; selectively import source and durable test documentation only after review;
- additional hard-link/metadata ACL experiments may be valuable research, but require individual review and a stable reproduction/cleanup story;
- local YAML backups are not source and must not be imported;
- logs, output captures, binaries and temporary objects are excluded.

### D. Exclude from Raya Agent repository

Always exclude:
- secrets, credentials, access tokens, private keys, certificates, \`.env\` files;
- local tunnel profile/configuration and its backups;
- user data, workspace contents and machine-specific absolute paths where not required as examples;
- logs, traces, crash dumps, generated JSON captures and temporary outputs;
- \`node_modules/\`, \`build/\`, \`dist/\`, caches, compiled \`.exe/.obj/.dll/.node/.pdb/.lib/.exp\` artifacts;
- temporary folders, backups, scratch experiments and service fixtures;
- any binary whose source and reproducible build instructions are not included and reviewed.

## 4. Product boundaries in the imported tree

- Raya MCP owns MCP transport/protocol, tool schemas/catalog, local capability adapters, bounded execution, MCP sessions/search lifecycle and MCP diagnostics.
- Raya Agent owns agent runtime, planning, task decomposition, tool selection/orchestration, context/memory policy, user interaction, consent/approval, cancellation coordination and Agent UX.
- Agent-to-MCP communication uses the public, versioned MCP interface. Agent must not import MCP server private modules as a runtime shortcut.
- Raya MCP must not depend on Raya Agent and must remain independently buildable/testable/releasable.

## 5. Security and release gates

- Raya MCP filesystem confinement Phase 1 remains NOT ACCEPTED.
- The current broker is a disposable prototype; production filesystem handlers have not been accepted as race-free.
- Agent must not advertise or expose unresolved filesystem operations as secure merely because they are callable.
- The Agent bootstrap does not authorize production service deployment, production handler replacement, workspace ownership/DACL migration, or privileged filesystem changes.
- Agent must start with conservative tool allowlisting and approval gates for consequential actions.

## 6. Recommended import procedure

1. Keep the current Raya MCP working tree untouched.
2. Select a source commit deliberately. Current candidate is \`ef99ccec39f3eb318573421322df5e10bdc4335e\`, but confirm whether the latest security branch work is the intended base.
3. Review the 15 tracked-file diffs and classify each as required, superseded, experimental, or excluded.
4. Review untracked source/docs separately; create a path-level allowlist for broker/security research. Exclude outputs and local YAML backups.
5. Build a clean staging export in a separate directory/repository, never in the Raya MCP working tree.
6. Run secret scanning and path/config scans on the staging export.
7. Run MCP lint/tests in staging and document any Windows-only test requirements.
8. Add Agent-specific package identity, entrypoint, config namespace, logging, approvals, orchestration and tests.
9. Create a separate private Raya Agent GitHub repository only after the staged import manifest and exclusions have been reviewed.
10. Verify that both repositories build and test independently, and record source commit/import provenance.

## 7. First-pass review notes for the 15 modified tracked files
The following production diffs were inspected at a high level; they are not yet approved for blanket import:

- `server.mjs`: adds chunked file read/write tools, a child-process environment allowlist, and UTF-8 byte-size enforcement. These are potentially reusable capabilities, but chunk-upload temp-file lifecycle, cancellation/cleanup, path races, overwrite behavior, and environment compatibility need focused review/tests before Agent adopts them.
- `advanced-tools.mjs`: changes process/session lifecycle and output-ring behavior, and the diff currently shows an in-module `MAX_SESSIONS=64` default while the approved runtime config observed by `get_config` reports `maxSessions=32`. Resolve this discrepancy before choosing the working-tree patch. Process-tree termination and safe child environment changes require dedicated regression coverage.
- `package.json`: expands native syntax checks and native test scripts. Some scripts invoke compiled `.exe` artifacts that are intentionally not imported; convert these to reproducible build-then-test workflows or keep them explicitly opt-in.
- `smoke-test.mjs`: adds chunk I/O and output-offset checks and asserts the live configured session limit of 32. Useful coverage to retain after code review.
- `start-mcp-stdio.cmd`: hardcodes this machine's absolute project path and a long list of personal workspace roots, and opts in to arbitrary PowerShell execution. Do not copy this launcher verbatim into Raya Agent. Replace with a generic installer/launcher and explicit per-user configuration/consent.
- `README.md`, `HANDOFF.md`, `docs/CURRENT-STATUS.md`, `docs/PROJECT-ROADMAP.md`, `docs/DESKTOP_COMMANDER_PARITY.md`, and the modified native-probe notes contain project history/status/design; review for personal paths, stale claims, and prototype-vs-production distinctions before import.

The above is a triage, not a code audit. Preserve these diffs in Raya MCP; do not revert or normalize them as part of Agent bootstrap.

### Broker prototype-specific triage
The 47 untracked broker-prototype paths are not one clean source bundle. Durable design/results/handoff documents and C/CJS/MJS source/build scripts may be retained under a quarantined research area after review. The `.out`, `.err`, captured client logs, binaries and intermediate objects are excluded. The setup/cleanup PowerShell scripts register/delete a Windows service and change protected ProgramData fixture/image ownership and ACLs; they must never be part of the Agent default build, install, or test path. The recorded prototype has useful read-only fixed-root evidence, but identity-negative and explicit pipe-DACL/remote denial tests remain unproven, and pending response-write disconnect is still open. Keep its Production NO-GO / Phase 1 NOT ACCEPTED qualification with every imported report.

## 8. Immediate next actions

- Review diffs for the 15 modified tracked files.
- Review the broker prototype's source/docs separately from its output captures.
- Produce a path-level inclusion/exclusion manifest with reasons.
- Identify the intended private GitHub repository name and verify it does not already exist before creation.
- Do not create or push a repository until the staging export has passed the import and secret checks.

## 9. Acceptance checklist

- [ ] Source commit and imported patch set are recorded.
- [ ] Every imported path has a classification.
- [ ] No credentials, tunnel config, user data, logs or compiled outputs are present.
- [ ] Research prototypes are segregated and clearly marked non-production.
- [ ] Agent and MCP package identities/configuration are separate.
- [ ] Agent runtime communicates through the versioned MCP contract.
- [ ] Raya MCP continues to build/test/run independently.
- [ ] Agent has independent tests for approvals, deny path, cancellation, timeouts, partial failures, reconnect and cleanup.
- [ ] Raya MCP Phase 1 security status remains accurately marked NOT ACCEPTED until its own gate passes.
## 10. Path-level disposition of the 117 untracked files

Generated from `git ls-files --others --exclude-standard` on 2026-11-05. This is a path inventory, not permission to execute any prototype. The 117 paths below are exhaustive for the observed non-ignored untracked set.

- `.native-probe/broker-prototype-20261103/HANDOFF-20261103.md` - QUARANTINE - broker research source/client/build notes; not runtime; manual security review required; no service install.
- `.native-probe/broker-prototype-20261103/RESULTS-20261103.md` - QUARANTINE - broker research source/client/build notes; not runtime; manual security review required; no service install.
- `.native-probe/broker-prototype-20261103/abrupt-disconnect-client.c` - QUARANTINE - broker research source/client/build notes; not runtime; manual security review required; no service install.
- `.native-probe/broker-prototype-20261103/ack-timeout-window-client.mjs` - QUARANTINE - broker research source/client/build notes; not runtime; manual security review required; no service install.
- `.native-probe/broker-prototype-20261103/broker-probe.c` - QUARANTINE / MANUAL REVIEW - early service/fixture prototype; never default build/install/test.
- `.native-probe/broker-prototype-20261103/build-abrupt-disconnect-client.cmd` - QUARANTINE - broker research source/client/build notes; not runtime; manual security review required; no service install.
- `.native-probe/broker-prototype-20261103/build-named-pipe.cmd` - QUARANTINE - broker research source/client/build notes; not runtime; manual security review required; no service install.
- `.native-probe/broker-prototype-20261103/build-no-read-client.cmd` - QUARANTINE - broker research source/client/build notes; not runtime; manual security review required; no service install.
- `.native-probe/broker-prototype-20261103/build-node-pipe-native-adapter.cmd` - QUARANTINE - broker research source/client/build notes; not runtime; manual security review required; no service install.
- `.native-probe/broker-prototype-20261103/build-node-pipe-transport.cmd` - QUARANTINE - broker research source/client/build notes; not runtime; manual security review required; no service install.
- `.native-probe/broker-prototype-20261103/build-node-raya-protocol-adapter.cmd` - QUARANTINE - broker research source/client/build notes; not runtime; manual security review required; no service install.
- `.native-probe/broker-prototype-20261103/build-partial-request-deadline-client.cmd` - QUARANTINE - broker research source/client/build notes; not runtime; manual security review required; no service install.
- `.native-probe/broker-prototype-20261103/build.cmd` - QUARANTINE - broker research source/client/build notes; not runtime; manual security review required; no service install.
- `.native-probe/broker-prototype-20261103/concurrent-client-1.err` - EXCLUDE - captured process output; generated artifact.
- `.native-probe/broker-prototype-20261103/concurrent-client-1.out` - EXCLUDE - captured process output; generated artifact.
- `.native-probe/broker-prototype-20261103/concurrent-client-2.err` - EXCLUDE - captured process output; generated artifact.
- `.native-probe/broker-prototype-20261103/concurrent-client-2.out` - EXCLUDE - captured process output; generated artifact.
- `.native-probe/broker-prototype-20261103/concurrent-client-3.err` - EXCLUDE - captured process output; generated artifact.
- `.native-probe/broker-prototype-20261103/concurrent-client-3.out` - EXCLUDE - captured process output; generated artifact.
- `.native-probe/broker-prototype-20261103/concurrent-client-4.err` - EXCLUDE - captured process output; generated artifact.
- `.native-probe/broker-prototype-20261103/concurrent-client-4.out` - EXCLUDE - captured process output; generated artifact.
- `.native-probe/broker-prototype-20261103/concurrent-client-5.err` - EXCLUDE - captured process output; generated artifact.
- `.native-probe/broker-prototype-20261103/concurrent-client-5.out` - EXCLUDE - captured process output; generated artifact.
- `.native-probe/broker-prototype-20261103/concurrent-client-6.err` - EXCLUDE - captured process output; generated artifact.
- `.native-probe/broker-prototype-20261103/concurrent-client-6.out` - EXCLUDE - captured process output; generated artifact.
- `.native-probe/broker-prototype-20261103/concurrent-client-7.err` - EXCLUDE - captured process output; generated artifact.
- `.native-probe/broker-prototype-20261103/concurrent-client-7.out` - EXCLUDE - captured process output; generated artifact.
- `.native-probe/broker-prototype-20261103/concurrent-client-8.err` - EXCLUDE - captured process output; generated artifact.
- `.native-probe/broker-prototype-20261103/concurrent-client-8.out` - EXCLUDE - captured process output; generated artifact.
- `.native-probe/broker-prototype-20261103/configure-virtual-service-account.c` - EXCLUDE FROM AGENT - privileged service/fixture lifecycle helper; retain only in a separately controlled offline archive if needed.
- `.native-probe/broker-prototype-20261103/named-pipe-broker.c` - QUARANTINE - broker research source/client/build notes; not runtime; manual security review required; no service install.
- `.native-probe/broker-prototype-20261103/named-pipe-broker.pre-ack-20261104.c` - QUARANTINE - broker research source/client/build notes; not runtime; manual security review required; no service install.
- `.native-probe/broker-prototype-20261103/named-pipe-node-net-epipe-characterization.mjs` - QUARANTINE - broker research source/client/build notes; not runtime; manual security review required; no service install.
- `.native-probe/broker-prototype-20261103/no-read-client.c` - QUARANTINE - broker research source/client/build notes; not runtime; manual security review required; no service install.
- `.native-probe/broker-prototype-20261103/node-pipe-native-adapter-client.mjs` - QUARANTINE - broker research source/client/build notes; not runtime; manual security review required; no service install.
- `.native-probe/broker-prototype-20261103/node-pipe-native-adapter.c` - QUARANTINE - broker research source/client/build notes; not runtime; manual security review required; no service install.
- `.native-probe/broker-prototype-20261103/node-pipe-transport-client.mjs` - QUARANTINE - broker research source/client/build notes; not runtime; manual security review required; no service install.
- `.native-probe/broker-prototype-20261103/node-pipe-transport-probe.c` - QUARANTINE - broker research source/client/build notes; not runtime; manual security review required; no service install.
- `.native-probe/broker-prototype-20261103/node-raya-protocol-adapter-client.mjs` - QUARANTINE - broker research source/client/build notes; not runtime; manual security review required; no service install.
- `.native-probe/broker-prototype-20261103/node-raya-protocol-adapter.c` - QUARANTINE - broker research source/client/build notes; not runtime; manual security review required; no service install.
- `.native-probe/broker-prototype-20261103/node-raya-protocol-adapter.pre-ack-20261104.c` - QUARANTINE - broker research source/client/build notes; not runtime; manual security review required; no service install.
- `.native-probe/broker-prototype-20261103/partial-request-deadline-client.c` - QUARANTINE - broker research source/client/build notes; not runtime; manual security review required; no service install.
- `.native-probe/broker-prototype-20261103/pipe-identity-negative.c` - QUARANTINE - broker research source/client/build notes; not runtime; manual security review required; no service install.
- `.native-probe/broker-prototype-20261103/pipe-oversize-client.c` - QUARANTINE - broker research source/client/build notes; not runtime; manual security review required; no service install.
- `.native-probe/broker-prototype-20261103/pipe-security-negative.c` - QUARANTINE - broker research source/client/build notes; not runtime; manual security review required; no service install.
- `.native-probe/broker-prototype-20261103/run-abrupt-disconnect-cleanup.ps1` - EXCLUDE FROM AGENT - privileged service/fixture lifecycle helper; retain only in a separately controlled offline archive if needed.
- `.native-probe/broker-prototype-20261103/run-abrupt-disconnect-setup.ps1` - EXCLUDE FROM AGENT - privileged service/fixture lifecycle helper; retain only in a separately controlled offline archive if needed.
- `.native-probe/build-hardlink-oplock-filter-probe-20261103.cmd` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/build-hardlink-oplock-rwh-probe-20261103.cmd` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/build-raya-phase1-hardlink-guard-rootpin.cmd` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/build-raya-phase1-hardlink-guarded-rootapi.cmd` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/build-raya-phase1-publish-hardlink.cmd` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/build-raya-phase1-rename-directory.cmd` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/build-raya-phase1-rename-hardlink.cmd` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/build-raya-phase1-rootapi-acl-diag.cmd` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/generate-metadata-acl-handle-grant-diagnostic-20261102.cjs` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/handle-relative-hardlink-guard-cross-process-test.cjs` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/handle-relative-hardlink-guard-rootpin-cross-process-test.cjs` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/handle-relative-hardlink-guarded-rootapi-test.cjs` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/handle-relative-hardlink-guarded-rootapi.c` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/handle-relative-metadata-acl-medium-everyone-test.cjs` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/handle-relative-metadata-acl-medium-test.cjs` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/handle-relative-probe-phase1-rootapi-acl-diag.c` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/handle-relative-publish-hardlink-probe.c` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/handle-relative-publish-hardlink-test.cjs` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/handle-relative-rename-acl-test.cjs` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/handle-relative-rename-cancellation-test.cjs` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/handle-relative-rename-directory-probe.c` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/handle-relative-rename-directory-test.cjs` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/handle-relative-rename-directory-worker-race-test.cjs` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/handle-relative-rename-directory-worker-stress-test.cjs` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/handle-relative-rename-directory-worker.cjs` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/handle-relative-rename-hardlink-probe.c` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/handle-relative-rename-hardlink-test.cjs` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/handle-relative-rootapi-hardlink-policy-test.cjs` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/hardlink-acl-repro-v1.c` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/hardlink-acl-repro-v1.cmd` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/hardlink-operation-family-audit-20261102.md` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/hardlink-oplock-filter-probe-20261103.c` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/hardlink-oplock-investigation-20261103.md` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/hardlink-oplock-rwh-probe-20261103.c` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/hardlink-share-repro-v2.c` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/hardlink-share-repro-v2.cmd` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/metadata-acl-absolute-vs-relative-20261102.md` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/metadata-acl-absolute-vs-relative-20261102.ps1` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/metadata-acl-accesscheck-20261102.cjs` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/metadata-acl-accesscheck-20261102.ps1` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/metadata-acl-allow-rights-isolation-20261102.ps1` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/metadata-acl-create-time-dacl-20261102.ps1` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/metadata-acl-diagnostic-20261102.cjs` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/metadata-acl-handle-security-20261102.ps1` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/metadata-acl-internal-handle-grant-20261102.cjs` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/metadata-acl-ntcreatefile-sync-mode-20261102.ps1` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/metadata-acl-ntcreatefile-vs-win32-allow-isolation-20261102.md` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/metadata-acl-ntfs-readattributes-policy-20261102.ps1` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/metadata-acl-ntfs-rights-isolation-20261102.ps1` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/metadata-acl-ntfs-sync-isolation-20261102.ps1` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/metadata-acl-query-class-matrix-20261102.ps1` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/metadata-acl-ra-rd-matrix-20261102.cjs` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/metadata-acl-ra-rd-matrix-results-20261102.md` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/metadata-acl-real-rootapi-acceptance-20261102.cjs` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/metadata-acl-sync-only-query-20261102.ps1` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/metadata-acl-token-privileges-20261102.ps1` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/metadata-acl-win32-compare-20261102.cjs` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/metadata-acl-win32-compare-20261102.ps1` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/metadata-acl-zero-access-query-20261102.ps1` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/open-relative-root-pin-characterization.cjs` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `.native-probe/open-relative-root-pin-regression-20261102.cjs` - QUARANTINE - native security experiment/source/test/build script; not production runtime; manual prerequisites and path review required.
- `STABILITY-BASELINE-20261105.md` - IMPORT CANDIDATE - project design/status documentation; sanitize local paths and verify claims before publishing.
- `docs/ARCHITECTURE-DECISION-RAYA-MCP-AND-AGENT-20261105.md` - IMPORT CANDIDATE - project design/status documentation; sanitize local paths and verify claims before publishing.
- `docs/DEDICATED-FILESYSTEM-BROKER-DESIGN-20261103.md` - IMPORT CANDIDATE - project design/status documentation; sanitize local paths and verify claims before publishing.
- `docs/HANDOFF-CONTINUATION-20261105.md` - IMPORT CANDIDATE - project design/status documentation; sanitize local paths and verify claims before publishing.
- `docs/PENDING-WRITE-DISCONNECT-TEST-DESIGN-20261105.md` - IMPORT CANDIDATE - project design/status documentation; sanitize local paths and verify claims before publishing.
- `docs/RAYA-AGENT-IMPORT-MANIFEST-20261105.md` - IMPORT CANDIDATE - project design/status documentation; sanitize local paths and verify claims before publishing.
- `docs/RAYA-EXECUTION-PLAN-20261105.md` - IMPORT CANDIDATE - project design/status documentation; sanitize local paths and verify claims before publishing.
- `raya-local.yaml.backup-20261105-102331` - EXCLUDE - local tunnel configuration backup; never import.
- `raya-local.yaml.backup-before-initialized-20261105-102433` - EXCLUDE - local tunnel configuration backup; never import.

### Additional source-level exclusions and cautions

- `start-mcp-stdio.cmd` is a modified tracked file and is explicitly excluded from verbatim import: it contains this host user profile, personal workspace roots, and enables arbitrary PowerShell.
- The untracked broker setup/cleanup helpers are not part of the Agent build/install/test path. The service prototype source may be preserved only in a quarantined research area with its NO-GO status.
- Native build scripts are not automatically trusted: inspect compiler invocation, output destination, absolute paths, service/ACL side effects, and generated binary dependencies before running.
- `metadata-acl-token-privileges-20261102.ps1` is a diagnostic source file, not a credential/config backup; it remains quarantined research, despite its filename containing the word `token`.


## 11. Additional code-review findings - import blockers

The following issues were found in the working-tree diffs and must be resolved or deliberately excluded before selecting an import patch:

- `advanced-tools.mjs` changes the session default/config upper bound from 32 to 64, while the observed live runtime and smoke test still expect 32. `README.md` also now claims 64. Treat 32 as the verified baseline; do not import the 64 change without an explicit capacity decision and synchronized config/tests/docs.
- `advanced-tools.mjs` adds `force_terminate(..., force)` but calls `terminateTree(s, true)` unconditionally. The `force` argument is therefore ignored and the implementation always requests `taskkill /T /F`. Correct semantics and tests are required before import.
- `safeChildEnv()` copies a baseline environment allowlist, but `start_process` then accepts arbitrary syntactically valid override names (except a small secret-like regex). Callers can override sensitive execution variables such as `PATH`, `NODE_OPTIONS`, `ComSpec`, `SystemRoot`, or `TEMP`; this is not a strict allowlist. Define an explicit override policy and test protected variables.
- The new session timeout/termination path launches `taskkill.exe` asynchronously and reports `terminationRequested` before confirming process-tree exit. Add bounded completion/verification and failure reporting; do not equate request submission with termination.
- `server.mjs` `file_write_chunk` uses a check-then-rename sequence (`fs.access` followed by `fs.rename`) for the no-overwrite guarantee. This is a race-prone boundary and must be replaced with a genuinely exclusive publication primitive or explicitly documented weaker contract. Interrupted uploads leave temporary files with no TTL/quota/cleanup mechanism; add bounded upload lifecycle and disconnect cleanup. Parent/path race concerns remain under Phase 1.
- `file_read_chunk` performs path-based `stat` then `open` and reads by path; it does not close the Phase 1 confinement gap. Do not describe chunk I/O as race-safe.
- `package.json` adds syntax-check entries for untracked native research files. If those files are excluded from the Agent staging export, `npm run lint` will fail. The package diff also adds two native test scripts that directly execute prebuilt `.exe` files, which are excluded artifacts. Split research checks from the default portable check and make native tests reproducible build-then-test/opt-in.
- `start-mcp-stdio.cmd` must not be imported verbatim: it contains absolute personal workspace paths and sets `MCP_ENABLE_POWERSHELL=true`. This conflicts with README history that says the launcher was corrected to leave PowerShell disabled by default.
- `README.md` now claims a 64-session limit while the observed runtime config/test is 32, and its appended native-security history still says the strict hard-link policy is pending a user decision even though the project record says the user approved the single-link policy on 2026-11-02. Sanitize machine paths and reconcile claims before publishing.
- `docs/DESKTOP_COMMANDER_PARITY.md` contains visible mojibake in headings (`-?"`) even though it has no Unicode replacement character U+FFFD. Repair encoding/typography in the staged copy, not in the source tree as part of this bootstrap.
- `HANDOFF.md` remains over the MCP file-read limit and has not been fully audited. Keep it out of any automatic import until a separate bounded export/review is available; use the supplemental handoff as the current transfer record.

### Corrected untracked inventory count

The exhaustive current `git ls-files --others --exclude-standard` result contains **117 paths**, including this untracked manifest itself: 47 broker-prototype paths, 108 paths under `.native-probe/` overall, 16 `.out`/`.err` output captures, 2 local tunnel YAML backups, and 7 project/stability documents outside `.native-probe/`. The earlier 116 count was an off-by-one inventory error; use 117 as the corrected count. The 16 output captures and 2 backups are excluded. The `metadata-acl-token-privileges-20261102.ps1` file is diagnostic source, not a secret/config file, and stays in quarantined research.