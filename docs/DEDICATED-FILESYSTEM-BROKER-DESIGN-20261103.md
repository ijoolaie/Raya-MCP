# Dedicated Filesystem Broker — Architecture Proposal
Date: 2026-11-03
Status: **DESIGN + ISOLATED PROTOTYPE EVIDENCE — NO PRODUCTION APPROVAL**

## Executive decision

The same-identity filesystem model cannot currently prove the approved strict single-link invariant at the operation commit boundary. Local NTFS experiments have shown that share modes, a simple per-file DACL denial, RWH oplocks, Filter oplocks, and repeated NumberOfLinks checks do not exclude a concurrent hard-link creation by another process running as the same user.

The recommended direction is to move only privileged filesystem operations behind a small Windows service running under a dedicated, non-interactive service identity. The existing MCP process remains the user-facing protocol adapter and must not itself gain broader filesystem authority. The broker owns protected workspace objects and performs HANDLE-relative operations after validating every request.

This proposal does not authorize creating a service, changing ownership/ACLs, migrating workspace data, changing runtime configuration, restarting tunnels, or modifying Production handlers. Those steps require a separate explicit implementation approval and a reviewed migration/rollback plan.

## Goals and non-goals

### Goals
- Establish a real Windows security principal boundary between the interactive user and workspace object metadata.
- Make the service identity the owner of protected workspace directories and regular files, so the interactive user cannot rewrite their DACLs or take ownership.
- Ensure the interactive user cannot create hard links to protected regular files through direct filesystem access.
- Keep the strict policy: every regular file admitted to a confined operation must have FILE_STANDARD_INFO.NumberOfLinks == 1; unavailable or non-one values fail closed.
- Preserve root confinement, reparse-point rejection, handle-relative namespace pinning, bounded operations, cancellation semantics, and atomic publication guarantees.
- Limit privileged broker functionality to filesystem operations; do not grant it shell, PowerShell, process-launch, network, or arbitrary command execution.

### Non-goals
- Elevating the whole Raya MCP Node process.
- Running the broker as LocalSystem or granting broad backup/restore/take-ownership privileges.
- Exposing a remote TCP/HTTP administration surface.
- Changing the separate IranPriceScout tunnel or any unrelated workspace/service.
- Claiming provider portability beyond the filesystems and Windows versions explicitly tested.

## Proposed component topology

```text
ChatGPT / MCP client
        |
        | MCP stdio or existing authenticated local HTTP transport
        v
Raya MCP Node process (interactive user token; unprivileged)
        |
        | local Windows named pipe; versioned, bounded RPC
        v
Raya Filesystem Broker Windows service
        |  dedicated virtual service account + service SID
        |  no interactive logon; no admin membership
        |  owns protected workspace roots and objects
        v
HANDLE-relative filesystem layer (native implementation)
        |
        v
Protected workspace volumes / directories
```

The Node process remains responsible for MCP schemas, client sessions, non-filesystem tools, and transport. It forwards only explicitly allowlisted filesystem operations to the broker. Process/session tools continue to run under the interactive user's token and remain outside the broker's authority.

## Identity and privilege model

### Broker identity
Preferred first candidate: a dedicated Windows virtual service account / service SID for a service named specifically for Raya's filesystem broker. The final account form must be validated on the target Windows edition and service-hosting model before implementation.

Requirements:
- Not a member of Administrators.
- No interactive or remote-interactive logon.
- No SeBackupPrivilege, SeRestorePrivilege, SeTakeOwnershipPrivilege, or other unnecessary powerful privileges enabled.
- Service SID enabled and used to ACL the named pipe, service-owned state, and service binaries/configuration as appropriate.
- Service executable, DLLs, configuration, and update path are writable only by Administrators / trusted installer principals, not by the interactive user.
- Service runs with the minimum required privileges; do not use LocalSystem as a convenience.
- Service identity is the owner of the protected root directories and files. Ownership must not be transferred to the interactive user during normal writes, replacement, extraction, or restore.

### Interactive user
The interactive user must not have:
- WRITE_DAC or WRITE_OWNER on protected roots or regular files;
- FILE_WRITE_ATTRIBUTES on protected regular files where it enables hard-link creation;
- direct directory rights that enable bypassing the broker to create arbitrary hard links or replace protected entries;
- rights to modify service executable/configuration, service registry configuration, pipe ACLs, or service identity.

The exact DACL is operation-specific and must be generated from an explicit access matrix, not a broad inherited template. Prefer broker-mediated mutations. If direct user access is needed for compatibility, grant only the minimum content-read or content-write rights proven not to permit metadata/namespace bypass. Do not assume that denying one right is sufficient without adversarial tests against the complete effective token and all parent-directory rights.

### Important limitation
A broker cannot enforce its policy if the interactive user retains another path to mutate the protected objects or can modify the broker itself. The security boundary therefore includes root ownership, every ancestor directory, reparse points, backup/restore workflows, installer/update paths, service control permissions, and any alternate process that can write the same volume.

## Named-pipe RPC boundary

Use a local named pipe only; do not add a TCP listener for the broker.

Required controls:
1. Apply a restrictive pipe DACL that admits the broker service SID and the intended interactive user/logon SID only.
2. Reject remote clients and validate the connecting client's token SID/session; do not trust a caller-supplied username or PID.
3. Bound message size, nesting, path length, operation duration, concurrency, and queued requests.
4. Use a versioned, typed request/response envelope with request IDs and structured error codes.
5. Validate every request again inside the service. MCP-side validation is not a security boundary.
6. Do not accept arbitrary native paths, raw handles, DLL names, command strings, or caller-selected security descriptors.
7. Bind each request to a broker-side configured root identifier. Resolve child names relative to held root/parent HANDLEs; never authorize by string-prefix checks alone.
8. Apply per-operation authorization and explicit overwrite/no-overwrite semantics.
9. Treat disconnect/cancellation as a lifecycle signal; never report cancellation after a mutation has committed. Complete cleanup and distinguish pre-commit cancellation from committed-but-cleanup-failed.
10. Audit operation class, root ID, result, duration, and correlation ID; never log file contents, credentials, bearer tokens, or sensitive path components unnecessarily.

The broker should not impersonate the client for protected filesystem operations. Impersonation would revert the operation to the user's weaker authority and undermine the ownership boundary. Instead, authenticate the client once, authorize the requested operation against broker policy, and perform it under the broker identity. This makes broker-side authorization critical: a compromised or confused-deputy request could otherwise read or mutate any object the service can access.

## Filesystem and DACL design

- Service identity owns the protected root and all descendants.
- Administrators / trusted installer principals retain recovery and servicing access; ordinary user access is explicit and narrow.
- The interactive user must not be able to change owner, DACL, parent namespace, or protected file attributes directly.
- The broker must create temporary/staging files with the protected owner and intended DACL from the moment of creation; do not create user-owned staging objects and later attempt to repair them.
- Atomic replacement/publication must preserve protected ownership and DACL and must not expose a writable staging path to the interactive user.
- New directories must inherit the intended protected ACL; the broker must verify the resulting owner and security descriptor after creation.
- Hard-link checks must be made on the opened file HANDLE. The broker must ensure no untrusted principal has rights to create an additional link during the complete operation interval. The single-link count check remains a defense-in-depth assertion, not the exclusion primitive.
- Reparse points, mount points, alternate data streams, case normalization, short names, volume changes, and ancestor replacement require explicit policy and tests.
- Do not rely on inherited ACLs without inspecting the effective descriptor on every created/replaced object and parent.

## Operation contract

Initially expose only a small vertical slice:
- open/read child relative to an authorized root/parent;
- inspect metadata under the same authorization policy;
- enumerate directory entries with per-entry validation;
- create a new file exclusively;
- publish/replace through a broker-owned staging object;
- rename/move with explicit no-overwrite semantics;
- create directories;
- delete/unlink with explicit object-type and parent checks.

Do not expose raw generic filesystem primitives that let the caller compose arbitrary privileged actions. Each RPC method should encode a complete policy-checked operation and return a stable result contract.

Every regular-file operation must reject if link count is not exactly one or cannot be queried. Directories/reparse points require their own object-type and reparse policy; the regular-file link-count rule must not be blindly applied to directories.

## Threat model

| Threat | Required mitigation | Acceptance evidence |
|---|---|---|
| Same-user process attempts CreateHardLinkW | User lacks FILE_WRITE_ATTRIBUTES and cannot rewrite protected DACL/owner; broker-owned file | Cross-process adversarial test under real interactive token |
| User attempts WRITE_DAC / WRITE_OWNER | Protected owner and DACL; no owner takeover privilege | Effective-access and live operation denial tests |
| User writes directly to workspace namespace | Minimal parent-directory rights; broker-mediated mutation | Attempt create/delete/rename/reparse through Win32 and native APIs |
| Malicious/buggy MCP request | Broker-side schema, root-ID authorization, operation allowlist | Fuzzing, invalid root IDs, path traversal and type confusion |
| Confused deputy / unauthorized read | Per-client authorization and root capability mapping | Cross-user and cross-session pipe tests; unauthorized root read denial |
| Pipe spoofing or remote connection | Restrictive pipe ACL, local-only validation, client token inspection | Negative remote and wrong-SID tests |
| Broker compromise | Minimal service rights, small codebase, no shell/network, hardened update path | Static review, service configuration audit, attack-surface inventory |
| Crash during mutation | Staging/commit protocol, durable state only where needed, recovery journal | Crash injection at every commit boundary |
| Backup/restore changes owner or ACL | Explicit restore procedure and post-restore verification | Backup/restore round-trip with owner/DACL assertions |
| Upgrade changes service ACL or identity | Signed/trusted installer, stable service SID, migration checks | Upgrade/rollback acceptance on clean VM |
| Volume/provider differs | Capability detection and fail-closed unsupported result | NTFS first; separate ReFS/network/removable/cloud matrix |

## Migration and deployment sequence (future, separately approved)

1. **Design review:** approve service name, account type, pipe contract, root model, DACL matrix, threat model, recovery administrator, and compatibility requirements.
2. **Disposable VM prototype:** create a throwaway service and fixture root only. Do not use the real workspace. Validate service identity, pipe ACL, effective rights, and hard-link denial.
3. **Broker vertical slice:** implement one read-only operation, then one exclusive create, with tests proving user cannot create a hard link or rewrite DACLs.
4. **Mutation family:** add publish/replace, rename/move, directory create, delete, enumeration, metadata, and read-tail one family at a time. Each gets adversarial and crash tests.
5. **Data migration rehearsal:** copy a disposable fixture tree; verify owner, DACL, alternate streams, timestamps, reparse policy, and file IDs. Do not migrate the real root during rehearsal.
6. **Compatibility review:** verify existing MCP clients and error/result shapes; preserve stdio/HTTP transport behavior.
7. **Operational readiness:** service recovery, startup ordering, logs, upgrade, rollback, backup/restore, uninstall, and administrator recovery.
8. **Separate production change request:** only after the above evidence, obtain explicit approval for real workspace ownership/ACL migration, service installation, configuration changes, and controlled cutover.

## Acceptance gates

The broker design is not accepted until all are true:
- Service token is demonstrably distinct from the interactive token and has no unnecessary powerful privileges.
- Interactive token cannot change protected file owner/DACL, grant itself FILE_WRITE_ATTRIBUTES, create hard links to protected regular files, or bypass the broker through parent namespace operations.
- Broker pipe is accessible only to the intended local client identity; unauthorized sessions and remote clients are rejected.
- Broker-side authorization prevents access to unconfigured roots and prevents confused-deputy operations.
- All operation families pass positive, negative, boundary, concurrency, hard-link, reparse, crash, cancellation, and cleanup tests.
- Service binary/config/update paths cannot be modified by the interactive user.
- Backup/restore, upgrade, rollback, service restart, and crash recovery preserve ownership and ACL invariants.
- NTFS results are recorded precisely; other providers remain unsupported until independently tested.
- npm run lint, npm test, and the complete native acceptance suites pass in the candidate environment.
- Security review confirms no Production handler has been silently switched to broker access and no unrelated service/tunnel was changed.

## Rollback and recovery

Rollback must be designed before deployment. It requires an administrator-controlled, audited procedure to stop accepting broker requests, preserve data, restore the previous owner/DACL from a verified manifest or backup, and return to the previous runtime only if that previous mode is explicitly accepted as lower security. Do not silently fall back to same-user path-based operations after broker failure; fail closed and report the service unavailable. Never recursively rewrite ACLs as an automatic recovery action.

## Current decision and constraints

- This document remains the architecture proposal; the disposable prototype evidence is recorded in `.native-probe/broker-prototype-20261103/RESULTS-20261103.md`.
- A temporary service and fixture were created only in the explicitly approved isolated test scope, then stopped, deleted, and cleaned up. No Production service, workspace ACL/owner, configured root, runtime, tunnel, or Production code was changed.
- The native named-pipe slice implements only one fixed-root read operation and passes three Win32 client cases. It is not a complete broker.
- The Node.js `net` named-pipe client fails with EPIPE even against an independent minimal Win32 byte-mode echo pipe; the underlying cause remains unresolved. An isolated native console adapter successfully bridges Node `child_process` stdio to a Win32 named pipe in byte and message modes, but the adapter has not yet been tested against the broker protocol/service. Safe Raya MCP transport integration is therefore NOT ACCEPTED. See `.native-probe/broker-prototype-20261103/RESULTS-20261103.md`.
- Different-user/session denial, remote-client denial, pipe ACL negative tests, fuzzing, concurrency, crash recovery, and other operation families remain untested.
- The earlier strict single-link policy remains approved, but its race-free enforcement across Production operation families is not established.
- Production remains **NO-GO** and Phase 1 remains **NOT ACCEPTED**.
- Any Production implementation, workspace ownership/ACL migration, runtime integration, or service deployment requires a separate explicit approval and reviewed migration/rollback plan.

## 2026-11-04 prototype follow-up

The disposable named-pipe prototype now has a verified Node native-adapter protocol path for the one fixed-root read operation. The framing defect (header and payload emitted as separate message-mode messages) was corrected by sending one bounded response message. Current local NTFS test evidence and lifecycle checks are recorded in `.native-probe/broker-prototype-20261103/RESULTS-20261103.md`. This does not change the acceptance gates above: the prototype remains synchronous, single-instance, and based on `PIPE_NOWAIT` polling; it is not production-grade overlapped I/O. Different-user/session, remote-client, pipe-DACL negative, concurrency, crash-recovery, tamper-resistance, other operation-family, provider-portability and formal-review gates remain open. No Raya MCP runtime integration or Production changes were made. Production remains **NO-GO**; Phase 1 remains **NOT ACCEPTED**.


## 2026-11-04 transport update
The disposable named-pipe broker now uses FILE_FLAG_OVERLAPPED with PIPE_WAIT for connect, request read, and response write. Per-operation events are waited with bounded deadlines and service-stop cancellation via CancelIoEx. Initial build and integration/lifecycle tests passed, including idle-read timeout and service stop while a read was pending. The tested service run used LocalSystem plus the service SID because SCM rejected virtual-account configuration (1057); dedicated virtual-account deployment remains unverified. The prototype remains single-instance and non-production. Production NO-GO; Phase 1 NOT ACCEPTED.

## 2026-11-04 dedicated service-account deployment revalidation
The initial overlapped-I/O test used LocalSystem after a failed command-line service-account configuration. A subsequent isolated run resolved this using a native `ChangeServiceConfigW` helper and confirmed `NT SERVICE\RayaFsPipeProbe20261103` through `QueryServiceConfigW` and SCM. The overlapped broker reached RUNNING under that virtual account; fixture and image directory ownership matched the service SID. Win32 and Node protocol tests, idle deadline, reconnect, stop during pending read, restart, and non-reading-client recovery passed. Service and ProgramData artifacts were removed and absence verified. This improves evidence for this host only; it does not establish general deployment readiness. Different-user/session, remote, pipe-DACL, forced blocked-write, concurrency, crash/tamper, and formal security tests remain open. Production NO-GO; Phase 1 NOT ACCEPTED.

## 2026-11-04 restricted-token DACL negative checks
A restricted impersonation token for the interactive user (Administrators SID disabled; privileges removed) was denied fixture file write, WRITE_DAC, WRITE_OWNER, FILE_WRITE_ATTRIBUTES, directory FILE_ADD_FILE/FILE_ADD_SUBDIRECTORY, broker-image write, and CreateHardLinkW into the protected fixture directory. The hardlink check used an actual CreateHardLinkW operation and returned ACCESS_DENIED. An alternate service-token authorization check was inconclusive because the test harness could not open the service process token (ERROR_ACCESS_DENIED); different-user/session denial remains NOT TESTED. Remote-client and explicit pipe-DACL connection denial remain open. Disposable artifacts were cleaned. Production NO-GO; Phase 1 NOT ACCEPTED.
