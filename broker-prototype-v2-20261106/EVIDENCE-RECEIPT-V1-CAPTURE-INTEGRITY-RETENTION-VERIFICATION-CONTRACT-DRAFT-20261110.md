# Evidence Receipt v1 — Capture, Integrity, Retention and Verification Contract (Draft)

Date: 2026-11-10
Status: design-only; not frozen; no implementation authorization
Production: NO-GO

## 1. Purpose and trust boundaries

Defines capture, independent retention, receipt issuance, and later verification of journal evidence before recovery. A receipt proves only that the evidence service acknowledged specified objects under its stated policy. It does not prove the capture process observed the correct live file, that the bytes are latest, or that Witness/Ledger agree.

Distinct trust domains: capture process/effective Windows identity; same-handle observer; transport client; independent evidence service/object store; receipt signing authority/trust bundle; independent verifier; Witness and local epoch Ledger.

Broker host compromise, backup restore, VM snapshot revert, or clone must not permit deletion, replacement, or rollback of previously acknowledged evidence.

## 2. Mandatory invariants

1. Capture both exact 256-byte slots and complete 512-byte image before any journal mutation.
2. Preserve raw malformed bytes exactly. Never normalize, decode/re-encode, repair, or omit them.
3. Bind evidence to incident, namespace, exact volume/file identity, file length, slot indices, capture session, and provenance.
4. Hashes identify bytes; they do not authenticate origin, time, or retention.
5. Issue receipt only after durable commit of all referenced objects and metadata.
6. Objects, receipt keys, trust bundle, audit, and retention controls are outside host rollback domain.
7. Acknowledged evidence cannot be overwritten/deleted during required retention. Corrections create new linked objects/receipts.
8. Partial upload, uncertain commit, unavailable status, or invalid receipt blocks repair.
9. Capture/verification never authorizes journal write, service start, or generation issuance.

## 3. Capture package v1

Required immutable objects:
- slot0.raw: exactly 256 bytes from offset 0;
- slot1.raw: exactly 256 bytes from offset 256;
- image.raw: exact slot0 || slot1 concatenation, exactly 512 bytes;
- manifest: canonical, versioned metadata.

Verifier checks lengths and byte-for-byte concatenation. Missing raw objects cannot be reconstructed from hashes or decoded structures.

Manifest fields (integer key assignment deferred to schema freeze):
- schema/profile version; incident_id, namespace_id;
- unique nonzero 16-byte capture_id and 32-byte CSPRNG capture_nonce;
- capture start/end times with clock source and quality;
- source host/machine/boot identity and capture build digest;
- process identity, effective token user SID, service SID, logon/session and impersonation state;
- volume identity and volume-scoped file ID from the same opened handle;
- file size/type/filesystem, reparse status, owner SID, canonical security descriptor/DACL digest;
- handle open/share/access flags; exact offsets, requested/actual lengths, read completion and same-handle continuity;
- hashes/lengths for each raw object and full image; capture policy version.

Do not embed credentials, tokens, private keys, full access tokens, unrelated command lines, or unrelated user files.

### Same-handle requirements

The future adapter must open only the configured journal using approved root/handle-relative semantics; reject reparse and non-regular objects; obtain file identity/type/owner/DACL/size from the same handle; read both slots and image through that handle; prove exact-length reads; detect concurrent mutation under approved exclusion/fencing; and recheck identity/security metadata before completion.

Path is diagnostic only. Reopening by path between identity check and read is prohibited. If same-handle identity cannot be proven, provenance is UNVERIFIED and recovery blocks. This draft does not claim NTFS or remote-filesystem behavior has been tested.

## 4. Canonical manifest and package digest

Candidate: deterministic CBOR, not frozen. Require definite lengths, shortest encodings, deterministic key ordering, no duplicate/unknown keys, no tags/floats/indefinite values, fixed byte strings for IDs/hashes/SIDs, strict bounds, and decode/re-encode byte equality.

Each object has exact length and SHA-256. Package digest uses a domain-separated, canonical ordered binary tuple containing protocol/version, capture/incident/namespace IDs, and each object's identifier, length and hash, including manifest. Freeze exact preimage and test vectors; never hash ambiguous string concatenations. SHA-256 is not origin authentication.

## 5. Independent service transaction

Service and administration must be outside Broker host VM/snapshot/backup rollback domain. Durability, replication, immutable retention, audit and disaster recovery must be documented. Client workload authentication is distinct from receipt signing.

Conceptual idempotent protocol:
1. BEGIN_CAPTURE(capture_id, package_digest, incident, namespace, object_manifest)
2. PUT_OBJECT(capture_id, object_id, length, digest, bytes)
3. VERIFY_CAPTURE(capture_id): server validates lengths, hashes and cross-object consistency
4. COMMIT_CAPTURE(capture_id, package_digest): atomically commits immutable references and metadata
5. STATUS_CAPTURE(capture_id, package_digest): resolves ambiguous/lost responses

Same capture_id is idempotent only for the exact package digest and manifest; different digest is CONFLICT. Partial upload never yields receipt. Lost commit response requires STATUS, not a new capture ID. UNKNOWN status blocks.


## 6. Receipt issuance and content

Only after documented durable commit and retention activation, issue a signed receipt containing:
- schema/profile, evidence_service_id, receipt_id and capture_id;
- package digest, incident_id and namespace_id;
- immutable object IDs, exact lengths and hashes;
- manifest and full-image digests;
- service commit sequence/transaction ID and service-controlled committed_at;
- retention policy, retention-until, immutability mode and legal-hold status;
- signing key ID and trust-bundle version.

Receipt signature profile and key custody are unresolved. A receipt must not claim WORM/immutability unless the service verified the actual retention lock. Best-effort retention is insufficient for recovery.

## 7. Retention and rollback resistance

Require append-only object identities; retention lock; independent immutable audit of upload/commit/read/policy/key/admin actions; separation of duties; documented replication/failure domains; anti-rollback for metadata, object references, trust bundle and audit sequence; authenticated retrieval and hash verification on every read; namespace isolation; independent verification material.

Restoring the evidence service from an older database snapshot must be detected through an independent monotonic anchor and block receipt verification until reconciled. No vendor, cloud, HSM, endpoint or retention mode is selected here.

## 8. Independent verification

Verifier must:
1. Bound and strictly parse receipt; reject unknown schema, duplicate fields, noncanonical encoding and trailing bytes.
2. Resolve key from independently provisioned anti-rollback trust bundle; verify signature, key validity and revocation under one fixed profile.
3. Query live STATUS_RECEIPT over authenticated channel; require COMMITTED and not revoked/corrupt/expired.
4. Retrieve every object by immutable ID; verify exact length and SHA-256.
5. Reconstruct image from slot objects and compare byte-for-byte.
6. Recompute package digest and all manifest/receipt bindings.
7. Match incident, namespace, capture ID, file identity and policy to recovery case.
8. Verify retention remains active through recovery/audit period.
9. Independently assess capture provenance; service signature alone does not prove source-handle correctness.
10. Preserve verification result and service status proof in evidence chain.

Unavailable, stale, ambiguous, rolled-back or inconsistent status returns BLOCK_EVIDENCE_UNVERIFIED. Cached receipt alone is insufficient for destructive recovery.

## 9. Lifecycle, privacy and access

States: CAPTURE_OPEN, OBJECTS_UPLOADING, COMMIT_UNKNOWN, COMMITTED_IMMUTABLE, RECEIPT_ISSUED, RETENTION_HOLD, RETENTION_EXPIRED, REVOKED_OR_CORRUPT, RECOVERY_REQUIRED.

Only RECEIPT_ISSUED with live COMMITTED_IMMUTABLE status is usable. Corrections use new capture IDs; old evidence remains. Revocation/corruption blocks use but does not erase evidence. Expiry never silently validates old evidence. Legal hold/extension are audited. Recovery binds exact receipt digest, not filename/path.

Use least privilege and separate recovery operator, evidence custodian, service administrator and signing-key administrator roles. Log access; encrypt in transit/at rest; exclude secrets and unrelated files; define lawful retention/deletion. Encryption does not replace immutability, signatures or access control.

## 10. Error taxonomy

BLOCK_CAPTURE_IDENTITY_UNVERIFIED
BLOCK_HANDLE_CONTINUITY_UNPROVEN
BLOCK_SHORT_OR_INCONSISTENT_READ
BLOCK_OBJECT_LENGTH_MISMATCH
BLOCK_OBJECT_HASH_MISMATCH
BLOCK_MANIFEST_NONCANONICAL
BLOCK_PACKAGE_DIGEST_MISMATCH
BLOCK_UPLOAD_INCOMPLETE
BLOCK_COMMIT_UNKNOWN
BLOCK_RECEIPT_SIGNATURE_INVALID
BLOCK_RECEIPT_KEY_UNKNOWN_OR_REVOKED
BLOCK_TRUST_BUNDLE_ROLLBACK
BLOCK_RECEIPT_NOT_COMMITTED
BLOCK_RECEIPT_REVOKED_OR_CORRUPT
BLOCK_RETENTION_NOT_GUARANTEED
BLOCK_EVIDENCE_SERVICE_UNAVAILABLE
BLOCK_EVIDENCE_SERVICE_ROLLBACK
BLOCK_NAMESPACE_OR_INCIDENT_MISMATCH
BLOCK_FILE_IDENTITY_MISMATCH
BLOCK_CAPTURE_PROVENANCE_UNVERIFIED

No error defaults to acceptance.

## 11. Acceptance tests before freeze

1. Exact 256/256/512 lengths and byte concatenation; short/extra reads, offset/slot swaps, truncation, mutation and handle replacement block.
2. Reparse/device/directory/network object, file ID/owner/DACL drift and identity mismatch block.
3. Same-handle and exclusion/fencing races under concurrent writers.
4. Malformed bytes preserved exactly.
5. Canonical vectors; reject duplicate/unknown keys, non-shortest/indefinite encodings, trailing bytes and oversized metadata.
6. Digest vectors; mutate each object, length, ID and manifest field.
7. Exact retry idempotent; same capture ID/different digest conflicts; partial upload never issues receipt.
8. Lost BEGIN/PUT/COMMIT responses resolve by STATUS; unknown blocks.
9. Crash at each upload/verify/commit/receipt boundary; receipt only after durable commit and retention activation.
10. Valid/invalid/revoked signatures, wrong key, trust-bundle rollback, object mutation/deletion/substitution and metadata rollback.
11. Host restore/VM revert/clone cannot alter evidence; service old-snapshot restore detected.
12. Retention lock, legal hold, expiry, unauthorized deletion, audit gaps and sequence rollback tested.
13. Concurrent commit/failover preserves one immutable receipt.
14. Authorization binds exact receipt; cross-incident/namespace/file substitution rejected.
15. Every failure path proves no journal write, service start, generation or epoch advance.
16. Independent review of storage, key custody, time, receipt and capture provenance.

## 12. Freeze gates

Do not implement until approved:
1. Canonical encoding, schema keys, digest preimage and vectors.
2. Evidence service deployment and independent admin/backup/rollback domains.
3. Retention/immutability/legal hold/expiry/deletion and audit policy.
4. Receipt signature profile, key custody, trust bundle, rotation/revocation and anti-rollback.
5. Workload identity and transport authentication.
6. Trusted time and service commit-sequence semantics.
7. Windows same-handle/file-identity/exclusion adapter and tested filesystem scope.
8. Privacy, access roles, encryption and retention compliance.
9. Independent verifier and live STATUS semantics.
10. Integration with Recovery Authorization and operator separation of duties.
11. Independent security/operational review.

## 13. Explicit non-authorization

No endpoint, cloud account, object store, bucket, key, certificate, credential, trust bundle, network rule, persistent evidence file, Windows adapter, mutex, ACL, service identity, C source/header, tests, or Production integration is created or authorized. No journal read/write or recovery action is performed/authorized by this design task. Normal selector remains unchanged; recovery classifier remains design-only.

Production status: NO-GO.

