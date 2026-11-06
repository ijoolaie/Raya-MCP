# Shared Raya MCP service: client identity, authorization and session isolation

- Status: design proposal for implementation
- Target: independent Raya MCP service shared by ChatGPT and Raya Agent
- Related: Raya Agent ADR 0002

## Objective

Operate one Raya MCP service on the Windows host and let multiple independent clients use it safely. The service is the sole owner of local tool execution. Clients do not share credentials, MCP sessions, approvals, request IDs, cancellation state or audit identity.

## Existing HTTP mode is not ready for shared production use

The current `server.mjs --http` path:
- reads one `MCP_BEARER_TOKEN` for every client;
- creates one `McpServer` and one `StreamableHTTPServerTransport` with `sessionIdGenerator: undefined`;
- applies a static Origin allowlist but has no client-specific authorization profile;
- uses process-wide `ROOTS` and process environment for capability configuration;
- binds to `127.0.0.1` by default.

This is a useful authenticated local prototype, not a multi-principal production service. Do not expose it to ChatGPT or Agent yet. An Origin allowlist is not authentication, and authentication is not authorization.

## Target trust model

There are three separate boundaries:

1. **Ingress:** TLS termination, host validation, request size/rate limits, and protection against direct public access to the local listener.
2. **Authentication:** establish a stable client principal (for example `chatgpt` or `raya-agent`) from a validated credential.
3. **Authorization:** determine which workspaces, tools, operations, quotas and risk classes that principal may use.

The service must fail closed if any layer is absent or ambiguous.

## Authentication model

Preferred model for remote Streamable HTTP clients:
- Implement the MCP HTTP authorization specification using OAuth 2.1-compatible authorization/resource-server behavior where the client platform supports it.
- Validate issuer, audience/resource, expiry, not-before, scopes, signature and key rotation.
- Support revocation and short-lived access tokens. Never trust a client-supplied principal header unless a trusted gateway strips external copies and injects a verified identity.
- Require HTTPS for traffic leaving the Windows host. Do not put credentials in query parameters, URL paths, MCP tool arguments or logs.

For a constrained same-host Agent connection before OAuth is available:
- Use a distinct high-entropy credential for the Agent principal, stored with Windows Credential Manager or DPAPI-backed storage.
- Do not use the same credential for ChatGPT, Agent, CLI administration or a human operator.
- Support rotation and revocation without changing application source.
- Keep the credential out of process command lines, source-controlled config, crash dumps where practical, and audit records.

ChatGPT must be configured through its supported secure connector authentication mechanism. Never copy the Agent's local secret into ChatGPT settings or share a static bearer token between the two clients.

## Principal and authorization model

Authentication produces a server-owned immutable principal, for example:

```text
principal_id: raya-agent
credential_id: agent-key-2026-01
audience: raya-mcp
scopes: mcp:connect, tools:read, workspace:read
policy_profile: agent-default
```

This is a schema illustration, not a credential or a live configuration.

Authorization is evaluated server-side for every tool call. Suggested dimensions:
- allowed tool names;
- allowed workspace root IDs (not arbitrary client-supplied paths);
- read/write/execute capability flags;
- maximum request size, result size, execution time and concurrency;
- per-principal rate limits and quotas;
- whether a specific privileged capability is disabled or requires an additional administrative grant.

Start with least privilege. A new principal gets no write, process-control or PowerShell permissions by default. PowerShell remains disabled unless separately enabled by a server administrator. Raya Agent's own policy and approval prompt are a second gate; they cannot elevate the server-side grant.

The server must not expose all host roots to all principals. Refactor global `ROOTS` and process-wide capability configuration into an immutable per-principal request context passed to tool handlers. Tool handlers must resolve paths against the principal's server-owned root set, never against a caller-supplied root list.

## MCP session isolation

- Use a separate Streamable HTTP transport and MCP server/session state per initialized client session.
- Authenticate before creating a session.
- Bind each session to one immutable principal and credential ID.
- Do not allow a session to change principal after initialization.
- Enforce maximum sessions per principal and globally, idle TTL, absolute TTL, request concurrency limits and bounded cleanup.
- Validate the MCP session identifier on every subsequent request; reject unknown, expired, cross-principal or replayed session use.
- A client disconnect or session expiry must cancel or safely detach in-flight work according to explicit tool semantics; never claim an operation did not execute solely because the response was lost.
- A server restart invalidates sessions; clients must reinitialize.
- Never share one mutable `McpServer`/transport object across independent stateful sessions unless the SDK contract explicitly guarantees isolation and tests prove it.

If stateless HTTP mode is retained for a particular client, make it an explicit separate deployment profile with no server-side session state. Do not describe a single shared stateless transport as per-client session isolation.

## Audit and observability

Every request/tool event should include server-derived principal ID, credential ID (non-secret identifier), session ID (or a non-reversible correlation), tool name, decision, policy version, duration, outcome and bounded error category. Never record bearer tokens, authorization headers, raw tool arguments, file contents or secrets.

Keep service audit records authoritative for server-side allow/deny and execution. Agent may keep a separate client-side audit record for plan/approval/intent. Correlate using a random request ID without sharing approval grants.

## Ingress and network

- Keep the MCP listener loopback-only by default.
- ChatGPT cloud connectivity requires a separately secured HTTPS ingress/tunnel. The tunnel must not turn a local bearer-token endpoint into a publicly reusable shared-secret service.
- Enforce host allowlisting and trusted proxy configuration; reject spoofed forwarding headers from untrusted peers.
- Restrict methods and routes to the MCP endpoint and any explicitly documented health endpoint.
- Apply request body limits, header limits, connection limits, rate limits, read/idle timeouts and graceful shutdown.
- CORS/Origin checks are defense-in-depth for browser contexts, not a substitute for authentication.
- Health checks must reveal only minimal readiness information and must not enumerate roots or execute tools.

## Service lifecycle

Raya MCP owns service startup, shutdown, restart, health, session expiry and child processes. Raya Agent owns only its own MCP client connection and does not stop the shared service during Agent shutdown.

A service shutdown sequence:
1. stop accepting new sessions/requests;
2. mark readiness false;
3. reject new tool calls;
4. drain or cancel in-flight work according to each tool's cancellation contract and a bounded deadline;
5. close session transports and MCP server instances;
6. close HTTP listener and release service resources;
7. report stopped only after all required cleanup is confirmed.

A service restart invalidates sessions and must be visible to clients as a transport/session failure. Clients reconnect with bounded backoff and re-run initialize/tools/list. They must not automatically retry non-idempotent tool calls after ambiguous completion.

## Compatibility and independent upgrades

- Publish a supported MCP protocol version range and a versioned tool catalog contract.
- Agent checks server identity/version, negotiated protocol version, tool names and schemas at connection time.
- New unknown tools default-deny in Agent. Removed tools or incompatible schema changes disable the affected capability and produce a diagnostic.
- Compatible server upgrades must not require rebuilding Agent. Breaking changes require a staged rollout and compatibility update.
- Contract tests must run against a separately launched Raya MCP service and a clean dependency install.

## Implementation sequence

1. Add a principal/authentication abstraction and a policy resolver without changing current stdio behavior.
2. Refactor tool registration so each server/session receives an immutable principal context and scoped workspace roots.
3. Implement per-client credentials and authentication middleware; add rotation/revocation and negative tests.
4. Implement independent session/transport creation with bounded session registry and cleanup.
5. Add request/body/rate/concurrency limits and hardened ingress/proxy handling.
6. Add tests for unauthenticated, expired, revoked, wrong-audience, wrong-scope, cross-principal session reuse, session expiry, server restart, disconnect and graceful shutdown.
7. Add separate ChatGPT and Agent credentials and verify both connect to the same service without sharing session or policy state.
8. Only after security review, document the endpoint for production clients.

## Acceptance gates

Do not enable shared production access until all of the following are true:
- credentials are unique, scoped, revocable and never logged;
- authentication is enforced before session creation;
- every session is bound to one principal;
- per-principal tool/root/capability authorization is enforced server-side;
- no mutable session state is shared across clients;
- resource limits and bounded shutdown are tested;
- the HTTPS ingress and trusted proxy boundary are reviewed;
- filesystem confinement Phase 1 is separately accepted;
- independent security review and integration tests pass.
