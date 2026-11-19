# Submitter Durable Ledger and Restart-Recovery Contract — Design Gate
Date: 2026-11-17
Status: design only; implementation NOT authorized by this slice.

## 1. Purpose and boundary
This document defines the minimum contract required before replacing the current in-memory request diagnostics with a crash-recoverable journal. It does not create or open a journal, change runtime behavior, add a recovery API, or authorize replay of external effects.

Working Copy: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`.
Source Project and Production are untouched.

The current Submitter request ID is monotonic only within one in-memory Submitter instance. The effect fields are stored in caller-owned request memory. Neither survives process termination. The current Issue and Publish callbacks have no durable recipient receipt protocol, so a local journal alone cannot prove exactly-once external effects.

## 2. Source facts and hard constraints
Inspection of `dedicated-io-submitter-v2.h/.c`, `DEDICATED-IO-SUBMITTER-DESIGN-20261108.md`, and `validation/CALLBACK-DISPOSITION-AND-RETAINED-INTEGRITY-CONTRACT-20261115.md` establishes:
- Issue callback returns ACCEPTED, NOT_ACCEPTED, or UNCERTAIN.
- Publish callback returns only int; its durable effect and idempotency semantics are unspecified.
- not_issued callback returns void.
- Callbacks receive the request object, but current request identity is instance-local and is not a durable idempotency key.
- Existing callback API has no recipient-side durable deduplication or receipt.
- A callback or OS effect may occur before the process records its result. A crash in that interval is inherently ambiguous.
- The real broker I/O operation uses process-owned HANDLE/OVERLAPPED/event/buffer resources. A persisted record cannot make those kernel objects survive process death or make a previous pending I/O safe to reissue.

Therefore:
1. Persistence can preserve evidence and prevent unsafe automatic replay; it cannot by itself resolve an ambiguous external effect.
2. Do not call the journal exactly-once, transactional outbox, or crash-safe replay until the recipient participates in the protocol.
3. Unknown effects remain UNKNOWN and fail closed after restart.
4. Do not restore a Submitter to RUNNING merely because the journal parses successfully.

## 3. Identity contract
A durable operation identity must not reuse the current instance-local counter by itself.

Required identity shape:
- durable namespace / broker-instance identifier, provisioned and retained outside the Submitter process;
- monotonically increasing 64-bit sequence within that namespace;
- operation kind and schema version;
- optional caller/business idempotency key where the caller needs semantic deduplication across broker recreation.

The pair (namespace, sequence) is the journal identity. Sequence zero is invalid. Sequence wrap/exhaustion is terminal for new admissions; never wrap or reuse. The namespace must be stable across ordinary process restart and must not be silently regenerated when the journal is missing or corrupt.

Before an ID is exposed to any external callback:
1. reserve the sequence under the single-writer journal lock;
2. append a durable ID_RESERVE record;
3. force the record to the selected stable-storage boundary;
4. only then publish the ID to the caller/callback.

A gap in IDs is acceptable. Reuse is not.

A journal reset, restore-from-backup, clone, or copy to another host must be treated as a namespace/fencing event. Two processes must never append to the same namespace concurrently without a proven exclusive-writer protocol.

## 4. Journal format and atomicity
Candidate v1 is an append-only, single-writer write-ahead log (WAL). No in-place record updates.

Each record must contain:
- fixed magic and format version;
- record type and bounded total length;
- monotonically increasing journal sequence;
- durable operation identity (namespace + request sequence);
- operation kind and transition;
- payload digest and bounded metadata (never raw sensitive payload by default);
- previous-record digest or chain value;
- record checksum covering header and body;
- explicit reserved fields required to be zero.

Record types:
1. JOURNAL_HEADER / NAMESPACE_BIND
2. ID_RESERVE
3. REQUEST_ACCEPTED (immutable request metadata and digest)
4. EFFECT_BEGIN (effect kind, attempt number, idempotency key)
5. EFFECT_RESULT (COMMITTED / PROVEN_NOT_COMMITTED / UNKNOWN plus evidence reference)
6. OPERATION_TERMINAL
7. CHECKPOINT_COMMIT (only after a separately specified snapshot protocol)

The only legal effect transitions are:
- NOT_ATTEMPTED -> IN_PROGRESS, after durable EFFECT_BEGIN;
- IN_PROGRESS -> COMMITTED, only after callback/recipient evidence satisfies the selected contract;
- IN_PROGRESS -> PROVEN_NOT_COMMITTED, only with affirmative evidence that the effect did not occur;
- IN_PROGRESS -> UNKNOWN, for crash, timeout, lost response, malformed result, or any ambiguous boundary.

UNKNOWN is not an automatic retry state. It may move only through an explicit reconciliation record backed by typed evidence and an approved policy. Absence of a receipt is not proof that an effect did not occur.

A record is accepted only if its complete length, version, sequence, identity, checksum, chain value, and transition validate. Never trust a partially written tail. Recovery may ignore only a provably incomplete final record after the last valid committed boundary; any interior corruption, sequence gap, duplicate identity, invalid transition, unexpected namespace, or chain mismatch quarantines the journal and blocks service start. Whether a torn final record can be truncated is a separate approved repair operation; default recovery is read-only.

## 5. Durability boundary
The implementation candidate on Windows must:
- use a configured local storage root with explicit ACL/owner policy;
- open the journal with a single-writer exclusion mechanism held for the full writer lifetime;
- reject reparse points and unexpected file types;
- use exact-length writes and check every API result;
- use an explicitly selected durability mode (candidate: append, then FlushFileBuffers before external effect);
- document filesystem, volume, controller-cache, virtual-disk, and power-loss assumptions;
- handle disk-full, quota, access denied, short write, flush failure, close failure, and device removal as fail-closed conditions;
- avoid deleting, rotating, or truncating the active journal until a separate checkpoint/retention protocol is designed and tested.

FILE_FLAG_WRITE_THROUGH may be evaluated as an additional policy, but must not be described as a substitute for a documented FlushFileBuffers and storage-stack durability contract. No guarantee is made for network shares, cloud-synced folders, removable media, or filesystems outside the explicitly tested matrix.

## 6. External effect protocol
For every effect:
1. durably append EFFECT_BEGIN;
2. invoke the external operation with the durable operation identity/idempotency key;
3. obtain a structured result;
4. validate the result and any recipient receipt;
5. durably append EFFECT_RESULT;
6. only then expose the corresponding committed state to dependent lifecycle logic.

A Publish API returning int is insufficient. It must be versioned to return a structured disposition and evidence/receipt, and accept the durable operation identity. The Issue adapter must define which native outcomes prove not accepted, accepted, or uncertain, and whether any effect can outlive process death.

If the recipient cannot durably deduplicate the identity and return a verifiable receipt, the supported guarantee is at-most-once attempt with UNKNOWN after an ambiguous crash boundary; eventual delivery and exactly-once effects are not claimed.

For overlapped pipe I/O, a restarted process must not recreate or replay the old OVERLAPPED operation from journal metadata. Any in-flight operation at process death is quarantined as UNKNOWN until the transport/peer protocol provides independent evidence and an explicit recovery policy. The journal does not resurrect kernel handles or prove that the peer did not process bytes.

## 7. Restart classification
Recovery is read-only and runs before listener/service admission.

| Last valid durable state | Restart classification | Automatic action |
|---|---|---|
| ID_RESERVED, no REQUEST_ACCEPTED | Unused identity gap | Preserve sequence; no replay |
| REQUEST_ACCEPTED, no EFFECT_BEGIN | Not attempted | May be considered for new scheduling only after separate caller/ownership validation |
| EFFECT_BEGIN, no EFFECT_RESULT | UNKNOWN | Quarantine; no replay |
| EFFECT_RESULT COMMITTED | Committed | Never reissue; continue only idempotent local bookkeeping |
| EFFECT_RESULT PROVEN_NOT_COMMITTED | Proven no effect | Retry only under explicit policy and stable identity |
| EFFECT_RESULT UNKNOWN | UNKNOWN | Quarantine; evidence-based reconciliation only |
| OPERATION_TERMINAL | Terminal | No replay |

No automatic restart of the Submitter is allowed while any request is UNKNOWN, journal corruption exists, the namespace is not bound, or the exclusive-writer fence is not held.

## 8. Evidence and reconciliation
A reconciliation record must include:
- exact durable operation identity and effect kind;
- evidence source and trust domain;
- evidence object identifier, digest, and acquisition time;
- verifier identity/version and verification result;
- conclusion: COMMITTED, PROVEN_NOT_COMMITTED, or UNRESOLVED;
- policy/rule identifier and operator/owner authorization where required;
- link to the prior journal sequence and chain digest.

Evidence must be independent of the local journal when the question is whether an external recipient performed the effect. Local absence, timeout, process exit, or missing response is never sufficient evidence for PROVEN_NOT_COMMITTED.

Reconciliation is a separate owner-only action. It must not directly release resources, free caller-owned request memory, decrement leases, or change lifecycle to STOPPED. It only records a reviewed effect conclusion; a separate lifecycle cursor must validate all resource and ownership predicates.

## 9. Checkpoint, retention, and recovery ownership
Do not implement compaction in v1. An append-only journal may grow without bound; this is a known blocker that requires a retention and capacity policy before production use.

Future checkpoint protocol must define:
- snapshot schema/version and complete state coverage;
- atomic publication of snapshot and checkpoint commit marker;
- durable ordering between snapshot flush and WAL truncation/rotation;
- recovery from every crash boundary;
- retained old journal generations and rollback policy;
- independent backup/restore and clone fencing;
- maximum size, disk-full behavior, retention, secure deletion and audit requirements.

One process owns the journal writer and recovery lock. The existing in-memory cleanup owner token is not a cross-process fencing token and cannot substitute for this lock.

## 10. Failure matrix required before implementation acceptance
Inject process termination or API failure:
- before/after ID_RESERVE write and flush;
- before/after REQUEST_ACCEPTED;
- before/after EFFECT_BEGIN;
- inside external effect before it returns;
- after external effect returns but before EFFECT_RESULT write;
- during partial EFFECT_RESULT write and flush;
- after EFFECT_RESULT flush but before local state publication;
- at every record boundary, checksum, sequence, chain and length corruption;
- disk full, short write, flush failure, access denied, reparse swap, lock contention, second writer, namespace mismatch;
- journal missing, truncated, restored from old backup, copied/cloned, and volume rollback;
- recovery crash during read-only scan and during any separately approved repair.

Each case must prove: no unsafe replay, no identity reuse, no false COMMITTED/PROVEN_NOT_COMMITTED classification, no lifecycle admission while quarantined, and no release of resources whose terminal state is unknown.

## 11. Implementation gates / unresolved product decisions
Do not implement the WAL until these are decided:
1. Is the durable namespace a broker installation identity, transport identity, or caller-provisioned identity?
2. Which local volume/path is approved, and what ACL/service identity owns it?
3. Which Windows/filesystem/storage configurations are supported?
4. Is the intended guarantee at-most-once attempt, at-least-once with recipient dedup, or transactional effect?
5. Which real recipients can accept and durably deduplicate operation IDs?
6. What is the structured Publish result and receipt schema?
7. What evidence can prove an overlapped pipe operation's external effect after process death?
8. What are journal capacity, retention, backup, restore, clone, and operator-reconciliation policies?
9. Is a missing/corrupt journal a permanent fail-closed condition or is there a separately authorized re-provisioning workflow?

Until these are approved, adding a generic file-backed ledger would create a false sense of crash recovery.

## 12. Decision
Proceed next with API-contract design for a versioned durable operation identity and structured Issue/Publish result/receipt interface, plus a read-only WAL parser/recovery model. Do not yet create a journal file, mutate the current callback ABI, replay UNKNOWN effects, or relax DEGRADED/Destroy preconditions.

Phase 1 remains NOT ACCEPTED. Production remains NO-GO.
