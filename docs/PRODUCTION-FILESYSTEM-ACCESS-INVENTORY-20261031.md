# Production filesystem access inventory — 2026-10-31

## Scope and method

Reviewed the current production registrations and handlers in:
- `server.mjs`
- `advanced-tools.mjs`
- `format-tools.mjs`
- `url-tools.mjs`
- `package.json` scripts

This is a source inventory, not a runtime race test. No production source, configuration, permissions, service, tunnel, or credentials were changed during this review.

## Filesystem operation inventory

### Core server registrations (`server.mjs`)

| Tool / path | Operation | Current access pattern | Native primitive candidate | Priority / notes |
|---|---|---|---|---|
| `workspace_roots` | Return configured roots | No filesystem access | None | Not a file operation |
| `workspace_list` (no path) | Enumerate root children | `fs.readdir(root, withFileTypes)` | Open root + paged enumeration | Medium; root identity and child metadata semantics need definition |
| `workspace_list(path)` | Enumerate directory children | `checkedPath` then `fs.readdir` | Open directory relative to verified root/parent + paged enumeration | High; path check/use race |
| `file_read` | Read one UTF-8 file | `checkedPath`, `fs.stat`, then `fs.readFile` | Handle-relative open + bounded read | High; metadata/read are separate path resolutions |
| `file_write` | Create or overwrite UTF-8 file | `checkedPath`, parent allowlist check, `fs.access`, `mkdir`, then `writeFile(wx|w)` | Handle-relative parent open + exclusive create; separate safe replace design | Critical; preflight existence check is not atomic with write; overwrite uses truncating path open |
| `run_check` | Run predefined command in cwd | `checkedPath(cwd)`, then spawn | No direct file primitive; cwd directory HANDLE cannot be passed portably to child spawn | Keep separate; command's own filesystem effects are outside bridge confinement |
| `powershell_execute` | Arbitrary PowerShell when explicitly enabled | `checkedPath(cwd)`, then spawn powershell | Not safely confined by current native file primitives | Explicitly privileged escape hatch; must remain disabled unless operator accepts host-level command authority |

### Advanced registrations (`advanced-tools.mjs`)

| Tool | Operation | Current access pattern | Native primitive candidate | Priority / notes |
|---|---|---|---|---|
| `create_directory` | Create directory | `securePath` + parent check + `fs.mkdir` | Handle-relative directory create | High; prototype exists, but recursive semantics and production adapter are separate gates |
| `get_file_info` | Read leaf metadata | `securePath` + `fs.lstat` | `inspectEntryMetadata(parentHandle, leaf)` | High; metadata prototype exists |
| `read_multiple_files` | Read up to 20 files | Per-item `securePath`, `fs.stat`, `fs.readFile` | Sequential handle-relative leaf open/read; preserve per-file error isolation | High; prototype wrapper exists, production semantics still need byte/aggregate and path mapping review |
| `list_directory` | Recursive directory listing | `securePath` + path-based `walk/fs.readdir` | Handle-relative open + paged enumerate + metadata | Critical; recursive test wrapper exists, production adapter absent |
| `move_file` | Rename/move | `securePath` source/destination, `noSymlinkAncestors`, destination `lstat`, then `fs.rename` | Handle-relative rename with same-volume and no-replace semantics | Critical; no tested native rename primitive identified |
| `edit_block` | Read-modify-replace text file | `securePath`, `noSymlinkAncestors`, `stat/readFile`, temp path `writeFile`, path `rename` | Handle-relative read + safe temp create + atomic replace tied to verified parent | Critical; no native replace/rename adapter |
| `start_search` | Filename/content scan | `securePath` roots + path-based `walk`, then `stat/readFile` per file | Reuse handle-relative walk/list/read adapter; keep search-session layer separate | Critical; native traversal semantics tested only in isolated wrapper |
| `read_file_tail` | Bounded tail read | `securePath`, `stat`, path `open/read/close` | `readTailChildFromParent` | High; native tail prototype exists, but production binding absent |
| Process/session/config/search-session tools | Process management, in-memory sessions, config | Some config file I/O in `loadSettings` and `set_config_value`; process cwd validation for spawn | Config needs separate trusted-config policy; process tools are not ordinary workspace file operations | Keep out of initial filesystem adapter scope |

### Format registrations (`format-tools.mjs`)

All document handlers use `safeFile` (path resolution and realpath containment), but then use ordinary path-based Node I/O. The optional `noLinks` check is only applied by selected write/modify handlers.

| Tools | Operation | Current access pattern | Native primitive candidate | Priority / notes |
|---|---|---|---|---|
| `excel_read_workbook`, `excel_read_range`, `excel_search_workbook` | Read XLSX | `safeFile`, `stat`, `readFile`, then library parsing | Handle-relative bounded read into buffer | Medium; parser behavior remains above filesystem adapter |
| `docx_read`, `docx_search` | Read DOCX | `safeFile`, `stat`, `readFile` | Handle-relative bounded read | Medium |
| `pdf_extract_text`, `pdf_info` | Read PDF | `safeFile`, `stat`, `readFile` | Handle-relative bounded read | Medium |
| `excel_write_cells` | Create/replace XLSX | `safeFile(missing)`, `noLinks(parent)`, optional target `lstat`, then `atomicWrite(temp + rename)` | Handle-relative temp create + verified atomic replace/no-replace | High |
| `docx_create`, `pdf_create` | Create/replace documents | Same parent/target checks and path-based `atomicWrite` | Handle-relative temp create + verified atomic replace/no-replace | High |
| `pdf_merge` | Read multiple PDFs and create/replace output | Path-based input reads; parent `noLinks`; target `lstat`; `atomicWrite` | Handle-relative input reads and safe output transaction | High |
| `docx_replace_text`, `excel_set_formula`, `pdf_add_text` | Modify existing document | `safeFile`, selected `noLinks(target)`, `stat/readFile`, then `atomicWrite` | Handle-relative read + same-parent safe replacement | High |

### URL registrations (`url-tools.mjs`)

`read_url` does not access workspace files. It performs HTTPS requests with DNS validation/pinning and redirect revalidation. Excluded from the filesystem-native adapter.

## Cross-cutting findings

1. `securePath`, `safeFile`, and `checkedPath` validate a path before later path-based I/O. The filesystem object can change between those steps; these helpers are not HANDLE-based confinement.
2. `noSymlinkAncestors` / `noLinks` are point-in-time path walks. They do not bind subsequent opens or renames to the inspected objects.
3. Read paths commonly perform separate `stat` and `readFile` operations. The object read need not be the object whose metadata was checked.
4. Several write paths use a temporary pathname followed by `rename`. This is useful for crash-safe replacement in ordinary conditions, but does not itself prove that the parent directory stayed the same object or that a leaf was not redirected during the transaction.
5. `move_file` checks destination absence before `rename`; this is not a proven atomic no-replace operation.
6. Native prototype coverage currently includes handle-relative create/read, directory create, tail/multi-read wrapper, directory enumeration, recursive test-only traversal, and metadata. It does not include a production adapter, handle-relative rename/replace, or the complete format-tool transaction model.
7. `run_check` and enabled `powershell_execute` can execute programs that independently read/write the host filesystem. The bridge's path checks on `cwd` do not confine those programs' filesystem effects.

## Recommended integration order (not executed)

1. **Define one production filesystem adapter contract** for verified root handles, child open, metadata, bounded reads, directory enumeration, and explicit handle ownership/error semantics.
2. Integrate read-only leaf operations first: `file_read`, `get_file_info`, `read_multiple_files`, `read_file_tail`.
3. Integrate list/search only after the production adapter reproduces the tested recursive wrapper's limits, cancellation, per-entry errors, pagination, and reparse behavior.
4. Design and independently test no-replace rename and atomic replace before migrating `move_file`, `edit_block`, or document writers.
5. Treat recursive `create_directory` as a separate transaction design; the current native directory-create prototype proves only the tested single-child operation.
6. Keep `run_check` and PowerShell execution explicitly outside filesystem confinement claims.
7. Add production-equivalent integration tests and Windows permission-denied/ACL tests before Phase 1 acceptance.

## Status

- Inventory completed from current source.
- No Production handlers changed.
- No native prototype connected to Production.
- No ACL or permission settings changed.
- Phase 1 remains **IN PROGRESS / NOT ACCEPTED**.
