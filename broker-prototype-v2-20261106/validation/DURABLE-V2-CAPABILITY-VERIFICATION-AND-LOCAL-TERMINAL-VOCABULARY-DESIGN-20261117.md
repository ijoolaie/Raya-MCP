# DURABLE V2 CAPABILITY VERIFICATION AND LOCAL TERMINAL VOCABULARY DESIGN
Date: 2026-11-17
Scope: design-only follow-up in the Working Copy
Status: PROPOSAL — NOT IMPLEMENTED — NOT APPROVED FOR PRODUCTION

## 1. Executive conclusion

The v1 contract currently conflates provider-declared capability with an executable, verifiable guarantee. In particular, a configuration can pass validation with RECIPIENT_IDEMPOTENCY, RECIPIENT_RECEIPT, and RECIPIENT_STATUS_QUERY bits set and guarantee=RECIPIENT_COMMIT, while the callback ABI contains only Issue and Handoff callbacks. There is no recipient receipt/status verifier callback, no typed recipient evidence result, and no verifier identity/version or trust-policy binding.

Therefore:
- RECIPIENT_COMMIT must be treated as unsupported and rejected by any runtime profile until a separately versioned recipient-verifier contract exists and is actually invoked.
- The v1 config validator's OK result means only that declarations are internally consistent; it does not prove a provider implements those capabilities.
- The current code must not advertise, enable, or infer recipient-level commit from those bits.
- The current WAL scanner remains a structural, read-only scanner; this proposal does not change its format, writer, replay, or recovery behavior.

## 2. Exact v1 contract findings

Inspected:
- raya-durable-contract-v1.h
- raya-durable-contract-v1.c
- raya-durable-contract-v1-test.c

### 2.1 Declared recipient capability bits

The header declares:
- RAYA_DURABLE_CAP_RECIPIENT_IDEMPOTENCY = 0x10
- RAYA_DURABLE_CAP_RECIPIENT_RECEIPT = 0x20
- RAYA_DURABLE_CAP_RECIPIENT_STATUS_QUERY = 0x40

The config validator requires these three bits to be either all absent or all present. For RECIPIENT_COMMIT it requires the local capability set and all three recipient bits. The tests explicitly demonstrate that a config with all three recipient bits and RECIPIENT_COMMIT returns VALIDATION_OK.

### 2.2 Runtime callback mismatch

The callback table has only:
- issue: raya_durable_issue_callback_v1
- handoff: raya_durable_handoff_callback_v1

The callback validator checks that typed Issue and Handoff bits correspond to non-NULL function pointers. It does not require or validate any recipient callback because none exists. It also does not bind recipient claims to a provider identity, protocol version, receipt verifier, cryptographic trust policy, or runtime observation.

This is a concrete contract hole, not merely a missing test. The existing positive test for RECIPIENT_COMMIT proves only acceptance of a self-declared bitset.

### 2.3 Meaning of existing evidence

- ISSUE_OS_ACCEPTED is native OS acceptance of the I/O request, not application or recipient commit.
- HANDOFF_TRANSFERRED is transfer of ownership to a Worker, not recipient receipt.
- The callback result normalizers bind Issue/Handoff evidence to the operation ID, but do not establish a remote recipient's durable state.
- The v1 capability bits are declarations; they are not evidence.

## 3. Proposed v2 separation: declarations, interfaces, observations

Use three distinct layers. Do not let one substitute for another.

### Layer A — Declared provider profile
A provider may declare support for a named capability, but declaration is descriptive metadata only. It must include a stable provider ID, provider contract version, and a profile/version identifier. It cannot by itself satisfy a guarantee.

### Layer B — Required runtime interface
The runtime must validate the presence and version of the exact callbacks required by the selected profile. For recipient commit, a distinct recipient verifier callback is mandatory. Missing, incompatible, or untrusted verifier means initialization failure for that guarantee; there is no fallback to Issue/Handoff-only semantics.

### Layer C — Per-operation verified observation
A successful recipient terminal decision requires a verifier result for the exact operation identity and payload digest. Missing, malformed, stale, mismatched, unauthenticated, or ambiguous evidence normalizes to UNKNOWN and quarantines the operation.

## 4. Candidate recipient verifier contract (design only)

A future versioned recipient verifier interface should minimally carry:

### Request
- struct_size and schema_version
- operation_id (namespace, sequence, operation kind, payload digest)
- recipient_id in a stable, explicitly scoped namespace
- protocol_id and protocol_version
- request_nonce or challenge bound to this verification attempt
- expected operation/effect kind
- optional deadline metadata, without implying that timeout proves non-commit

### Result
- struct_size and schema_version
- exact operation_id_echo and payload_digest_echo
- exact recipient_id, protocol_id, and protocol_version echo
- disposition: COMMITTED, PROVEN_NOT_COMMITTED, UNKNOWN
- receipt identifier and receipt digest when COMMITTED
- evidence timestamp/sequence and freshness metadata
- verifier identity and verifier-policy version
- authentication result/reference bound to an explicitly configured trust policy
- reserved fields required to be zero

### Acceptance rules
- COMMITTED is accepted only when every identity and digest echo matches, the receipt is authenticated under the configured trust policy, freshness/nonce requirements pass, and the provider's verifier implementation is the one bound at initialization.
- PROVEN_NOT_COMMITTED is accepted only when the protocol can prove non-commit for the same operation identity and verification scope. A timeout, connection failure, missing receipt, or empty status is UNKNOWN, never PROVEN_NOT_COMMITTED.
- UNKNOWN includes malformed result, stale result, nonce mismatch, recipient mismatch, digest mismatch, unsupported protocol version, verifier failure, timeout, and contradictory evidence.
- A retry after PROVEN_NOT_COMMITTED still requires an explicit policy and durable attempt identity. It must not be inferred from a generic status query.
- A receipt digest alone is not authentication. The trust model (signature, MAC, mutually authenticated channel plus protocol guarantee, or another reviewed mechanism) must be explicit and independently validated.
- Idempotency capability must be scoped to recipient_id + protocol_id/version + operation identity; a boolean bit without scope is insufficient.

This is a minimum contract outline, not a claim that any recipient protocol currently implements it. Protocol-specific semantics, key management, revocation, replay protection, retention, and evidence persistence remain unresolved.

## 5. Local lifecycle terminal vocabulary

The existing in-process Adapter → Submitter/Worker → finalizer/release chain can support a narrowly scoped local lifecycle outcome. It does not establish restart-surviving durability or recipient commit.

Use distinct terminal scopes:

1. LOCAL_LIFECYCLE_TERMINAL_VOLATILE
   All required in-process ownership and cleanup stages reached a known terminal state during the current process lifetime. This is volatile evidence unless separately persisted and recovered.
2. TRANSPORT_TERMINAL
   A transport/OS/Worker operation reached a defined terminal state. This does not imply recipient commit.
3. RECIPIENT_COMMIT_TERMINAL
   The recipient verifier returned authenticated COMMITTED evidence under an approved protocol policy. Unsupported in v1.

### 5.1 Candidate local evidence chain

For an Issue-accepted path, the candidate evidence chain is:
1. Issue disposition is OS_ACCEPTED with exact operation identity.
2. Handoff disposition is TRANSFERRED with exact operation identity and nonzero Worker instance/token.
3. Worker observes the exact event/OVERLAPPED pair and obtains a determinate completion result; INCOMPLETE remains pending, and uncertain completion is quarantined.
4. Finalizer completes required handle closure and release-commit steps.
5. Pipe/operation ownership is released and integrated registry ownership is released.
6. Adapter release callback acknowledges the Submitter, invokes the caller completion callback with the recorded result, and frees the envelope.

Every stage must be tied to the same operation identity and must have explicit terminal evidence. A successful callback invocation alone is not sufficient if earlier ownership/finalizer stages are uncertain.

For ISSUE_PROVEN_NOT_SUBMITTED, the operation does not take the Worker handoff path, but it is not automatically terminal. The Adapter's non-submission cleanup/finalization path must still reach a determinate terminal state, release retained resources/ownership, and complete the required Submitter/Adapter disposition. If that cleanup evidence is absent or uncertain, quarantine.

### 5.2 Nonterminal/quarantine conditions

The following must never be normalized to successful local terminal:
- Issue UNKNOWN
- Issue OS_ACCEPTED with Handoff NOT_TRANSFERRED or UNKNOWN
- Handoff transferred-degraded
- Worker wait/completion uncertainty
- Worker/Adapter DEGRADED state
- retained envelope or unresolved ownership
- finalizer close/release failure
- owner/registry release invariant failure
- callback/acknowledgement failure
- missing, mismatched, or contradictory operation identity

The existing evidence indicates that Issue accepted followed by Handoff not transferred is an ownership uncertainty and must be quarantined, not retried as though it were never submitted.

### 5.3 Volatile versus restart-durable

The local evidence chain described above is process-lifetime evidence. Current implementation does not provide:
- durable namespace/sequence allocation before operation exposure,
- append-only persisted evidence for every transition,
- restart replay/reconstruction of Worker and Adapter ownership,
- proof that external callbacks and recipient state survived a crash,
- crash-safe reconciliation of unknown effects.

Consequently, LOCAL_LIFECYCLE_TERMINAL_VOLATILE must not be named simply DURABLE_COMMITTED, and must not be used to claim crash recovery or exactly-once delivery.

## 6. Policy and operation-kind decisions

Still unresolved and must remain unassigned:
- Mapping GENERIC/READ/WRITE/MESSAGE to mandatory evidence planes.
- Whether a specific operation kind requires Issue only, Issue+Handoff, or recipient receipt.
- Policy owner, immutable policy ID/version, migration rules, and downgrade behavior.
- Recipient protocol identity and verifier trust policy.
- Persistence and recovery requirements for terminal evidence.

Do not derive required evidence from enum labels alone. In particular, READ/WRITE/MESSAGE do not by themselves specify whether the caller's desired semantic completion is OS completion, Worker ownership completion, remote application processing, or durable recipient commit.

## 7. Runtime fail-closed rules proposed

- v1 RECIPIENT_COMMIT config: reject at runtime as UNSUPPORTED_RECIPIENT_VERIFIER, even if config_validate_v1 currently returns OK.
- Any profile with recipient capability declarations but no matching verifier interface: reject initialization for that profile.
- Quarantine-only local profile: may use Issue/Handoff callbacks, but must report only the exact local/transport scope actually proven.
- Never silently downgrade RECIPIENT_COMMIT to QUARANTINE_ONLY.
- Never promote QUARANTINE_ONLY to RECIPIENT_COMMIT based on provider declarations.
- Any missing/malformed/contradictory evidence: UNKNOWN + quarantine.
- Do not add a verifier callback to the v1 ABI in place; introduce a separately versioned contract and explicit compatibility rules.

## 8. Work deliberately not performed

- No source/header/test/project configuration/README changes.
- No WAL record-format changes, Writer, replay, startup recovery, or filesystem persistence.
- No recipient protocol implementation or capability enablement.
- No build or test run; this was a read-only contract review and design report.
- No install/download, process termination, or Production modification.

## 9. Reproducibility: inspected source hashes

SHA-256 verified in the Working Copy:
- raya-durable-contract-v1.h: 2FA73C0B77968D48909982379EC851DD5ECE1B0FD7DEEC7FD960AA04B7984B31
- raya-durable-contract-v1.c: 0CBEE0D35A98768A322D37B1BBA065BDBE44DBB1A513E366E1BDC42FA1CC637F
- raya-durable-contract-v1-test.c: 4CDD7E53C32987529B25736190CC427C13993BF7539BD765CAAEECFC66BC211F
- raya-durable-wal-v1.c: E1BD46915B859751DAB20B1E62352A39C72C105B75979DB0DE9CCC3B007159E5
- reaper-managed-worker-v2.c: 5B0B43BA60303A0EC3EA07A9E26A641E32E40812702C0F87EC37C1ADD11DD8DD
- integrated-submit-adapter-v2.c: A9B39D0D4094E8418F820E53B3E28FEE64FC7B0A9EC417450F25EC422EE8D87F
- pipe-io-owner-v2.c: 9A27A45414D4A58374620B761D7BD0E9CDD832D48A68598214C0CF25E1D529BC

## 10. Next gated engineering slice

Before implementation, obtain explicit product decisions for:
1. Which externally observable event constitutes the required terminal outcome for each real operation kind.
2. Whether the project needs only local lifecycle terminal, transport terminal, or recipient commit.
3. Recipient identity/protocol and the authoritative source of authenticated receipts/status.
4. Whether recipient commit must survive process restart and what durable storage/retention guarantees are required.
5. Who owns policy versioning and compatibility/migration.

After those decisions, implement a separately versioned v2 contract with a real verifier interface and negative tests. Keep v1 source/ABI unchanged. Then separately design persistence and recovery; do not combine those changes into one unreviewable slice.

---
End of design report.
