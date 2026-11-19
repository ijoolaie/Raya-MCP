# Versioned Durable Contract v1 — Pure Types and Validation Slice
Date: 2026-11-17
Status: implemented in isolated new files; not integrated with Submitter/Adapter; not a durability or recovery claim.

## Scope
Added three new files in the Working Copy:
- `raya-durable-contract-v1.h`
- `raya-durable-contract-v1.c`
- `raya-durable-contract-v1-test.c`

No existing header, source, callback ABI, public struct, Submitter, Adapter, Worker, Orchestrator, journal, or Production file was modified.

## Implemented contract types
- Fixed-width v1 operation identity: 16-byte namespace, nonzero 64-bit sequence, explicit operation kind, 32-byte payload digest, schema/size and reserved field.
- Typed native Issue result: PROVEN_NOT_SUBMITTED / OS_ACCEPTED / UNKNOWN.
- Typed Worker handoff result: TRANSFERRED / NOT_TRANSFERRED / UNKNOWN, with Worker instance and operation token evidence for TRANSFERRED.
- Capability/configuration declaration with explicit guarantee level:
  - QUARANTINE_ONLY requires identity, local journal, typed Issue and typed handoff capabilities.
  - RECIPIENT_COMMIT additionally requires recipient idempotency, durable receipt and status-query capabilities.
- Pure validators and normalizers. Malformed sizes, versions, reserved fields, identity echoes, native statuses or handoff evidence normalize to UNKNOWN or fail configuration validation.

## Important semantic boundary
The implementation intentionally has no recipient receipt structure or verifier and no persistence implementation. Capability flags describe a required future contract; they do not prove a connected recipient currently implements it. No code maps Worker handoff to recipient commit. No automatic retry/replay behavior exists.

## Validation
Compiled with MSVC x64:
`cl.exe /nologo /W4 /WX /TC raya-durable-contract-v1.c raya-durable-contract-v1-test.c`

- Compiler: success, warnings treated as errors.
- Test executable: 30 checks per run.
- Repetitions: 25.
- Total: 750 checks passed, 0 failures.
- Executable inspected: 150,528 bytes, SHA-256 `204FA492CCFD8559CEF1F48E56921946F4064FEF499D472636FF3E0275E361B5`.
- Object files inspected:
  - test object 13,639 bytes, SHA-256 `801F0C8D1E8A99474CE8A342023EC5485249215DAF24046978F84E018BDBC12C`
  - contract object 3,775 bytes, SHA-256 `BC2ABC5D58ED7B744E68E6D0ED3D3846635C66D7A1CC01DB3D5E1E5ED1BF76A3`
- Temporary executable and both object files removed after inspection. No other files removed.

## Current new-file hashes
(To be recorded after final report write and verification.)

## Limitations / next gates
1. Namespace provisioning authority and clone/restore fencing remain unresolved.
2. The payload digest canonicalization contract is not implemented; caller must not treat this slice as a hashing implementation.
3. The capability config is only a validator input; no start_v3 API exists.
4. No WAL, durable ID reservation, exclusive writer lock, flush semantics, crash scanner, receipt query, or reconciliation exists.
5. Recipient idempotency/receipt/status query is not implemented.
6. No existing callback callsite uses these types.
7. No ABI compatibility or Production readiness claim.

Next safe work: review the v1 type layout and validation policy, then add a separate versioned callback table/start configuration only after namespace ownership and recipient guarantee decisions are approved. Keep the legacy API isolated.

Phase 1 remains NOT ACCEPTED. Production remains NO-GO.
