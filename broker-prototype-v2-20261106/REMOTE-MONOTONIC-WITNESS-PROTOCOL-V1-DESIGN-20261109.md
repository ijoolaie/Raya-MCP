# Remote Monotonic Witness Protocol v1

Date: 2026-11-09
Status: design only; no endpoint, credentials, service, storage adapter, or Production integration
Decision: define a remote compare-and-advance authority outside the host rollback domain. This protocol is not yet approved for deployment.

## 1. Purpose and security boundary

The witness is the independent monotonic authority for one logical Supervisor namespace. The local two-slot generation ledger remains a crash-recovery journal/cache only. It MUST NOT authorize a generation when it disagrees with the witness.

The witness protects against restoration of an older local ledger, VM snapshot rollback, and duplicate issuers only when the witness service, its durable state, credentials, administration, and recovery process are outside the host's rollback/clone domain. A witness deployed inside the same VM, restored from the same image, or operated with credentials embedded in a reusable host image does not provide this property.

The protocol carries no filesystem paths, file contents, Worker commands, process handles, arbitrary RPC, or secrets other than transport authentication material managed outside protocol payloads.

## 2. Trust model and required properties

### Trusted components
- Witness service implementation and its monotonic namespace database.
- Witness signing identity and key lifecycle.
- TLS trust configuration and the Supervisor's protected client credential access.
- Operator-controlled namespace provisioning and recovery workflow.

### Untrusted or failure-prone components
- Local ledger bytes, local backups, host/VM snapshots, and cloned disks.
- Network delivery: requests and responses may be delayed, duplicated, reordered, or lost.
- A Supervisor may crash at any instruction boundary.
- A caller may retry after a timeout without knowing whether the witness committed.
- Multiple Supervisor instances may race, including instances on cloned hosts.

### Security invariants
1. A namespace sequence never decreases and never advances by more than one per committed operation.
2. For a given namespace and operation_id, at most one advance can commit; retries return the original outcome.
3. A sequence is not reusable after a committed advance, even if its response or local persistence is lost.
4. A response is accepted only when bound to the current authenticated witness, namespace, operation_id, request digest, and connection/request nonce.
5. The local ledger never moves the witness backward and never overrides witness state.
6. Any ambiguous result blocks generation publication until the same operation_id is resolved.
7. Namespace credentials and private keys are not stored in a reusable host image or local rollback-prone ledger.

## 3. Namespace identity and provisioning

A namespace is a 128-bit opaque identifier allocated by an authorized provisioning workflow. It is not derived solely from hostname, SID, disk ID, VM ID, or a local random value. Provisioning binds:
- namespace_id;
- deployment/application identity and environment;
- authorized Supervisor service identity and expected service SID;
- witness tenant/account and witness key identity;
- initial sequence (normally zero before first advance);
- provisioning operation ID, timestamp, operator approval reference, and audit record.

The witness MUST reject an unknown namespace and MUST NOT auto-create one on READ or ADVANCE. Provisioning is a separate privileged administrative operation, outside this protocol's runtime API. A clone MUST receive a new namespace and separately provisioned credentials. Re-provisioning an existing namespace requires explicit retirement/recovery workflow; it is never an automatic response to a missing local file.

The namespace identifier is public routing metadata, not an authentication credential.

## 4. Transport authentication and cryptographic binding

### Transport
- Use HTTPS over TLS 1.3 where supported by the selected platform and witness provider; otherwise document and approve the exact secure TLS profile before implementation.
- Require mutual TLS. The client certificate identifies the provisioned Supervisor deployment; server certificate validation checks a configured trust anchor and an expected witness service identity.
- Disable redirects for protocol endpoints. Enforce hostname validation, certificate validity, revocation policy, bounded response sizes, and strict connect/read deadlines.
- Do not permit plaintext, insecure certificate-validation bypass, proxy downgrade, or alternate unauthenticated endpoint fallback.

### Credential storage and lifecycle
- Client private key access is restricted to the dedicated service identity. Prefer a non-exportable hardware-backed key only after platform/API and recovery behavior are proven; do not claim TPM rollback resistance merely because a key is TPM-protected.
- If a software key is used, its protected store, ACL, backup/restore behavior, rotation, revocation, and clone handling require explicit threat review. DPAPI protection alone is not anti-rollback.
- Never place private keys, bearer tokens, passwords, or client secrets in command lines, environment variables, source files, logs, local ledger records, or golden images.
- Support overlapping credential rotation: provision new credential, verify it against the same namespace, switch the service, confirm successful authenticated READ, then revoke old credential. Lost rotation responses are resolved through an administrative status operation.
- Revocation or credential mismatch is fail-closed. No fallback to a shared static secret.

### Response authenticity
TLS server authentication is mandatory. In addition, each response carries witness_id and key_id. The selected implementation SHOULD sign canonical response bytes with a witness signing key whose public verification key is pinned through an independently managed trust configuration. This protects stored/audited response artifacts and makes witness identity explicit; it does not replace TLS.
The canonical signature input includes protocol version, message type, witness_id, namespace_id, operation_id (zero only for READ if specified), request_digest, client_nonce, committed_sequence, result_code, and response_nonce. Signature algorithm and encoding must be fixed by an implementation profile; algorithm negotiation from untrusted messages is prohibited. Key rotation requires an authenticated, versioned trust-bundle update and overlap window.

## 5. Wire protocol profile

All integers are unsigned big-endian. All messages have a fixed schema, bounded length, protocol version, message type, and nonzero request_id. Unknown fields, duplicate fields, noncanonical encodings, trailing bytes, invalid enum values, and oversized bodies are rejected. JSON may be used only if a canonicalization profile is selected and tested; a compact binary schema is preferred. Do not serialize native C structs directly.

Common request envelope:
- protocol_version: exactly 1;
- message_type;
- request_id: nonzero 64-bit value, unique within a TLS connection and never reused on that connection;
- namespace_id: 16 bytes;
- client_nonce: 32 bytes from the Windows CNG system RNG;
- client_key_id: provisioned credential identifier;
- operation_id: 16 bytes for mutating/idempotent operations; absent only for READ;
- body_digest: SHA-256 of the canonical operation body.

Common response envelope:
- protocol_version and message_type;
- request_id echo;
- witness_id and witness_key_id;
- namespace_id echo;
- operation_id echo, when applicable;
- request_digest echo;
- client_nonce echo;
- response_nonce: 32 fresh bytes from witness CSPRNG;
- result_code;
- committed_sequence;
- canonical response signature and signature algorithm profile identifier.

The TLS connection is bound to one authenticated client credential. A fresh client_nonce is required for every logical request. A response with any mismatched echo, wrong witness identity/key, invalid signature, stale connection, or unexpected request_id is rejected and the channel is discarded. Nonces are freshness/binding values, not standalone authenticators.

### READ

Request body: empty. Required fields: namespace_id, request_id, client_nonce, client_key_id. operation_id is absent.

Success response returns namespace status, committed_sequence, namespace lifecycle state, and witness policy version. Unknown/retired namespaces return explicit errors and no sequence usable for issuance.

READ is observational and may be repeated. It MUST NOT create or mutate a namespace. The caller must authenticate the response and verify every binding field.

### ADVANCE

Request body contains:
- expected_sequence: uint64;
- next_sequence: uint64, required to equal expected_sequence + 1;
- operation_id: 16-byte nonzero globally unique value within the namespace;
- optional reason_code from a fixed allowlist (no free-form secrets or paths).

Witness transaction semantics are atomic:
1. Authenticate client and authorize its certificate for namespace and operation.
2. Look up (namespace_id, operation_id) before evaluating current sequence.
3. If the operation exists with the same canonical request digest, return its original committed result without another advance.
4. If the operation exists with a different digest, return OPERATION_ID_CONFLICT and do not mutate state.
5. Otherwise compare current sequence with expected_sequence.
6. If unequal, return SEQUENCE_MISMATCH with the authoritative current sequence; do not mutate.
7. If equal and next_sequence is exactly expected_sequence + 1, atomically persist both the new sequence and immutable operation result/digest.
8. Only after durable commit return ADVANCED.

The namespace sequence update and idempotency record MUST share one atomic transaction or equivalent crash-safe commit protocol. A response must never claim ADVANCED before durable commit. Operation records are retained at least as long as the namespace exists; pruning requires a separately proven replay-safe compaction design. Sequence overflow retires the namespace and requires explicit replacement; wraparound is forbidden.

### OPERATION_STATUS

Request body contains operation_id and the original request_digest. It is authenticated and nonce-bound like READ.

Response is one of:
- COMMITTED: includes the original digest and committed_sequence;
- NOT_FOUND: no committed operation record exists at the time of the linearizable query;
- CONFLICT: operation_id exists with a different digest;
- UNKNOWN/UNAVAILABLE: witness cannot safely determine the outcome.

NOT_FOUND is not permission to invent a new operation_id for the same intended advance. Retry ADVANCE with the original operation_id and identical request body. UNKNOWN/UNAVAILABLE blocks issuance.

A NOT_FOUND result can race with an in-flight request if the server does not serialize operations correctly. Therefore the witness must provide linearizable status semantics with respect to ADVANCE for the same namespace/operation_id, or the client must retry the exact ADVANCE and rely on idempotency. The implementation MUST document which guarantee is provided; the preferred contract is linearizable per-namespace operation processing.

## 6. Idempotency, replay resistance, and concurrency

- operation_id is generated once by CNG before the first ADVANCE attempt and is persisted in protected local recovery state before transmission. If that persistence is uncertain, do not send; enter DEGRADED.
- Every retry of the same logical operation uses exactly the same operation_id, expected_sequence, next_sequence, and canonical body digest.
- A new operation_id is created only after the previous operation is conclusively COMMITTED or conclusively NOT_FOUND and abandoned under an explicit state transition.
- Replaying an old ADVANCE with its original operation_id returns the original outcome and cannot increment the sequence again.
- Replaying an old response on a new request fails nonce/request_id/digest binding and signature checks.
- Concurrent writers use compare-and-advance. Exactly one operation can commit from a given expected_sequence; all others receive SEQUENCE_MISMATCH and must reconcile rather than silently choose another epoch.
- The witness database enforces a unique constraint on (namespace_id, operation_id) and serializable/linearizable sequence advancement.
- The client serializes its own issuance transaction with a cross-process singleton/writer lock. The remote CAS is still required because host-local locking cannot coordinate clones or separate machines.

## 7. Client transaction with the local generation ledger

The following is the required candidate transaction; it is not yet implemented or approved for Production.

1. Acquire the cross-process Supervisor issuance lock and lifecycle lock.
2. Verify service identity, client credential access, witness TLS identity, and namespace binding.
3. Load the local journal without auto-initializing. Preserve its raw bytes and metadata for recovery.
4. Perform authenticated READ and validate witness identity, signature, namespace, and nonce/request binding.
5. Reconcile:
   - local epoch equals witness sequence: eligible to attempt next advance;
   - local epoch below witness sequence: ROLLBACK_SUSPECTED; preserve evidence and follow recovery policy, not silent overwrite;
   - local epoch above witness sequence: DEGRADED/RECOVERY_REQUIRED; local state cannot overrule witness;
   - namespace mismatch, retired namespace, invalid local journal, or unknown witness state: fail closed.
6. Persist a pending operation record (operation_id, expected, next, request digest) in a separately protected recovery journal before sending ADVANCE. Its durability contract must be designed and tested; the current two-slot epoch ledger does not yet store pending operation metadata.
7. Send ADVANCE once. On authenticated ADVANCED, verify exact binding and committed sequence.
8. On timeout, disconnect, process crash, or invalid/missing response, do not create a new operation_id and do not issue a generation. Reconnect with a fresh TLS session and query OPERATION_STATUS using the same operation_id/digest. If COMMITTED, continue reconciliation. If NOT_FOUND, retry the exact ADVANCE with the same operation_id/body. If UNKNOWN, CONFLICT, or witness unavailable, remain DEGRADED/RECOVERY_REQUIRED.
9. Persist the committed sequence in the local journal, flush, read back, and verify. A local persistence failure after remote commit burns that sequence; do not ask the witness to decrement or reuse it. Restart must reconcile local state to the witness under an explicit recovery procedure.
10. Only after remote commit and local durable readback agree, generate the CNG suffix and publish the generation. CNG failure burns the committed epoch.
11. Record an audit event without credentials or private data, then release locks.

Critical unresolved design point: the current ledger record stores only epoch and has no pending-operation slot. A separate write-ahead intent journal or an extended atomic local record is required before this transaction can safely survive a crash between request send and response. The local intent journal itself must not be treated as the anti-rollback authority; the witness remains authoritative.

## 8. Client and witness state machines

Client states:
- UNPROVISIONED: no valid namespace/credential binding; no requests except approved provisioning.
- READY: local journal and witness agree; issuance may begin.
- ADVANCE_PENDING: durable local intent exists; operation outcome may be unresolved; no new issuance.
- DEGRADED: transport, signature, local write/flush/readback, or credential outcome is uncertain.
- ROLLBACK_SUSPECTED: local sequence is behind the witness or restore/clone evidence exists.
- RECOVERY_REQUIRED: witness/local state cannot be reconciled or credentials are unavailable/revoked.
- RETIRED: namespace permanently disabled.

Witness namespace states:
- ACTIVE: authenticated READ/ADVANCE permitted to authorized clients.
- ROTATING: only explicitly permitted old/new credential overlap.
- RECOVERY_LOCKED: reads may be allowed for recovery; advances denied.
- RETIRED: no further advances; status/audit access follows retention policy.

Forbidden client transitions:
- DEGRADED -> READY merely by reconnecting or rereading local bytes.
- ADVANCE_PENDING -> new operation_id without resolving the existing one.
- ROLLBACK_SUSPECTED -> READY by copying a backup or lowering the witness sequence.
- UNPROVISIONED -> ACTIVE through runtime READ/ADVANCE.
- Any witness outage -> offline issuance without a separately specified and approved lease protocol.

## 9. Errors and availability policy

Use stable machine-readable result codes: AUTH_FAILED, NAMESPACE_UNKNOWN, NAMESPACE_RETIRED, NAMESPACE_LOCKED, SEQUENCE_MISMATCH, OPERATION_ID_CONFLICT, INVALID_REQUEST, RATE_LIMITED, WITNESS_UNAVAILABLE, INTERNAL_UNCERTAIN, and ADVANCED/COMMITTED.

Retry policy:
- Bounded exponential backoff with jitter for transport establishment and read-only READ.
- ADVANCE retries only with the exact same operation_id and body.
- After any ambiguous ADVANCE outcome, resolve status first or retry exact ADVANCE if the witness contract guarantees idempotency.
- Never interpret timeout as failure or success.
- Never fail open to local-only issuance.
- Define a maximum recovery outage and alerting policy operationally; the security result during outage remains no issuance.

Availability target, SLO, geography, replication, RPO/RTO, and disaster recovery are intentionally unspecified pending provider selection. A replicated witness must preserve linearizable CAS and idempotency records through failover. Restoring an old witness database backup is itself a monotonicity failure: witness recovery requires an independent control-plane anchor or a documented re-provision/retirement process. A remote witness is not automatically trustworthy merely because it is remote.

## 10. Audit, privacy, and operations

Audit provisioning, credential enrollment/rotation/revocation, READ failures, every ADVANCE attempt and final outcome, status resolution, sequence mismatch, rate limit, namespace lock/retirement, recovery, and denied rollback. Each event includes witness_id, namespace_id, operation_id where relevant, expected/committed sequence, request digest, authenticated client identity, result, timestamp, and correlation ID. Never log private keys, certificate private material, bearer tokens, TLS secrets, or full environment/command lines.

Audit storage must be append-only or tamper-evident under a separately defined retention/control-plane policy. Host-local logs alone are not authoritative. Clock timestamps are diagnostic, not sequence authority.

Operations must define independent witness backup/restore, quorum/failover, key custody, certificate revocation, emergency access, namespace retirement, operator separation of duties, incident evidence retention, and service recovery. No operator action may silently decrement a sequence.

## 11. Acceptance test matrix

1. Provisioning: unknown namespace rejected; no auto-create; unauthorized client denied.
2. Authentication: wrong client cert, revoked cert, wrong server identity, expired cert, wrong witness key, and TLS downgrade all fail closed.
3. Namespace binding: cross-namespace request/response substitution rejected.
4. Canonicalization: altered body, duplicate fields, trailing bytes, noncanonical integer, and digest mismatch rejected.
5. CAS race: many concurrent ADVANCE requests from the same expected sequence produce exactly one committed sequence.
6. Idempotency: duplicate identical ADVANCE returns the same committed result and increments once.
7. Conflict: same operation_id with different digest is rejected without mutation.
8. Lost response after commit: status returns COMMITTED; no second increment.
9. Lost request before commit: status/identical retry resolves without skipping or duplicating unexpectedly.
10. Replayed response: old nonce, request_id, digest, or TLS session binding rejected.
11. Reordered requests: per-connection request sequencing and per-namespace CAS remain correct.
12. Client crash before send: pending intent is safe and can be abandoned only under explicit proof no send occurred.
13. Client crash after send/before response: same operation_id resolves after restart.
14. Client crash after remote commit/before local write: witness remains ahead; no local-only issuance; recovery burns/reconciles epoch.
15. Local write/flush/readback failures: no generation publication.
16. Witness outage, partition, DNS failure, timeout, rate limit: no issuance.
17. Witness failover: linearizable sequence and idempotency survive leader change.
18. Witness backup rollback: detected by independent witness-control-plane recovery or namespace retirement; never silently accept old state.
19. Clone/VM revert: duplicate host cannot issue from stale local state; credential and namespace policy blocks clone.
20. Credential rotation overlap, lost rotation response, revocation, and stale credential retry.
21. Signing-key rotation, trust-bundle rollback, revoked witness key, and compromised key recovery.
22. Sequence UINT64_MAX: fail closed and retire namespace.
23. CNG failure after remote/local commit: output zeroed; epoch remains burned.
24. Audit failure: issuance policy explicitly defined; default fail closed if required audit cannot be durably recorded.
25. Resource exhaustion, malformed floods, slowloris, oversized responses, and TLS reconnect storms are bounded.
26. Concurrent Supervisors across separate hosts and cloned images remain serialized by witness CAS.

Use a disposable witness and isolated test namespaces. Fault injection must drop requests/responses at every transaction boundary and restart the client/witness independently. Unit tests do not prove TLS configuration, database durability, linearizability, backup independence, or operational key custody.

## 12. Implementation decomposition and no-go gates

Recommended isolated stages:
A. Freeze a canonical binary schema and cryptographic profile; review it independently.
B. Build pure codec and transcript-binding tests with fixed vectors and malformed-input corpus.
C. Build a mock witness state machine with durable transaction abstraction, unique operation IDs, CAS, and fault injection.
D. Prove concurrent linearizable ADVANCE and idempotent replay under process restart.
E. Select a real provider/hosting model and verify its backup/restore, key custody, regional failover, and administrative independence.
F. Implement a client transport adapter with strict TLS/mTLS validation and signed response verification.
G. Implement protected credential provisioning/rotation and the durable local pending-operation journal.
H. Test local ledger + witness transaction across every crash boundary and restore/clone scenario.
I. Complete service identity, ACL, process isolation, authenticated local IPC, and end-to-end Broker acceptance.

Production remains NO-GO until all stages, independent security review, operational runbooks, disaster recovery, and supported-host acceptance pass. This document authorizes design and isolated pure-code testing only. It does not authorize a cloud account, endpoint, DNS record, credential, certificate, service, persistent storage file, ACL, TPM provisioning, network change, or Production wiring.
