---
name: desktop-commander-parity
description: Audit and implement feature parity with the official Desktop Commander MCP capability set.
---

Sources: https://github.com/wonderwhy-er/DesktopCommanderMCP ; https://desktopcommander.app/mcp/ ; https://github.com/wonderwhy-er/DesktopCommanderMCP/blob/main/FAQ.md . Inspect current upstream schemas and record date/version before implementation.

Inventory each capability, user outcome, tool schema, status, risks, dependencies, automated tests and manual acceptance. Feature families:
1. get_config/set_config_value: configuration read, validated updates, redaction, persistence/rollback.
2. start_process/read_process_output/interact_with_process: shell execution, background work, streaming/paginated output, stdin, timeout and completion states.
3. list_sessions/force_terminate/list_processes/kill_process: managed sessions, process inspection and guarded termination; protect against PID reuse.
4. read_file/read_multiple_files/write_file: bounded reads/writes, multi-file reads, optional URL/media handling with SSRF protections.
5. create_directory/list_directory/move_file/get_file_info: directory operations, depth-limited recursive listing, metadata, safe move/rename.
6. edit_block: exact-string edits; fail on zero/ambiguous matches unless expected count supplied; preview/diff and backup.
7. start_search/get_more_search_results/list_searches/stop_search: progressive filename/content search, session IDs, pagination, cancellation, expiry and concurrency limits.
8. write_pdf; format-aware Excel and DOCX read/write/edit/search with round-trip tests.
9. get_usage_stats/get_recent_tool_calls: bounded diagnostics/history with secret redaction and retention.
10. get_prompts and feedback features only where target client supports them; don't send telemetry/feedback without consent.
11. CSV/JSON/Excel analysis and in-memory Python/Node/R workflows where practical.
12. SSH/database/dev-server workflows via persistent terminal sessions.
13. Local stdio and authenticated remote transport diagnostics.
14. UI-specific preview/open-in-folder features: mark not applicable if ChatGPT cannot expose them; document alternatives.

Prioritize safety, session lifecycle, filesystem/search, then structured documents/diagnostics. Never copy telemetry blindly. Never claim 100% parity until every accepted feature is verified or explicitly excluded with approved rationale.

Final report separates verified, implemented-unverified, not implemented, exclusions, risks, tests and next slice.
