# Versioned Durable Operation Identity and Callback Contract - Design Review
Date: 2026-11-17
Status: design only; no source/API changes authorized by this slice.

## 1. Scope and inspected implementation

Working Copy: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`.
Source Project and Production are untouched.

Inspected:
- `dedicated-io-submitter-v2.h/.c`
- `integrated-submit-adapter-v2.h/.c`
- Submitter callsites in the existing admission, ACK-race, backpressure, failure, multiproducer, native, and orchestrator tests.
- `validation/SUBMITTER-DURABLE-LEDGER-RECOVERY-CONTRACT-DESIGN-20261117.md`

Current callback/callsite facts:
1. The existing Issue callback returns NOT_ACCEPTED / ACCEPTED / UNCERTAIN.
2. The existing Publish callback returns int; zero/nonzero has no typed evidence or receipt.
3. The not_issued callback is void.
4. Existing request_id is allocated by one Submitter instance and is not durable or stable across restart.
5. All current Submitter start callsites use the legacy callback signatures. The Integrated Adapter is the production-like callback implementation; the other callsites are tests.
6. Integrated Adapter Issue calls ReadFile/WriteFile with the operation's HANDLE, buffer and OVERLAPPED. TRUE and ERROR_IO_PENDING both mean the OS accepted/started the operation; neither proves remote application-level processing.
7. Integrated Adapter Publish calls raya_managed_worker_handoff (or the test hook variant). NOT_TRANSFERRED triggers quarantine/retention; successful handoff transfers ownership to Worker. It is an internal ownership transition, not a remote Publish receipt.
8. The Adapter's Worker handoff currently has a typed result enum, but that result is not a durable recipient receipt and does not survive process restart.
9. The OVERLAPPED, event, issuing HANDLE and buffer are process/kernel resources. A durable identity cannot resurrect them or make a pending I/O safe to replay.

## 2. Required semantic separation

Do not use one generic "Publish committed" state for all meanings. The next API family must distinguish three planes:

### A. Submitter admission identity
A durable operation identity identifies the logical request admitted by the broker. It does not by itself prove any external effect.

### B. Native I/O submission
Issue describes whether the OS accepted the ReadFile/WriteFile operation:
- PROVEN_NOT_SUBMITTED: affirmative native evidence that the call did not submit/queue the operation.
- OS_ACCEPTED: immediate completion or pending operation was accepted by the OS. This is not peer/application commit.
- UNKNOWN: the result cannot prove either of the above.

For the current adapter, an immediate non-pending error may be classified PROVEN_NOT_SUBMITTED only after the supported Win32 contract and exact error handling are reviewed. TRUE and ERROR_IO_PENDING map to OS_ACCEPTED. Any wrapper/test seam that cannot preserve GetLastError reliably must return UNKNOWN.

### C. Worker ownership handoff
Handoff describes who owns the accepted OVERLAPPED operation:
- TRANSFERRED: Worker accepted ownership and is responsible for terminal collection.
- NOT_TRANSFERRED: ownership did not transfer; caller/Adapter retains/quarantines the operation according to the existing recovery contract.
- UNKNOWN: ownership result cannot be established; retain/quarantine and prohibit cleanup/replay.

A TRANSFERRED result is not evidence that the peer received, processed, or committed the bytes.

### D. Recipient/application effect
A separate future recipient protocol must answer whether the logical operation was applied:
- COMMITTED with verifiable recipient receipt;
- PROVEN_NOT_COMMITTED with affirmative recipient evidence;
- UNKNOWN otherwise.

The current named-pipe byte-stream operation has no recipient-side durable deduplication/receipt in the inspected code. Therefore this plane is NOT IMPLEMENTED. Do not derive recipient commit from ReadFile/WriteFile success, Worker handoff, local completion, ACK, or a local journal record.

## 3. Versioned API boundary

Preserve the existing ABI and behavior:
- Keep `raya_submitter_start` and `raya_submitter_start_with_api` unchanged.
- Keep existing callback typedefs unchanged.
- Do not append fields to existing public structs in this slice.
- Do not silently reinterpret legacy callback results as durable dispositions.
- Persistent mode, when eventually implemented, must be selected through a distinct versioned entry point and an explicit capability/configuration object. It must reject legacy callbacks rather than silently falling back.

Candidate naming (design only):
- `raya_submitter_start_v3` / `raya_submitter_start_v3_with_api`
- `raya_submitter_durable_config_v1`
- `raya_submitter_operation_id_v1`
- `raya_submitter_issue_result_v1`
- `raya_submitter_handoff_result_v1`
- `raya_submitter_recipient_receipt_v1`

Names are provisional; do not reserve ABI or claim source compatibility.

## 4. Durable identity shape

Candidate fixed-width identity:
- namespace: 16 opaque bytes, provisioned by the owning broker installation/transport authority;
- sequence: unsigned 64-bit, nonzero, monotonically increasing within namespace;
- operation_kind: explicit enum, never inferred from a pointer or callback;
- identity_schema_version: explicit version.

Rules:
1. Namespace is provisioned and persisted outside the Submitter instance; it must survive ordinary process restart.
2. Sequence is reserved and durably recorded before it is exposed to callbacks or external systems.
3. Sequence gaps are legal; reuse, wrap and silent namespace regeneration are forbidden.
4. A restored/copied journal or cloned installation requires an explicit fencing/re-provisioning decision; two writers may not share the namespace.
5. Caller/business idempotency key is a separate optional semantic key. It must not replace the broker operation identity.
6. Payload digest is immutable metadata bound to the identity. Define canonicalization before hashing; do not hash raw pointers, struct padding, uninitialized bytes, or mutable buffers.
7. The identity is not an authorization token and does not prove that the caller is trusted.

Unresolved ownership choice: the namespace should be owned by the broker installation/transport identity rather than generated by each Submitter. Product architecture must decide the exact provisioning authority and clone/restore behavior before code.

## 5. Candidate typed result contracts

All result structs should be fixed-width, versioned, include `struct_size`, explicit reserved-zero fields, and validate the echoed operation identity. A malformed/unsupported result maps to UNKNOWN and fail-closed.

### Issue v1
Input: operation identity, operation kind, payload digest, and native operation descriptor (still process-local).
Output disposition:
- PROVEN_NOT_SUBMITTED
- OS_ACCEPTED
- UNKNOWN

Additional fields:
- native error/status;
- immediate byte count where meaningful;
- evidence kind and evidence version;
- operation identity echo.

No result may claim recipient/application COMMITTED.

### Handoff v1
Input: operation identity plus the live process-local operation descriptor.
Output:
- TRANSFERRED
- NOT_TRANSFERRED
- UNKNOWN

Additional fields:
- Worker/registry instance identity;
- Worker operation/registration token if available;
- evidence kind/version;
- identity echo.

A Worker registration token is only a local ownership/evidence reference unless separately persisted and fenced. It is not a remote receipt.

### Recipient receipt v1 (future, separate capability)
A future recipient adapter may return:
- COMMITTED + receipt;
- PROVEN_NOT_COMMITTED + affirmative evidence;
- UNKNOWN.

Receipt minimum fields:
- schema/version and recipient identity;
- full operation identity echo;
- payload digest echo;
- recipient durable commit sequence or equivalent;
- receipt identifier and cryptographic/authenticated integrity binding;
- evidence timestamp/epoch and verifier policy.

A receipt must be verifiable against a documented trust root and recipient durability guarantee. A bare Boolean, HTTP status, pipe write completion, or locally generated token is insufficient.

## 6. Capability negotiation and fail-closed behavior

The versioned start configuration must explicitly declare capabilities, not infer them from non-NULL function pointers.

Candidate capability flags:
- DURABLE_IDENTITY
- DURABLE_LOCAL_JOURNAL
- ISSUE_TYPED_RESULT
- HANDOFF_TYPED_RESULT
- RECIPIENT_IDEMPOTENCY
- RECIPIENT_DURABLE_RECEIPT
- RECIPIENT_STATUS_QUERY

Validation rules:
1. Unknown capability bits or unsupported schema versions => reject start.
2. Durable journal enabled without durable namespace and exclusive writer fence => reject start.
3. Durable mode enabled with a legacy callback => reject start.
4. Recipient exactly-once/effect-replay mode enabled without recipient idempotency + verifiable receipt/status query => reject start.
5. A configuration may support local crash evidence without recipient receipts, but its declared guarantee is only at-most-once attempt plus UNKNOWN quarantine. It must not advertise exactly-once or automatic replay.
6. Missing/invalid callback result, identity mismatch, receipt mismatch, timeout or lost response => UNKNOWN.
7. UNKNOWN is never silently converted to PROVEN_NOT_SUBMITTED or PROVEN_NOT_COMMITTED.

## 7. Crash and restart semantics

- Crash before durable ID reservation: no identity was exposed; no external call may have occurred.
- Crash after ID reservation but before request acceptance: identity gap; do not reuse.
- Crash after request acceptance but before EFFECT_BEGIN: candidate not attempted only if the implementation proves no external call could precede EFFECT_BEGIN.
- Crash after durable EFFECT_BEGIN and before durable result: UNKNOWN, even if the callback may not yet have run.
- Crash after OS_ACCEPTED but before handoff result: ownership/effect state UNKNOWN; preserve metadata, do not reconstruct or replay the OVERLAPPED request.
- Crash after Worker handoff but before local journal result: ownership may be UNKNOWN across process death; do not claim recipient commit.
- Crash after I/O completion but before durable terminal record: recipient/application effect remains UNKNOWN unless the recipient protocol provides independent evidence.
- Only a validated recipient receipt can support recipient COMMITTED; local Worker terminal completion is not enough.

A future restart scanner must be read-only by default. It may classify and quarantine but must not recreate HANDLE/OVERLAPPED state, reissue bytes, free caller memory from the prior process, or mark the Submitter RUNNING while unresolved UNKNOWN effects remain.

## 8. Compatibility and migration

1. Existing callers continue using the legacy entry points and legacy in-memory guarantees.
2. A separate opt-in versioned entry point is added only after the durable contract is approved.
3. No mixed callback set: all callbacks participating in a durable transaction must be versioned and capability-checked.
4. Adapter migration requires a distinct typed Issue adapter and typed Worker handoff adapter. Do not rename the current adapter_publish result as a recipient receipt.
5. Test-only callback implementations must explicitly declare their capability profile. Legacy tests remain legacy; durable tests use dedicated v1 fixtures.
6. Public struct layout and callback ABI changes require a separate compatibility review; no ABI stability claim is made today.

## 9. Required tests before implementation acceptance

- Legacy entry points compile and preserve all current test behavior.
- Version/capability negotiation rejects every incomplete or contradictory configuration.
- Identity namespace mismatch, sequence zero, sequence reuse, sequence exhaustion, payload digest mismatch and identity echo mismatch.
- Issue: immediate success, ERROR_IO_PENDING, proven immediate failure, ambiguous wrapper error, malformed result.
- Handoff: transferred, not transferred, unknown, duplicate result, identity mismatch, Worker registration failure.
- Explicitly prove that TRANSFERRED never maps to recipient COMMITTED.
- Crash injection before/after durable ID_RESERVE, REQUEST_ACCEPTED, EFFECT_BEGIN, callback entry/return, result append/flush and terminal append/flush.
- Receipt authenticity, wrong recipient, wrong operation identity, wrong payload digest, stale receipt, duplicate receipt and status-query consistency.
- No replay of UNKNOWN and no restoration of process-local OVERLAPPED resources.
- Full existing Submitter/Adapter/Worker/Orchestrator regression suites after the new path is isolated.

## 10. Decisions still required

1. Namespace provisioning authority and installation clone/restore fencing.
2. Whether this prototype's durable guarantee is only at-most-once attempt, or whether a recipient can implement durable deduplication and receipts.
3. The actual recipient/application protocol. Named-pipe transport completion is not a recipient commit protocol.
4. Supported Windows versions, filesystem/volume, ACL identity, storage durability boundary and journal path.
5. Journal size limits, retention, backup/restore and operator reconciliation policy.
6. Whether a durable mode can be useful before a recipient status-query endpoint exists. If yes, it must remain quarantine-only for ambiguous operations.

## 11. Decision and next safe step

Decision:
- Keep legacy callback ABI untouched.
- Treat native Issue and Worker handoff as separate result domains.
- Treat recipient effect as a third, currently unsupported domain.
- Do not add a WAL writer or automatic recovery yet.
- Do not claim exactly-once, replay safety, or restart continuation for in-flight pipe I/O.

Next safe implementation-sized step, after product decisions: create isolated versioned type declarations and pure validation/normalization helpers only; no file I/O, no callback execution, no mutation of legacy structs. Then add contract unit tests before integrating a journal.

Phase 1 remains NOT ACCEPTED. Production remains NO-GO.
