# Protected filesystem broker — staged implementation specification (2026-11-06)

## Scope and decision

This is a test-only continuation of Phase 1. It does not authorize Production deployment, Production handler changes, real workspace ACL/owner migration, or changes to unrelated services/tunnels.

The disposable native broker remains a single-instance, read-only prototype. On 2026-11-06, a LocalSystem client successfully opened the pipe but was rejected because its TokenUser SID/session did not match the configured interactive identity. The temporary task and all disposable service, process, fixture, image, and output artifacts were removed and absence verified.

## Security model

1. Run the broker under a dedicated virtual service identity, never as the interactive user or LocalSystem.
2. Broker-owned test roots and descendants must have protected owner/DACLs. The interactive client must not have WRITE_DAC, WRITE_OWNER, FILE_WRITE_ATTRIBUTES, or direct namespace mutation rights that would let it bypass the broker.
3. The client supplies only a versioned root ID and validated relative components; never an absolute filesystem path. The broker maps root IDs to pinned directory HANDLEs.
4. Impersonate the pipe client only for TokenUser SID and TokenSessionId authentication, then always revert before filesystem operations.
5. Remote pipe clients are rejected; pipe DACL and token identity are independent authorization gates.
6. Every regular-file operation enforces the approved strict policy on the opened HANDLE: NumberOfLinks must be exactly 1. Query failure or any other value fails closed. Directory objects use separate type/reparse checks.
7. Do not rely on share modes or same-owner deny ACEs to prevent hard-link creation. Experiments showed they do not provide a durable same-identity boundary.
8. Native errors never trigger a path-based fallback.

## Operation contract and migration order

| Family | Broker contract | Required acceptance before marking supported |
|---|---|---|
| Root/session | Register fixed root IDs; pin root HANDLEs per authenticated session | Root replacement, reparse, ACL/owner, SID/session mismatch, cancellation, restart and HANDLE cleanup |
| Read/info/tail | Open relative to held parent; validate type, reparse and single-link count on same HANDLE; read bounded bytes | Single-link success, pre-existing and concurrent alias rejection, size boundaries, metadata ACL denial, parent/leaf swaps, cleanup |
| List/search | Enumerate through held directory HANDLEs; validate each regular-file identity before reporting/reading it | Multi-page enumeration, entry caps, junction swaps, hard-link aliases, cancellation, stable pagination and cleanup |
| Create | Exclusive handle-relative creation under held parent; no overwrite | Parent/leaf races, collision preservation, ACL denial, post-create ambiguous outcomes and cleanup |
| Write/publish | New staging object created by broker; publish only under a proven identity/ACL model | Concurrent alias attempts, crash points, atomic no-overwrite, commit ambiguity and cleanup |
| Edit/replace | Initially unsupported; fail closed | Requires a separate identity-conditional replacement design and explicit acceptance |
| Rename/move | Initially unsupported for regular files; directory semantics separately reviewed | Must exclude hard-link creation through commit; stale NumberOfLinks recheck is insufficient |
| Delete | Handle-relative identity-conditional deletion | Link policy, reparse, ACL, concurrent replacement, commit result and cleanup |

## Staged execution

A. Freeze the v1 read-only broker and its passing evidence; do not silently change its protocol.
B. Build a new v2 disposable service/fixture with a dedicated virtual account and protected root. Add a machine-readable operation matrix and protocol parser tests before adding filesystem verbs.
C. Add one operation family at a time, starting with root pinning and read/info/tail. Each family gets a separate harness with positive, negative, boundary, race, concurrency, fault-injection, cancellation and HANDLE-leak checks.
D. Add list/search, then exclusive create. Keep write/publish, edit, rename/move and delete disabled until their commit-boundary semantics are proven.
E. Add a Node stdio adapter only after native protocol framing, message limits, ACK/deadline behavior and identity checks pass. Do not wire it into the production MCP server during Phase 1.
F. At the phase gate, run lint, full npm tests, every native harness, source/diff review, secret scan, and cleanup verification. Record exact skipped/blocked tests.

## Current blockers

- Production handlers remain path-based.
- Hard-link exclusion at mutation commit boundaries is not solved by NumberOfLinks check/recheck alone.
- Metadata FILE_READ_ATTRIBUTES denial remains unresolved in the existing isolated candidate.
- Remote-client and complete pipe-DACL negative coverage remain outstanding.
- Partial request/response disconnect and full slow-client/fuzz coverage remain incomplete.
- Provider portability, automatic recovery, tamper resistance, formal review and Production integration are not accepted.

## Explicit non-goals

No Production service installation, no changes to configured ROOTS, no workspace ownership/DACL migration, no Production handler changes, no commits/resets, no unrelated service/process termination, and no changes to IranPriceScout or port 8765.

## Dedicated service identity and protected-root lifecycle design

This section is a design contract for the disposable v2 prototype. It does not authorize service installation or ACL changes by itself.

### Identity and privilege boundary

- Use one dedicated virtual service account named for the disposable service (`NT SERVICE\\<service-name>`). Never run the broker as the interactive user, LocalSystem, or a shared general-purpose account.
- Configure and verify the service SID explicitly. The implementation must record the effective process token identity and enabled/restricted SIDs at startup; a service name alone is not proof of the runtime token.
- The service must not require interactive logon, a user profile, stored password, network credentials, or a general-purpose shell. Disable service recovery/restart actions during disposable acceptance so failures remain observable and cleanup deterministic.
- Grant service-control rights only to the intended local administrators/test operator. The pipe client receives no service-control capability.
- Do not grant the broker privileges merely for convenience. Document every required token privilege and fail startup if an unexpected privilege or identity is present. No child process or shell execution is part of the broker contract.

### Protected root ownership and ACL model

- Create each disposable root beneath a dedicated test-only parent. The parent and root must have protected DACLs; inheritance from a user-writable ancestor is not accepted.
- SYSTEM and local Administrators retain administrative recovery access. The dedicated service SID receives only the access required by the currently enabled operation family. The interactive client and ordinary users receive no namespace-mutation, WRITE_DAC, WRITE_OWNER, or FILE_WRITE_ATTRIBUTES rights on the root or its protected ancestors.
- For the initial read-only milestone, the service SID receives only directory traversal/enumeration and file read/query rights needed by the implementation. Do not pre-grant create, delete, rename, or write rights before those operation families are separately designed and accepted.
- The root owner and DACL must be verified from opened handles at service startup and before a root is registered. A mismatch, unexpected ACE, inherited DACL, writable ancestor, or failed security query is a fail-closed startup error.
- The broker must not let a client choose or register an arbitrary path. Root IDs map to a fixed, administrator-provisioned configuration; clients send only root IDs and validated relative components.
- Reject a root or any traversed directory that is not the expected directory type or is a reparse point. Pin root and parent directory HANDLEs; do not validate a string path and later reopen it by name.
- For regular files, enforce the strict single-link policy on the opened file HANDLE. Query failure or NumberOfLinks other than one rejects the operation. This check is not a proven mutation-commit defense for future write/publish/rename/delete operations.

### Lifecycle state machine

1. **Provision (test-only):** an explicit administrator-controlled step creates a unique service identity/name, protected image/config locations, and an empty disposable root. Record exact paths and expected SIDs in a non-secret test manifest.
2. **Preflight:** verify service binary/config ACLs, parent/root owner and DACL, reparse status, service account mapping, service SID configuration, and absence of stale service/process/pipe/root artifacts. Any uncertainty aborts before service start.
3. **Start:** service obtains its effective token identity, validates its configuration, opens the configured root once, verifies the root HANDLE's type/security/reparse properties, and pins it. It creates no client-controlled paths during startup.
4. **Authenticate session:** accept only local named-pipe clients. Impersonate only long enough to read TokenUser SID and TokenSessionId; compare both to the configured interactive identity/session, then revert on every success and error path. Bind the authenticated session to the pinned root HANDLE set. LocalSystem or another session must be rejected even if it can open the pipe.
5. **Operate:** resolve only root IDs and validated relative components beneath pinned directory HANDLEs. Never fall back to path-based APIs. Each request has bounded framing, deadline, cancellation, and a request ID; after a partial frame or ambiguous timeout, discard the connection.
6. **Stop:** reject new sessions, cancel outstanding I/O, drain or safely reap every pending operation, close session/root HANDLEs, and report stop only after worker threads and handles are accounted for.
7. **Remove (test-only):** stop and verify service absence, verify broker process absence by exact image/path and service identity, then remove only the uniquely named disposable root/image/config artifacts. Verify absence afterward. Do not use broad process termination or wildcard cleanup.

### Required lifecycle acceptance evidence

- Positive: expected virtual account and service SID are present; service can open only the provisioned test root; expected client SID/session authenticates; pinned root survives benign path rename/replacement attempts without redirecting operations.
- Negative: LocalSystem, wrong user, wrong session, remote client, malformed/missing service SID, altered owner/DACL, inherited or writable parent, reparse root, stale root configuration, and unauthorized service-control requests are rejected.
- Fault/cancellation: service stop during pending read/write, client disconnect during authentication and transfer, cancellation completion races, restart with stale client handles, and cleanup after startup failure leave no live broker handles or orphaned test artifacts.
- Evidence: record service configuration, effective token/SID facts, ACL checks, exact test outcomes, and cleanup verification without recording secrets. Every skipped or blocked case remains explicitly NOT TESTED.

### Deliberately deferred

No production service registration, no real workspace root, no ACL/owner migration, no auto-recovery, no remote access, no filesystem mutation verb, and no Node adapter integration are authorized by this design. A successful service lifecycle test would still not establish hard-link-safe mutation semantics or Production readiness.

**Status: design specified; implementation of v2 operation families not yet started. Phase 1 NOT ACCEPTED; Production NO-GO.**
