# Durable WAL v1 — Binary Record and Read-Only Recovery Scanner Design
Date: 2026-11-17
Status: design specification only; no WAL file is created or opened.

## Scope
Working Copy: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`.
This document specifies a candidate byte-level record envelope and deterministic read-only scanner contract. It does not implement a writer, parser, repair, truncation, replay, or startup integration. It does not select a journal path, ACL, storage device, namespace provisioning authority, or product durability guarantee.

## 1. Design constraints carried forward
- Single writer only; a process-lifetime exclusive writer fence is required before any future append.
- Stable namespace + nonzero 64-bit sequence is the durable operation identity. Sequence gaps are allowed; reuse/wrap are forbidden.
- Reserve identity durably before exposing it to callbacks.
- Append EFFECT_BEGIN durably before external callback invocation.
- Record the callback outcome durably before publishing the corresponding committed state.
- Crash after EFFECT_BEGIN without a validated result means UNKNOWN; never replay automatically.
- OS_ACCEPTED and Worker TRANSFERRED are not recipient/application COMMITTED.
- No compaction, checkpoint, in-place update, or automatic tail truncation in v1.
- A readable WAL does not authorize restoring process-owned HANDLE/OVERLAPPED/event/buffer resources or starting the Submitter with unresolved effects.

## 2. Candidate fixed-size record envelope
Candidate v1 record size: **192 bytes**, little-endian Windows representation. Fixed-size records simplify exact-write validation and eliminate ambiguous variable-length parsing in the first implementation. All integer fields are unsigned unless explicitly described otherwise.

| Offset | Size | Field | Rule |
|---:|---:|---|---|
| 0 | 4 | magic | ASCII `RYW1` |
| 4 | 2 | format_version | exactly 1 |
| 6 | 2 | record_type | enum below |
| 8 | 4 | total_size | exactly 192 |
| 12 | 4 | header_size | exactly 192 |
| 16 | 8 | journal_sequence | starts at 1, increments exactly by 1 |
| 24 | 16 | namespace | bound namespace; nonzero |
| 40 | 8 | operation_sequence | zero only for JOURNAL_HEADER |
| 48 | 4 | operation_kind | zero only for JOURNAL_HEADER; otherwise known operation enum |
| 52 | 4 | effect_kind | zero unless EFFECT_BEGIN/EFFECT_RESULT |
| 56 | 4 | attempt | zero except effect records; positive and monotonic per effect |
| 60 | 4 | disposition | record-type-specific enum; reserved values rejected |
| 64 | 32 | payload_digest | operation digest; zero only for JOURNAL_HEADER |
| 96 | 32 | previous_record_digest | all zero for first record; otherwise exact prior record digest |
| 128 | 32 | record_digest | SHA-256 of all 192 bytes with this field zeroed |
| 160 | 32 | metadata_digest | SHA-256 of bounded external metadata/evidence descriptor; zero if absent |
| 192 | — | end | no trailing bytes in a record |

The record envelope is a candidate wire contract, not a C ABI promise. Implementations must serialize fields explicitly; never hash compiler struct padding or raw in-memory structs. The digest algorithm and byte order are part of the format and cannot be silently changed in v1.

### Record types
1. JOURNAL_HEADER: first and only header for a fresh generation; binds namespace and format. Operation/effect fields and payload/metadata digests are zero.
2. ID_RESERVE: reserves one operation sequence. Identity must not have appeared earlier.
3. REQUEST_ACCEPTED: immutable operation kind and payload digest binding; requires prior ID_RESERVE.
4. EFFECT_BEGIN: names one effect kind and attempt; requires REQUEST_ACCEPTED and no unresolved prior attempt for that effect.
5. EFFECT_RESULT: terminal evidence for the exact in-progress effect attempt; disposition is COMMITTED, PROVEN_NOT_COMMITTED, or UNKNOWN. UNKNOWN is terminal for automatic execution and requires separate reconciliation.
6. OPERATION_TERMINAL: closes the logical request only when all required effect planes have determinate terminal states; never infer recipient commit from Issue or Worker handoff.
7. RECONCILIATION: records an owner-authorized, externally evidenced resolution of a prior UNKNOWN. Metadata digest must identify the evidence package; local journal absence is never proof of non-commit.

CHECKPOINT_COMMIT is intentionally excluded from v1. No compaction or retention is permitted until a separate snapshot/rotation protocol is specified.

## 3. Digest and chain rules
- Compute SHA-256 over the exact 192 serialized bytes after zeroing bytes 128..159.
- A record is valid only if the computed digest equals record_digest.
- The first record must be JOURNAL_HEADER, journal_sequence 1, previous_record_digest all zero.
- Every later record must match the same namespace, have sequence previous+1 without wrap, and previous_record_digest equal the preceding validated record_digest.
- A duplicate record digest, duplicate operation identity reservation, sequence gap, chain mismatch, unsupported type/version, nonzero reserved/unused fields, or invalid state transition is corruption.
- Digest chaining detects accidental modification and reordering; it is not an authenticity signature and does not prevent a malicious writer with full file access from recomputing the chain. ACL and writer identity remain mandatory.

## 4. Read-only scanner contract
Input: a caller-owned byte span, expected namespace, and explicit maximum byte/record limits. The scanner performs no file I/O and never mutates input.

Output classifications:
- VALID_COMPLETE: all bytes form complete valid records and the state machine has no structural errors.
- INCOMPLETE_FINAL_RECORD: remaining byte count is less than 192 after a valid prefix. This is evidence of a torn/incomplete tail, not permission to truncate it.
- CORRUPT: any complete record violates format, digest, chain, namespace, sequence, identity uniqueness, or transition rules.
- LIMIT_EXCEEDED: caller-specified scan/operation table limit reached; do not claim validity beyond the scanned prefix.
- EMPTY: no bytes; never silently provision a new namespace from an empty/missing journal.

Read-only scanner rules:
1. Never skip a malformed complete record.
2. Never search forward for the next magic value.
3. Never accept a valid-looking record after a gap.
4. Never truncate, repair, rewrite, rotate, or delete.
5. A valid prefix followed by fewer than 192 bytes is INCOMPLETE_FINAL_RECORD; caller policy remains fail-closed pending explicit repair authorization.
6. Any complete but invalid final record is CORRUPT, not a torn tail.
7. The scanner must return the last validated byte offset, last journal sequence, last record digest, namespace, and per-operation effect classification without exposing a writable file handle.
8. Resource exhaustion or bounded state-table overflow is LIMIT_EXCEEDED, not partial success.

## 5. Operation/effect transition rules
- ID_RESERVE: identity unique; operation sequence nonzero; no previous identity reservation.
- REQUEST_ACCEPTED: exactly one per identity; matches reserved namespace/sequence/kind/digest.
- EFFECT_BEGIN: requires accepted request; attempt starts at 1 and increments only after an explicitly reconciled PROVEN_NOT_COMMITTED outcome. COMMITTED and UNKNOWN cannot be retried.
- EFFECT_RESULT: must match one outstanding EFFECT_BEGIN of the same operation/effect/attempt. A second result is corruption.
- UNKNOWN: remains quarantined; no new EFFECT_BEGIN for that effect.
- RECONCILIATION: must reference the exact prior UNKNOWN attempt and a nonzero metadata_digest; the scanner validates structural linkage but cannot authenticate external evidence by itself.
- OPERATION_TERMINAL: rejected while any effect is IN_PROGRESS or UNKNOWN, or while required effect planes are absent.
- Recipient/application COMMITTED is permitted only for a future separately versioned recipient receipt effect type with a verifier contract. Current Issue and Worker handoff types cannot encode recipient commit.

## 6. Crash boundary interpretation
- Before ID_RESERVE is durably flushed: identity was not exposed; no callback may have been invoked.
- After ID_RESERVE: identity is consumed even if no request was accepted; gaps are legal.
- After REQUEST_ACCEPTED but before EFFECT_BEGIN: no effect may have been invoked only if implementation enforces EFFECT_BEGIN flush before callback entry.
- After EFFECT_BEGIN without EFFECT_RESULT: UNKNOWN after restart, even if callback may not have run.
- After callback returns but before EFFECT_RESULT flush: UNKNOWN after restart.
- After EFFECT_RESULT flush: local callback result evidence is durable, but this does not automatically mean recipient application commit.
- A scanner result never reconstructs process-local kernel objects or releases prior-process resources.

## 7. Required tests for a future parser implementation
- Exact 192-byte record and field offset fixtures; byte-order fixtures.
- Header first/duplicate/missing; namespace mismatch.
- Sequence 0, gaps, duplicates, max sequence and overflow.
- Wrong magic/version/type/size/header size.
- Every reserved/unused field nonzero.
- Record digest corruption at every byte region.
- Previous digest mismatch and reordered records.
- Duplicate operation identity; payload digest mismatch.
- Every legal/illegal transition, duplicate begin/result, stale attempt, unknown replay attempt.
- Every possible truncation offset 1..191 after a valid prefix.
- Complete malformed final record versus incomplete tail distinction.
- Valid prefix plus arbitrary trailing bytes; no magic resynchronization.
- Maximum byte/record/operation limits and overflow.
- Randomized malformed byte spans under memory-safety instrumentation where available.
- Verify scanner never writes to its input buffer.

## 8. Product/storage decisions still blocking writer implementation
1. Namespace provisioning authority and clone/restore fencing.
2. Approved local storage root, service identity, ACL, and reparse-point policy.
3. Supported Windows versions, filesystems, volumes, and storage stack.
4. Required FlushFileBuffers/WRITE_THROUGH policy and power-loss assumptions.
5. At-most-once attempt versus recipient idempotency/receipt/status-query guarantee.
6. Evidence package format, trust root, verifier, and operator authorization.
7. Journal capacity, retention, backup, restore, and disaster recovery policy.
8. Missing/corrupt journal provisioning policy.
9. Explicit authorization for a writer and for any repair/truncation operation.

## 9. Decision
The 192-byte envelope and scanner rules are a candidate specification for review, not an approved persistent format. The next implementation gate should be a pure in-memory serializer/parser and transition validator using immutable byte buffers and bounded state. It must be tested before adding file I/O. A Windows file writer must remain blocked until the storage and namespace decisions above are approved.

Phase 1 remains NOT ACCEPTED. Production remains NO-GO.
