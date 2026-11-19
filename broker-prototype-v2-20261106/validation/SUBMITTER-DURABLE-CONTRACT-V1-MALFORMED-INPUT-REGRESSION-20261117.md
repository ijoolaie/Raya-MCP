# Durable Contract v1 — malformed-input regression expansion — 2026-11-17

## Scope
Extended only `raya-durable-contract-v1-test.c` in the Working Copy. No production source, public header, legacy Submitter API, persistence, WAL, replay, or callback execution was changed.

## Additional validation coverage
- Operation identity: malformed size/schema, lower and upper invalid operation-kind values, namespace mutation, operation-kind inequality, and NULL equality input.
- Configuration: identity-only configuration without the required durable guarantee, missing typed Issue/Handoff capabilities, invalid guarantee, partial recipient capability groups, and the complete optional recipient capability group under QUARANTINE_ONLY.
- Typed Issue: invalid disposition, malformed size/schema/reserved field, and a deliberately maximal `transferred_bytes` value.
- Typed Handoff: invalid disposition, malformed size/schema/reserved field, and inconsistent worker identity for NOT_TRANSFERRED.

## Results
- MSVC x64, `/W4 /WX /TC`: compiled successfully.
- One initial build-command attempt incorrectly passed an object file through `/TC`; compilation rejected the object as source. Corrected by compiling each C source separately and linking the two object files. No source correction was needed for this command error.
- Regression executable: 54 checks/run, 25 runs, **1,350 checks passed, 0 failures**.
- Executable SHA-256: `F1C90776EE196768BE3D110695DC0ADD40EB76D017DFD6B336A410EE9B8FBC52`.
- Test object SHA-256: `253930E488B08D51305B527EBE6F79791E5268FE475BF528894ABD7CC3366EFC`.
- Implementation object SHA-256: `6DCB7F718B139F618F3DEBDAF62456516BD263137C2F4F87C6599D23BBC9E58A`.
- The three temporary build artifacts were inspected and removed; zero matching temporary artifacts remained.

## Contract clarifications discovered during review
- Recipient capability bits are an optional all-or-none group. They may be advertised with QUARANTINE_ONLY, but they do not upgrade the configured guarantee. RECIPIENT_COMMIT still requires the full local and recipient capability sets.
- `transferred_bytes` is currently diagnostic evidence only. The result structure does not carry an expected operation length, so the pure normalizer cannot decide whether a byte count is complete. In particular, OS_ACCEPTED means OS-level acceptance, not remote application commit.
- A nonzero payload digest is required by the current v1 identity contract. Empty payloads must still supply the digest of their canonical representation; zero-filled digest is invalid.

## Current source hashes
- `raya-durable-contract-v1.h`: `F6EF9F6DDB81BD4737DDA44FCC96125872298E227E1909724404D12DB20AADDE`
- `raya-durable-contract-v1.c`: `794761F7B629031234F88B8AFB0002BD70262F131CA56800B4C9420EEFE2CAD0`
- `raya-durable-contract-v1-test.c`: `B9C453EB37008ECCD29A95CE040396924407091A5B4BB91275F191CE945CA1F6`

## Boundaries and next decisions
This remains a pure in-memory type/validation layer. It does not provide durable identity reservation, callback idempotency, recipient receipts, persistence, crash recovery, or exactly-once semantics. Before a versioned start/callback API or WAL is implemented, the product must decide namespace ownership/lifetime and what recipient-side evidence can actually be guaranteed. Legacy API remains untouched.
