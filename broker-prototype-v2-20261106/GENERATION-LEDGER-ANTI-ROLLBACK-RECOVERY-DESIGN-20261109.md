# Generation Ledger Anti-Rollback Threat Model and Recovery Design

Date: 2026-11-09
Status: design only; no Windows storage adapter or Production integration
Decision: the local two-slot ledger is a crash-recovery journal candidate, NOT an anti-rollback authority.

## 1. Executive decision

The current core can reject malformed records and choose the higher of two valid epochs in the current storage image. It cannot distinguish the current image from an older, internally valid copy of the entire ledger. Restoring both slots from a valid old backup therefore silently restores an old epoch unless an independent authority detects it.

Production MUST select and validate an independent rollback anchor before this ledger can authorize session generations. Until then, generation-ledger-backed Supervisor startup is NO-GO. Never silently initialize empty storage, reset to epoch 1, accept a restored older image, or issue a generation after uncertain persistence.

Recommended direction: retain the two-slot file as a local journal; protect it with a dedicated service identity and restrictive DACL; add an independently administered monotonic witness outside the host backup/VM rollback domain; and require explicit namespace rotation/re-provisioning after restore or clone. A remote witness is the preferred candidate. A TPM/hardware option remains only a candidate until the exact primitive, APIs, supported hardware, reset semantics, vTPM behavior, and recovery properties are proven. This document selects or provisions neither.

## 2. Assets, goals, and non-goals

Assets: uniqueness of each 16-byte generation ID; monotonicity of its 64-bit epoch prefix within a namespace; integrity of Supervisor session admission; and safe availability during recoverable faults.

Goals: never knowingly reissue an epoch in the same namespace; never treat a valid older image as current without independent evidence; fail closed on ambiguous persistence/witness state; preserve forensic evidence; and require explicit, auditable recovery.

Non-goals: the ledger is not an authentication secret. CRC-32 plus complement detects accidental corruption, not malicious modification (an attacker can recompute it). The eight-byte CNG suffix is not a monotonicity mechanism. This design does not protect against a fully compromised kernel or administrator.

## 3. Threat actors and trust boundaries

Threats include process/OS crash, power loss, storage/controller failure, unprivileged file modification, local administrator/SYSTEM, backup restore, VM snapshot/revert, disk clone, service SID/DACL drift, adapter bugs, incorrect flush semantics, concurrent writers, and CNG failure.

The pure-C core trusts its storage callbacks; no real adapter or durability contract exists yet. A local NTFS file and its ACL remain within the machine's administrative and backup/restore domain. DPAPI alone is not monotonic: an older valid protected blob can be restored. CNG randomness cannot reveal VM rollback. A remote witness is independent only if its credentials, state, administration, and backup process are outside the host rollback domain. Do not assume TPM key sealing is a monotonic counter.

## 4. Failure taxonomy

### Crash/power loss
The inactive-slot protocol intends to preserve the prior valid slot while writing the next. This depends on actual adapter atomicity, flush semantics, filesystem metadata behavior, storage caches, and device power-loss behavior. Successful API return alone is not proof of end-to-end durability.

### Torn/corrupt record
Malformed records should fail validation. CRC detects accidental corruption but does not authenticate content.

### Whole-ledger rollback
Restoring both slots from an older valid image yields valid magic/version/CRC and an older epoch. The current loader can accept it. No local comparison detects this if all local state is restored consistently.

### Tamper/clone
A privileged writer can forge a valid record and recompute CRC. DACLs constrain ordinary users but not administrators/offline disk edits. A clone duplicates the ledger and namespace; fresh random suffixes do not prevent epoch reuse after rollback.

## 5. Mechanism decision matrix

| Mechanism | Crash/torn-write | Ordinary user tamper | Admin/whole-image rollback | VM snapshot rollback | Decision |
|---|---|---|---|---|---|
| Two-slot local file + CRC | Potentially, after tests | No, absent ACL | No | No | Journal only |
| Service identity + NTFS DACL | No durability itself | Yes, within ACL boundary | No | No | Required hardening, not anchor |
| DPAPI-protected file | No monotonicity | Scope-dependent protection | No; old blob restorable | No | Optional layer only |
| CNG random suffix | N/A | N/A | No | No | Freshness entropy only |
| TPM/hardware state | Primitive-dependent | Potentially | Only if proven monotonic | Platform-dependent | Unaccepted candidate |
| Independent remote monotonic witness | Protocol-dependent | Yes, if authenticated | Yes, if independently administered | Yes, if not reverted with VM | Preferred candidate |
| Restore prohibition + re-provision | No | Process-dependent | Only if enforced | Only if enforced | Required operational control |

## 6. Required architecture contract

### Local journal adapter
- Dedicated service-owned directory, not a user workspace; dedicated virtual service identity and service SID; runtime effective-token verification.
- Explicit verified DACL for directory and file, including inheritance, owner, SYSTEM/Administrators and recovery access.
- Restrictive file sharing and explicit reparse policy. Bind validation to the opened handle; do not validate a path and later reopen it independently.
- Define fixed offsets, length, alignment, preallocation, write order, FlushFileBuffers, metadata durability, close and readback semantics on every supported storage stack.
- Cross-process singleton/writer lock; a process-local mutex is insufficient if multiple Supervisors can start.
- A backup-restored local record must never be the sole rollback authority.

### Independent monotonic witness
The authenticated witness needs operations conceptually equivalent to READ(namespace)->sequence/identity and ADVANCE(namespace, expected, next, operation_id)->committed sequence, plus idempotent resolution after a lost response. It must prevent concurrent advances, replayed responses, uncommitted sequence claims, blind retry after ambiguous timeout, and rollback of the witness with the host. Its credentials and administration must be independent. Define outage, rotation, revocation, disaster recovery, audit, and retention.

For TPM evaluation, document exact API/primitive, hardware matrix, provisioning, firmware/OS dependencies, reset/clear, vTPM, migration, and motherboard-replacement recovery. A sealed secret is not proof of monotonic state.

### Candidate issue transaction (not yet approved)
1. Authenticate and read witness namespace/sequence.
2. Reconcile local journal; reject local epoch ahead of or inconsistent with witness.
3. Request idempotent witness advance N -> N+1.
4. If response is lost, query the same operation ID; never blindly advance again.
5. Persist N+1 locally, flush, read back, and verify.
6. Only after both authorities agree, generate CNG suffix and publish the generation.
7. If local persistence fails after witness advance, burn the epoch; restart reconciles to witness.
8. If witness is unavailable/uncertain, issue no generation.

This is a design sketch requiring state-machine review and fault injection. Local storage must never overrule the witness.

## 7. Runtime states and fail-closed transitions

- UNPROVISIONED: no namespace/witness binding; only explicit authorized provisioning may leave this state.
- READY: local journal and independent witness agree.
- DEGRADED: I/O, flush, readback, CNG, or close outcome is uncertain; issue no generation and retain evidence/handles.
- ROLLBACK_SUSPECTED: local epoch is behind witness, namespace differs, witness sequence is unexpected, or restore evidence exists.
- RECOVERY_REQUIRED: state cannot be reconciled or witness is unavailable beyond policy.
- RETIRED: namespace permanently disabled after identity rotation/decommission.

Forbidden: DEGRADED -> READY by rereading the same uncertain file; ROLLBACK_SUSPECTED -> READY by copying a backup; automatic UNPROVISIONED -> READY; automatic reset to epoch 1; or issuing another generation before resolving an ambiguous witness operation ID. One valid and one empty slot is only a journal-recovery candidate; the witness must confirm it. Two empty slots, malformed nonempty records, conflicting records, read errors, or witness disagreement require explicit recovery.

## 8. Recovery procedure

1. Stop admitting Worker sessions. Do not delete, truncate, replace, or reinitialize the ledger.
2. Preserve slot bytes, file metadata, ACL/owner, service identity details, and timestamps. Protect evidence; do not log secrets.
3. Classify crash, power loss, backup restore, VM revert, clone, disk replacement, or suspected tampering.
4. Authenticate to the independent witness and query namespace, committed sequence, and pending operation IDs.
5. Compare witness identity/sequence with local journal and deployment inventory.
6. If witness confirms the same namespace at a higher sequence, classify rollback suspected. Do not simply overwrite and resume; use approved reconciliation with an audit record.
7. If witness is unavailable or ambiguous, remain RECOVERY_REQUIRED. No offline issuance unless a separately approved bounded-offline protocol exists.
8. If machine/TPM/vTPM/service identity/namespace binding changed, retire the old namespace and explicitly re-provision. Never reuse an untrusted restored ledger.
9. Run storage, ACL, witness, process-identity, and replay tests; obtain operator approval before re-enabling admission.
10. Without an independent witness, whole-ledger rollback cannot be reliably distinguished; require fresh namespace and credentials rather than claiming local recovery is safe.

## 9. Backup, restore, clone, and operations policy

- Backups may preserve the local journal for forensics, but must not restore the monotonic authority.
- Restore runbooks stop the service and require witness reconciliation before admission.
- VM snapshot restore, disk clone, golden-image capture, and migration trigger restore/re-provision.
- Never embed live namespace credentials or witness client secrets in reusable golden images.
- A clone must receive a distinct namespace and credentials; it must not start with the source namespace.
- Witness outage means fail closed. An offline grace window weakens guarantees and requires a separate replay-safe lease design.
- Audit every provision, advance, restore, recovery, retirement, credential rotation, and denied rollback.

## 10. Acceptance test matrix

| Scenario | Injection | Required result |
|---|---|---|
| Power loss before write | Stop before inactive-slot write | Previous committed epoch remains |
| Partial write | Tear inactive record | Reject corrupt slot; witness decides recovery |
| Write/flush/readback error | Inject each failure | DEGRADED; no generation |
| Readback mismatch | Alter returned bytes | DEGRADED; no publication |
| Durable write, response lost | Restart with new slot present | Epoch may be burned; never reused |
| One valid + one empty slot | Interrupted update | Witness confirmation before READY |
| Both slots empty | Fresh/erased file | UNPROVISIONED/RECOVERY_REQUIRED; no auto-init |
| Malformed nonempty slot | Corrupt bytes | Fail closed |
| Two valid different epochs | Valid old/new pair | Parse local maximum, then compare witness |
| Restore both old valid slots | Restore snapshot | Witness detects local sequence behind |
| Forge record and recompute CRC | Malicious modification | CRC not trusted; ACL/witness constrain or detect |
| Restore old DPAPI blob | Replace with prior blob | Not accepted as fresh monotonic state |
| VM snapshot revert | Revert disk/process image | Independent witness reports newer state |
| Clone machine image | Start duplicate image | Block until new namespace provisioned |
| Witness response lost | Drop response after advance | Resolve same operation ID; no blind retry |
| Concurrent Supervisors | Race issuers | Serialized advance; no duplicate epoch |
| Witness unavailable | Deny network | No generation; recovery state |
| CNG failure after commit | Inject RNG failure | Epoch burned; output all-zero |
| Service SID/DACL drift | Alter test identity/ACL | Startup denied; no fallback identity |
| Sequence overflow | Set UINT64_MAX | Fail closed; retire/re-provision |
| Recovery credential revoked | Revoke test credential | No bypass; operator recovery required |

Use real process restarts and disposable VMs/storage images for rollback tests. Callback tests cannot establish power-loss durability, ACL enforcement, hardware monotonicity, or snapshot resistance.

## 11. Implementation status and gaps

Implemented in isolated pure-C core: two 32-byte RYGL/version-2 slots with equal nonzero sequence/epoch and CRC/complement; explicit first-install on two empty slots; load rejecting malformed nonempty/both-empty states and selecting the larger valid epoch; inactive-slot write/flush/readback verification; generation helper committing epoch before requesting eight CNG suffix bytes; CNG adapter using BCryptGenRandom with system-preferred RNG. RNG failure zeros output and burns the epoch.

Not implemented or accepted: real Windows file adapter and FlushFileBuffers/metadata durability; protected directory/file DACL; dedicated service identity/SID and token verification; handle-bound path/reparse validation; cross-process writer lock/singleton enforcement; independent remote or hardware witness; namespace provisioning and credential lifecycle; restore/recovery tooling and audit; power-loss, storage-cache, backup-restore, VM-snapshot, clone, admin-tamper and disaster-recovery acceptance; end-to-end Supervisor integration.

## 12. Go/no-go gates

Production remains NO-GO until: (1) threat model and rollback domain approved; (2) independent monotonic anchor selected and independence demonstrated; (3) advance transaction and idempotent timeout reconciliation reviewed; (4) Windows adapter, service identity, ACL, handle validation and cross-process locking implemented; (5) fault injection covers every write/flush/readback/witness boundary; (6) real power-loss and restore/snapshot/clone tests pass on supported deployments; (7) operator recovery/re-provision runbooks exercised; and (8) full Supervisor/Worker IPC, lifecycle, replay and Broker acceptance completes.

This document authorizes design and isolated testing only. It does not authorize service installation, persistent file creation, ACL modification, TPM provisioning, remote witness setup, or Production wiring.
