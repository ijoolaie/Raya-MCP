# Raya MCP stability baseline — 2026-11-05

## Runtime identity
- Bridge package/server: local-mcp-bridge 0.1.0
- MCP SDK: @modelcontextprotocol/sdk 1.32.2
- Node.js: v22.23.2
- Tunnel client: 0.0.17+a248b7f725fc989c1eb26680cf4351ee33421f61
- MCP protocol version observed: 2025-11-25

## Bridge configuration and limits
- Live and on-disk maxSessions reconciled to 32; maxSearches=8; searchMaxResults=500; searchMaxDepth=8.
- Runtime limits: text read/write 262144 bytes; aggregate reads 1048576 bytes; outputChars=131072.
- PowerShell default timeout 30000ms, maximum 120000ms; arbitrary PowerShell enabled explicitly. Shell is not sandboxed.
- Managed process sessions=0; active searches=0 after verification.

## Root-cause evidence
- Five-minute redacted tunnel-log sample: 972 entries; no process crash/restart event found.
- Five preserved MCP errors were tools/call HTTP 409, JSON-RPC -32002.
- Event order: initialize response delivered HTTP 200; a tools/call arrived before notifications/initialized and was rejected HTTP 409/-32002; notifications/initialized was forwarded only afterward.
- The failing tools/call had no matching dispatcher-forwarded-to-MCP-server event. Strong evidence of an initialization-order race at the tunnel/control-plane boundary, not a tool-handler failure or bridge crash. Exact owner (control-plane scheduling vs tunnel dispatcher) is not proven by redacted logs.

## Tunnel profile mitigation and activation
- Profile: C:\Users\Ali\AppData\Roaming\tunnel-client\raya-local.yaml.
- Configured `control_plane.max_inflight_requests=1`, `mcp.max_concurrent_requests=1`, and `mcp.stdio_send_initialized_notification=true`.
- The user restarted only `raya-local` from its original PowerShell window. New tunnel process observed: PID 20780, created 2026-11-05 10:35:06; MCP child node PID 17224. Separate IranPriceScout tunnel/port 8765 was not touched.
- Current profile file contains all three intended values. Tunnel process is running from the `raya-local` profile.
- Backup files: C:\Users\Ali\local-mcp-bridge\raya-local.yaml.backup-20261105-102331 and C:\Users\Ali\local-mcp-bridge\raya-local.yaml.backup-before-initialized-20261105-102433.

## Post-restart verification
- `/healthz`: HTTP 200 `live`; `/readyz`: HTTP 200 `ready`; `/health/mcp`: HTTP 200, state `initialized`, status `ok`, child_state `running`, evidence `same_child`, initialize ok and identity_complete, protocol 2025-11-25, server local-mcp-bridge 0.1.0.
- Health snapshot child_generation `48c00941fc4056243c0b4bc2a85eb895`; initialize_epoch advanced to 79 during the verification window, with same child generation.
- Health response currently reports `tools_list.ok=false`, complete=false, retained_count=0. This health sub-check has not been positively confirmed and should not be represented as a successful tools/list verification.
- 25 sequential low-risk `get_config` calls after restart: 25/25 successful, 0 initialization errors. Measured wall-clock latency range approximately 1.9–9.8 seconds; no retry/recovery was needed.
- Usage telemetry after the probe: no active managed sessions and no active searches; one historical error in the bounded in-memory counter (the earlier initialization issue), and the 25 post-restart calls were successful.

## Status
- The tunnel mitigation is active and the post-restart sequential stability test passed 25/25.
- Health endpoints and MCP initialization are healthy. However, the tunnel's health `tools_list` sub-check still reports `ok=false`; full acceptance of the tools/list health check remains open pending diagnosis/confirmation. Do not claim complete acceptance until that indicator is understood or verified through an independent supported check.
