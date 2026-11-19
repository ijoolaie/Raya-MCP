# Evidence Service Trust and Anti-Rollback v1 — Design

Date: 2026-11-10
Status: design-only; draft; not frozen; no implementation authorization
Production: NO-GO

## 1. Purpose and boundaries

Define how an independent Evidence Service protects captured journal objects and receipts against deletion, substitution, metadata rollback, stale backups, trust-bundle rollback, and split-brain. "Remote" or "cloud-hosted" is not itself a security property. Trust requires independently verifiable identity, durability, monotonic state, signing authority, administration, audit, and disaster recovery.

Complements the Evidence Receipt v1, Recovery Authorization v1, and Remote Monotonic Witness v1 drafts. It does not prove the capture process observed the correct live file; same-handle capture provenance remains a separate gate.

## 2. Threats and protected properties

Threats include compromised Broker host/client; VM snapshot, backup restore or clone; old service database restored over newer state; stale/substituted/truncated objects; administrator deletion or retention weakening; stolen/revoked signing key; stale trust bundle; replay/redirect/network ambiguity; replica disagreement; time rollback/outage.

Protected properties:
1. Committed object IDs resolve to exact immutable bytes during retention.
2. Receipt issuance occurs only after the declared durable commit boundary.
3. Receipts, revocations, retention locks and audit events cannot disappear silently after restore.
4. Stale replicas/backups cannot claim authority without independent monotonic reconciliation.
5. Status is authenticated, fresh, namespace-bound and digest-bound.
6. UNKNOWN/conflict blocks; local cache/host clock cannot promote it to COMMITTED.
7. Evidence Service has no journal-write or Broker-start authority.

## 3. Independent trust domains

Separate: API/workload identity; object data plane; metadata/transaction database; receipt signing-key custody; trust-bundle/revocation control plane; monotonic witness; append-only audit; backup/DR control plane; human administration.

Monotonic witness, signing-key control and recovery authority must not share the same host snapshot/backup rollback domain as the object/metadata service. Prefer separate administrative principals and independent audit. If one provider is used, document account, region, identity, control-plane and backup-domain independence; branding is not proof.

## 4. Authoritative monotonic state

### 4.1 Witness

Require an authority independent of the restorable Evidence Service database. Candidate per-namespace state:
- evidence_namespace_id;
- service_generation;
- committed_sequence;
- latest_commit_digest and receipt_id/digest;
- retention_policy_generation;
- trust_bundle_generation;
- audit_checkpoint_sequence/digest;
- lifecycle ACTIVE, RECOVERY_LOCKED, RETIRED.

Witness provides authenticated linearizable READ and compare-and-advance. Every advance binds expected prior state, exact next state, operation_id and canonical request_digest. Same ID+digest is idempotent; same ID with different digest conflicts. Overflow retires namespace.

### 4.2 Commit protocol (candidate; ordering not frozen)

1. Authenticate caller; validate exact capture/package digest.
2. Verify raw object lengths/hashes and manifest consistency.
3. Write unique objects to immutable storage; commit/flush under documented durability contract.
4. Persist PREPARED metadata with exact object IDs, lengths, hashes, package digest and retention generation.
5. Advance independent witness for exact next sequence and operation ID, binding exact commit digest.
6. Finalize metadata COMMITTED and activate retention lock.
7. Independently read back and verify every object and metadata binding.
8. Append receipt event to tamper-evident audit and obtain required checkpoint.
9. Issue signed receipt only after object bytes, metadata, retention, witness and required audit are durably established.

Ordering of witness advance vs metadata finalization is an explicit freeze gate. Crashes can leave witness ahead or metadata PREPARED. Recovery reconciles exact operation IDs/digests; never decrement or reuse sequence. Define crash-state table before implementation. API acceptance or upload completion alone never justifies a receipt.

### 4.3 Status

STATUS_RECEIPT(receipt_id, receipt_digest, package_digest) returns authenticated:
- COMMITTED with exact digests, service generation/sequence, manifest digest, retention and audit checkpoint;
- NOT_FOUND, linearizable within authoritative generation;
- REVOKED_OR_CORRUPT;
- RECOVERY_LOCKED;
- CONFLICT;
- UNKNOWN/UNAVAILABLE.

Cached receipt alone is insufficient for destructive recovery; live status and object verification are required. NOT_FOUND must be linearizable relative to commit; otherwise remain blocked and retry exact operation ID/digest.

## 5. Backup, restore, clone and split-brain

On every start/failover/restore compare local durable state to witness:
- exact sequence/generation and checkpoint match: eligible only after full integrity checks;
- local behind witness: RESTORE_ROLLBACK_SUSPECTED; lock writes/receipts, preserve evidence;
- local ahead: UNWITNESSED_STATE; lock, never auto-advance witness;
- same sequence/different digest: FORK_OR_TAMPER; lock and raise incident;
- witness stale/unavailable: RECOVERY_LOCKED;
- namespace/generation mismatch: CLONE_OR_REPROVISION_SUSPECTED.

Never repair by selecting whichever copy has the larger number. Reconcile exact commit log and object digests.

Restore runbook: isolated start with writes disabled; authenticate witness; present generation/sequence/checkpoint; obtain explicit recovery authorization bound to restored image and current witness; verify all objects/metadata; reconcile missing commits or remain locked; verify retention/revocation; externally audit restore; enable read/status only after reconciliation. Writes and receipt issuance require a separate gate.

Witness rollback/outage locks service. Witness DR needs its own independent monotonic anchor or namespace retirement/re-provisioning with preserved evidence.

Clones must not become concurrent writers. Use lease/fencing epoch anchored in witness and enforced by storage/database. A stale leader must be unable to commit after lease loss. If fencing/linearizability cannot be proven, use one writer or block. Split-brain/quorum loss locks namespace.

## 6. Object immutability and retention

Require unique immutable object IDs; no in-place overwrite; exact length/hash manifest; storage-enforced retention lock; retention generation bound into commit/receipt; denial of delete/shortening during retention/legal hold; append-only legal hold/extension; corrections as new linked objects/receipts; periodic integrity scrub; documented replication/failure domains.

Expiry is not automatic deletion authority. Check policy, legal/incident holds and audit retention. No administrator may unilaterally shorten an acknowledged retention period.


## 7. Receipt signing and trust lifecycle

Signing is separate from object storage and API administration:
- private key only in approved HSM/non-exportable provider after platform and recovery behavior are proven;
- signing API accepts only service-verified COMMITTED facts, never arbitrary caller fields;
- signed data binds schema/profile, namespace/generation/sequence, package and manifest digests, retention generation and audit checkpoint;
- key ID and trust-bundle generation are protected;
- trust bundle version is anti-rollback anchored independently;
- rotation has bounded overlap and explicit activation/revocation sequence;
- compromise revokes key and locks affected namespace pending impact review;
- verifier rejects stale bundle, unknown/revoked key, rollback or unavailable revocation state;
- key restore cannot resurrect revoked keys.

Signature algorithm/encoding remain unresolved. Hashes, TLS, HSM branding or non-exportability do not prove monotonic service state.

## 8. Audit integrity

Audit capture begin/upload/verify/commit/status/receipt; retention/legal hold/expiry/delete denial; key/trust changes; start/stop/failover/restore/leader election; witness operations/reconciliation; integrity scrub/corruption; admin access/denials.

Each event binds namespace/generation, monotonic sequence, previous checkpoint digest, event digest, actor, operation ID and trusted-time metadata. Use append-only storage and periodic external checkpoints. Hash chaining alone is insufficient if the entire chain can be rewritten. Checkpoints live in independent witness/control plane.

If required audit checkpoint cannot be committed, do not issue receipt. Never silently skip events during outage.

## 9. Identity and operations

Separate roles: uploader, verifier, evidence custodian, retention admin, object-store admin, signing-key admin, witness admin, audit custodian, recovery authority. No principal should both weaken retention and erase audit, or administer data and reset monotonic anchor.

Use short-lived workload credentials, audience/namespace binding, rotation, revocation and least privilege. Never put secrets in command lines, environment, source, journal, evidence objects or logs. Emergency access must be time-bounded, independently approved, externally audited and unable to lower sequence.

## 10. Failure states

- SERVICE_READY: witness, metadata, objects, retention, trust and audit agree.
- COMMIT_PENDING / COMMIT_UNKNOWN: no receipt; resolve exact operation, no new ID.
- RESTORE_ROLLBACK_SUSPECTED / UNWITNESSED_STATE: writes and receipts disabled.
- FORK_OR_TAMPER: namespace locked, incident raised.
- WITNESS_UNAVAILABLE / RETENTION_UNVERIFIED / AUDIT_CHECKPOINT_MISSING: no positive status or receipt.
- TRUST_ROLLBACK_OR_KEY_REVOKED: issuance/use blocked.
- RECOVERY_LOCKED: approved reconciliation only.
- RETIRED: no new commits.

No timeout, restart or local cache can transition a blocked state to SERVICE_READY without fresh authenticated reconciliation.

## 11. Acceptance matrix

1. Crash after every object write/flush, PREPARED, witness advance, COMMITTED, retention, audit and signing boundary.
2. Lost responses resolve with same operation ID/digest; no duplicate sequence/receipt.
3. Restore every older DB backup with current witness; detect rollback and lock.
4. Restore old object index with current metadata and vice versa; mismatch blocks.
5. Witness stale/unavailable/conflicting/rolled back blocks.
6. Behind/ahead/equal-with-different-digest states classify exactly.
7. Two leaders, stale leader, fencing replay, split-brain and quorum loss cannot both commit.
8. Concurrent CAS yields one commit; retries idempotent; same ID/different digest conflicts.
9. Never issue receipt before object durability, retention, witness and required audit checkpoint.
10. Signature mutation, wrong/revoked key, trust rollback and stale revocation reject.
11. Storage denies overwrite/delete and policy shortening during retention.
12. Host restore/clone cannot alter service state; admin cannot rewrite external witness/audit.
13. Backup cannot resurrect revoked key, expired receipt, deleted namespace or shortened retention.
14. Scrub detects bit rot, truncation, substitution and missing replicas.
15. Time rollback/outage and audit gaps block expiry-sensitive/receipt operations.
16. Namespace clone/re-provision and generation mismatch detected.
17. DR runbook tested with independent operators and evidence preservation.
18. Every fail-closed path has no false COMMITTED and no recovery authorization.
19. Independent review validates durability, consensus/fencing, key custody, trust monotonicity and rollback domains.

## 12. Freeze gates

No implementation until approved:
1. Hosting/provider and independent admin/backup/rollback domains.
2. Witness, trust model, linearizability, CAS/idempotency, DR and anti-rollback anchor.
3. Exact commit ordering and crash-recovery matrix across object store, DB, witness, retention and audit.
4. Object lock semantics and documented durability/replication.
5. DB consistency, leader election, fencing, split-brain and restore proof.
6. Receipt signature profile, key custody, trust bundle, revocation and rotation.
7. Trusted time and sequence semantics.
8. Append-only audit and independent checkpoints.
9. Workload identity, roles, credentials and incident response.
10. Privacy, retention/legal hold, access and deletion policy.
11. Independent security review and fault-injection acceptance.

## 13. Explicit non-authorization

Design only. Creates no cloud account, endpoint, DNS, bucket, database, key, certificate, credential, HSM object, witness namespace, network rule, service, persistent file, Windows adapter, ACL, mutex, C source/header, test harness or Production integration. No journal read/write or recovery action is performed/authorized.

Fail-closed selector remains unchanged. Recovery classifier remains design-only. Production: NO-GO.

