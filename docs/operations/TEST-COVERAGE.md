# Raya Local MCP — Test and Evidence Map

**Snapshot:** 2026-11-25\
**Method:** Static inspection of package.json and the four standard test programs.\
**Tests executed during this documentation pass:** None.\
**Important:** This matrix describes what the source code intends to assert; it does not report a current passing test run.

## 1. Test entry points

| Command | What it does | Side effects / interpretation |
|---|---|---|
| npm test | Runs smoke-test.mjs, format-smoke-test.mjs, http-smoke-test.mjs, url-security-test.mjs in order. | Not read-only. Starts child processes, creates temporary directories/files and a local HTTP listener. The format test calls set_config_value and persists search.maxDepth=8 to raya-config.json; it does not restore the prior value. Do not run against a valuable live config without an isolated copy or explicit approval. |
| npm run check | Executes a long node --check chain over production modules, standard test files and many .native-probe files. | Syntax checks only; does not execute tests. |
| npm run lint | Alias for npm run check. | Not ESLint and not a separate lint engine. |
| npm run start | Starts node server.mjs. | Starts an MCP server; not a test. |
| npm run start:http | Starts node server.mjs --http. | Starts an HTTP MCP server; requires a bearer token of at least 32 characters. |
| run_check(npm_test) | Calls npm test through the fixed preset. | Inherits the same side effects above. |
| run_check(npm_lint) | Calls npm run lint through the fixed preset. | Syntax-check chain only. |

## 2. Standard test programs

### A. smoke-test.mjs — base + advanced MCP tool behavior

The program starts a fresh stdio MCP server child, connects an SDK client, enumerates tools, creates a temporary fixture directory and an outside-root fixture, then removes those fixtures in finally.

The source contains **52 named check assertions**:

1. Tool inventory, configured roots and expected config limits.
2. Outside-root path rejection.
3. Recursive directory creation.
4. Junction escape attempts for read, edit, move, metadata, directory listing, multi-read, write and directory creation; checks that the outside target remains unchanged. Junction assertions are conditional on the host allowing junction creation.
5. Default overwrite protection.
6. Chunked upload completion, immutable total size, resume at expected offset, same-upload concurrency rejection, successful continuation after a rejected concurrent request, existing-target protection and failed-temp cleanup.
7. Bounded chunk read and UTF-8 byte limit.
8. Concurrent create exclusivity.
9. Edit preview and exact replacement.
10. Multi-file per-file error reporting, metadata, directory listing and move/rename.
11. Content search, pagination, search listing, expiry metadata, explicit cleanup, bounded capacity and oldest-session eviction.
12. Process start/output, rolling-output absolute offsets, session listing, interactive stdin, managed termination, OS process listing and PID identity mismatch rejection.
13. Local-only feedback recording.
14. PowerShell gate (either successful execution when enabled or expected disabled rejection).
15. Usage statistics, recent-call metadata, workflow prompts and file-tail reading.

**Interpretation caveats**
- This test validates representative behaviors, not every schema edge or every possible path race.
- Junction checks may be skipped when the host cannot create directory junctions.
- Process tests intentionally spawn and terminate child processes.
- The finally block removes fixture paths, but abrupt host shutdown can interrupt cleanup.
- The initial inventory array asserts 30 names (8 base + 22 advanced). set_config_value and give_feedback_to_desktop_commander are not in that array, but both are invoked later by the standard tests. The inventory check is not proof of live Connector discovery.

### B. format-smoke-test.mjs — Office/PDF adapters and selected URL/config gates

The source contains **19 check call sites**. The tool-inventory call iterates over 16 required tool names, so a normal full run reaches 34 assertion invocations (16 inventory checks + 18 other checks):
- Declaration check iterates over these 16 names: set_config_value, read_url, excel_read_workbook, excel_write_cells, excel_read_range, excel_search_workbook, docx_search, pdf_merge, docx_replace_text, excel_set_formula, pdf_add_text, docx_read, docx_create, pdf_extract_text, pdf_info and pdf_create.
- XLSX write/read, overwrite guard, range read, cell search and formula set.
- DOCX create, raw-text extraction, search, exact replacement and replacement readback.
- PDF create, info, text extraction, text overlay and merge.
- HTTP URL rejection guard for read_url.
- Config persistence.

**Important persistent side effect:** the last check calls set_config_value({key:"search.maxDepth", value:8}). That writes raya-config.json through the real MCP server. The test does not restore the original value. Its format fixtures are placed in a temporary directory and removed in finally.

### C. http-smoke-test.mjs — HTTP transport authentication

- Spawns server.mjs --http as a child on 127.0.0.1:18766.
- Generates an ephemeral 48-byte random bearer token.
- Sends an unauthenticated initialize request and requires HTTP 401.
- Sends an authenticated initialize request and requires a 2xx response.
- Kills the child in finally.

This is a local HTTP transport smoke test, not a full remote Connector, OAuth or tunnel test. It uses a fixed local port, so a port conflict can cause a false failure or prevent startup.

### D. url-security-test.mjs — URL safety unit tests

- Registers read_url against a fake MCP server.
- Replaces https.request with a mock and restores it after each case.
- Uses an injected DNS lookup function for the timeout case.
- The source's reported check summary is **36** cases, covering:
  - Public IPv6 and selected public IPv4 boundary addresses accepted.
  - Private, shared, loopback, unspecified, link-local, multicast, reserved, documentation, benchmarking, protocol-assignment and IPv4-mapped IPv6 ranges rejected.
  - HTTP, credential-bearing URLs and nonstandard HTTPS ports rejected.
  - Private redirects blocked before a follow-up request.
  - Public redirects revalidated and followed.
  - Redirect hop limit enforced.
  - DNS timeout enforced.

These are mocked/unit-level tests. They do not establish real-world DNS behavior, certificate validation, firewall behavior, external site compatibility or end-to-end tunnel security.

## 3. Test-to-tool family matrix

| Tool family / component | Primary test evidence | What remains unproven |
|---|---|---|
| Base file read/write/chunk | smoke-test.mjs | Exhaustive malformed-input, ACL, hardlink and race coverage |
| Advanced filesystem | smoke-test.mjs | All path aliases, junction/reparse variants, concurrent mutation and race-free containment |
| Search lifecycle | smoke-test.mjs | Every unreadable/oversized/encoding case and worst-case scan performance |
| Process/session lifecycle | smoke-test.mjs | Every process tree, timeout failure mode, PID reuse race and hostile child behavior |
| Excel/DOCX/PDF | format-smoke-test.mjs | Full format fidelity, malformed/corrupt files, huge documents and every overwrite/ACL case |
| URL fetch | format-smoke-test.mjs, url-security-test.mjs | Real DNS rebinding infrastructure, public Internet interoperability and end-to-end tunnel behavior |
| HTTP transport | http-smoke-test.mjs | OAuth, remote Connector lifecycle, proxy/tunnel behavior, long-running sessions and disconnect cancellation |
| PowerShell | smoke-test.mjs gate only | Security isolation; it is explicitly not sandboxed |
| Native filesystem prototypes | individually named test:native-* scripts | Production integration and production security acceptance |

## 4. Native/prototype script inventory

These are opt-in scripts in package.json; they are not part of standard npm test. They probe research/prototype implementations and must not be treated as proof that production path handlers have migrated.

### Create / directory creation / path handling
- test:native-create-race
- test:native-dircreate-race
- test:native-dircreate-pinned
- test:native-dircreate-preexisting-delete
- test:native-dircreate-pinned-cleanup
- test:native-dircreate-pinned-concurrent
- test:native-dircreate-pinned-acl
- test:native-dircreate-nul
- test:native-dircreate-path-policy
- test:native-dircreate-partial-acl
- test:native-dircreate-finalizer
- test:native-dircreate-napi-failures
- test:native-dircreate-allocator-failures
- test:native-dircreate-win-namespace
- test:native-dirpath
- test:native-dirwalk-race

### Read / root identity / request lifecycle
- test:native-read-family-race
- test:native-rootapi
- test:native-readonly-adapter
- test:native-reparse-parity
- test:native-error-map
- test:native-rootid-contract
- test:native-request-scope
- test:native-root-identity

### MCP / HTTP cancellation and shutdown
- test:native-mcp-cancellation
- test:native-http-disconnect
- test:native-http-disconnect-bridge
- test:native-http-disconnect-hardening
- test:native-http-disconnect-shutdown

### Rename / publish / hardlink characterization
- test:native-rename-relative
- test:native-rename-workers
- test:native-rename-worker-stress
- test:native-publish-exclusive
- test:native-publish-workers
- test:native-rename-acl
- test:native-rename-directories
- test:native-rename-directory-workers
- test:native-rename-cancellation
- test:native-rename-hardlink
- test:native-publish-hardlink-characterization
- test:native-rootapi-hardlink-characterization
- test:native-hardlink-share-characterization
- test:native-hardlink-acl-characterization

Some scripts require --expose-gc; two hardlink characterization entries execute prebuilt .exe artifacts. Treat them as separate, potentially environment-sensitive research probes.

## 5. npm run check syntax-check scope

The script runs node --check on:
- Production source: server.mjs, advanced-tools.mjs, format-tools.mjs, url-tools.mjs.
- Standard test programs: all four files listed above.
- A long list of .native-probe JavaScript/CJS/MJS source files.

A successful syntax check would establish parseability only for the files included in that command. It would not establish runtime correctness, test pass status, native binary correctness, security acceptance or Connector exposure.

## 6. Evidence policy for future answers

Use these labels consistently:
- **Source-confirmed:** behavior is visible in the current implementation.
- **Test-defined:** a test contains an assertion for the behavior, but it has not necessarily been run in the current snapshot.
- **Test-passed:** only use after a current run with captured successful result.
- **Observed-runtime:** current process/config/log/health evidence with timestamp.
- **Prototype-only:** demonstrated in a research/native probe, not accepted in production.
- **Unknown / not tested:** insufficient current evidence.

Never convert “test exists” into “test passed”. Never convert “native probe passed” into “production is race-free”. Never invoke npm test as an automatic diagnostic because it mutates raya-config.json.
