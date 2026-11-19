# Durable WAL v1 — In-Memory Record Codec and Read-Only Scanner

Date: 2026-11-17  
Status: **provisional implementation; not an approved persistent format**  
Workspace: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`

## Scope and safety boundary

Implemented a bounded in-memory encoder and read-only scanner for the candidate 192-byte WAL record envelope. This is a pure byte-buffer layer. It performs no file I/O, does not create/open/append a WAL, does not truncate or repair data, does not replay operations, and is not connected to Submitter, Adapter, Worker, Orchestrator, or startup.

The format remains provisional. The implementation must not be treated as a durable journal, crash-recovery facility, or delivery guarantee.

## Files created

1. `raya-durable-wal-v1.h`
2. `raya-durable-wal-v1.c`
3. `raya-durable-wal-v1-test.c`
4. `raya-durable-wal-v1-boundary-test.c`

No existing source, model, README, project configuration, or production file was changed in this slice.

## Implemented behavior

- Explicit little-endian serialization of the candidate 192-byte envelope; no hashing of C struct padding.
- BCrypt SHA-256 record digest over the exact 192 bytes with the record-digest field zeroed.
- Header, format, type, fixed-size, namespace, journal sequence, predecessor digest, payload digest, metadata digest, and type-specific field validation.
- Contiguous journal sequence and hash-chain validation; first record must be the single header.
- Bounded scanner: maximum 4,096 records and 128 distinct operations.
- Operation identity uniqueness by namespace + operation sequence within the fixed journal namespace; new ID reservations must be strictly increasing in journal order; operation kind and payload digest remain immutable.
- Transition checks for ID_RESERVE, REQUEST_ACCEPTED, EFFECT_BEGIN, EFFECT_RESULT, RECONCILIATION, and OPERATION_TERMINAL.
- EFFECT_RESULT UNKNOWN leaves the effect unresolved; no blind retry is accepted.
- A retry attempt is permitted only after a determinate PROVEN_NOT_COMMITTED result/reconciliation and must increment the attempt.
- Reconciliation requires the exact outstanding UNKNOWN effect attempt and a nonzero metadata digest. The scanner checks structural linkage only; it does not authenticate the evidence package.
- Distinct classifications: EMPTY, VALID_COMPLETE, INCOMPLETE_FINAL_RECORD, CORRUPT, LIMIT_EXCEEDED, CRYPTO_ERROR, INVALID_ARGUMENT.
- Scanner input is not modified. No magic resynchronization, malformed-record skipping, automatic tail truncation, repair, or replay.

## Validation

Compiler: MSVC x64, `/W4 /WX /TC`, linked with `bcrypt.lib`.

Regression after limit-prefix correction: **594 checks per run × 25 runs = 14,850 checks; zero failures**.

Semantic-boundary harness: **4,287 checks per run × 25 runs = 107,175 checks; zero failures**. It covers the exact 4,096-record limit, the 4,097th complete record, exact 128-operation capacity and the 129th operation, maximum `ULONGLONG` reservation, independent ISSUE/HANDOFF attempts, and invalid reconciliation references. On the 4,097th record, the scanner now returns `LIMIT_EXCEEDED` while preserving the validated prefix: 4,096 records, journal sequence 4,096, `valid_bytes=4096×192`, and one tracked operation.

The malformed-input fuzz harness was rerun unchanged: **500,000 cases and 3,500,000 assertions across 25 runs; zero failures**. These are deterministic repetitions with the same seed, not 25 independent seeds.

A terminal-policy probe confirms the current implementation accepts `OPERATION_TERMINAL` when either ISSUE or HANDOFF alone is COMMITTED and the other effect plane is absent; it rejects terminalization when both are absent. This behavior is recorded as an unresolved contract ambiguity, not approved semantics. Operation-kind-specific required-effect rules are not yet defined.

Coverage includes:
- Valid header-to-terminal journal path including UNKNOWN, evidence-linked reconciliation, and a subsequent incremented attempt.
- All 191 possible incomplete final-record tail lengths (1..191) after a valid complete prefix.
- Record digest corruption and a predecessor-chain mismatch with a separately valid record digest.
- Wrong expected namespace, wrong format version, wrong fixed size, malformed header fields, and empty/invalid input.
- Duplicate and out-of-order operation reservations, missing request acceptance, result without a matching in-progress attempt, terminalization while an effect is in progress, blind retry after UNKNOWN, and terminalization with only one effect plane present.
- Unresolved-effect reporting, operation-table exhaustion, and byte-for-byte input immutability.
- Encoder rejection of invalid type-specific fields.

The test harness does not provide randomized fuzzing, memory-safety instrumentation, cryptographic authenticity, power-loss testing, filesystem testing, or a file-writer integration test.

## Source hashes

| File | Bytes | SHA-256 |
|---|---:|---|
| `raya-durable-wal-v1.h` | 2,106 | `CFA50B740F67013171B3199F769F331A5AFECBD95A86A66D77185AABEA2B6DF9` |
| `raya-durable-wal-v1.c` | 10,195 | `E1BD46915B859751DAB20B1E62352A39C72C105B75979DB0DE9CCC3B007159E5` |
| `raya-durable-wal-v1-test.c` | 8,302 | `68983E52C2E82D26DA562DAE5220D3E3129BF6AB266FE2777E56E0ABCED7DB83` |
| `raya-durable-wal-v1-boundary-test.c` | 6,158 | `B913698C67AB23BF0C169A8F79DD3CDDA320305AEABEED3E0A408B47E5FBFDE2` |

Temporary executable SHA-256 values (all removed after verification):
- Boundary: `D69E1339716BDF2F89875806C2C73CF27F9A65DFB8DFD532924BE8F1B753FDF4`
- Regression: `14FD4447535BFDABD37D7543A4600EF30C4134E47C584D39F5D9F1358DE2455E`
- Fuzz: `71F031EEB263F3B9F6AAA9901A2A9FB58D1C183A7AB1ADF28DC2FDDB151CC5AF`

## Known limits and non-claims

- 192-byte format, offsets, and record semantics remain candidate-only pending design approval.
- Maximum 128 tracked operations and 4,096 records; limit exhaustion fails closed while reporting the exact validated prefix for a complete over-limit record.
- `OPERATION_TERMINAL` currently has no operation-kind-specific required-effect policy; one committed effect plane can terminalize an operation while the other is absent. This must be resolved before persistent format approval.
- This is a scanner over caller-supplied memory, not a Windows file scanner or journal writer.
- BCrypt SHA-256 detects accidental modification and chain reordering; it is not a signature, MAC, or protection against a malicious writer able to recompute the chain.
- No exclusive writer fence, namespace provisioning, durable identity reservation, FlushFileBuffers policy, ACL/reparse-point enforcement, or power-loss guarantee exists.
- No evidence verifier, recipient idempotency/receipt/status query, durable reconciliation authorization, compaction, checkpoint, backup, or restore protocol exists.
- No cross-process recovery, kernel-handle restoration, callback invocation, exactly-once delivery, or automatic replay is implemented.
- Product/storage decisions in `DURABLE-WAL-V1-BINARY-RECORD-AND-READONLY-SCANNER-DESIGN-20261117.md` remain blocking before any file I/O is added.

## Temporary artifact handling

The original regression build directory contained exactly three files and was cleaned after inspection. A later dedicated semantic/limit build contained exactly nine files across three isolated build subdirectories (three executables and six object files). Every file was inspected by relative path, byte size, and SHA-256 before removal; all nine were removed and the temporary directory was confirmed absent. No project checkpoints, traces, models, reports, or state stores were deleted.

Disk free space after the latest semantic/limit build cleanup: C = 11,480,354,816 bytes; D = 2,186,481,664 bytes.

## Decision

This slice establishes only a bounded in-memory record codec and transition validator. It does **not** authorize a WAL writer or integration. The next gate is design review of the provisional wire format and product/storage decisions, followed by adversarial/fuzz validation before any file I/O is considered.

Phase 1 remains **NOT ACCEPTED**. Production remains **NO-GO**.
