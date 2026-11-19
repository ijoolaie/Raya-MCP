# Durable WAL v1 — Deterministic Fuzz and Input-Boundary Validation

Date: 2026-11-17  
Status: provisional hardening evidence; not persistent-WAL approval  
Working Copy: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`

## Scope

Added one test-only source file:

- `raya-durable-wal-v1-fuzz-test.c`

No production implementation, wire-format definition, model, README, project configuration, or integration code was changed. The existing scanner and encoder were exercised as built. This fuzz harness does not access the filesystem and does not construct a WAL writer.

## Deterministic fuzz strategy

- Fixed PRNG seed: `0xD1B54A32D192ED03`.
- 20,000 cases per run.
- Random input lengths from 0 through 4,096 bytes.
- Random byte contents.
- 32-byte guard regions before and after the supplied input span.
- Full input-plus-guard snapshot compared after every scanner call.
- Status enum range, result/status agreement, valid_bytes bound, empty-input classification, complete-input byte count, incomplete-tail bound, and record-count bound checked.
- Encoder called with deliberately malformed all-0xFF fields; failure must not modify the output buffer.

## Results

MSVC x64, `/W4 /WX /TC`, linked with `bcrypt.lib`.

**20,000 cases × 25 runs = 500,000 deterministic fuzz cases.**  
**140,000 assertions per run × 25 = 3,500,000 assertions.**  
Failures: **0**.

The same fixed seed was used on every run, so the result is reproducible rather than a claim of 500,000 unique random seeds.

## AddressSanitizer attempt

A separate MSVC `/fsanitize=address` build was attempted. Compilation completed, but link failed because the toolchain could not locate `clang_rt.asan_static_runtime_thunk-x86_64.lib`. No dependency was installed or downloaded. Therefore this work has **no AddressSanitizer evidence** and does not claim sanitizer coverage. Temporary compiler outputs from the failed attempt were inspected and included in exact-file cleanup.

## Source hashes

| File | Bytes | SHA-256 |
|---|---:|---|
| `raya-durable-wal-v1.h` | 2,106 | `CFA50B740F67013171B3199F769F331A5AFECBD95A86A66D77185AABEA2B6DF9` |
| `raya-durable-wal-v1.c` | 10,173 | `341BD50A7C93DCA609892DFAC8FFF9D6944CC9578281FA8EAC73A49FC5F47B5A` |
| `raya-durable-wal-v1-fuzz-test.c` | 2,100 | `AAF2BC9EE964DEF14221EB09A761C890D35C3EEBC4F56A77279159709C728D8C` |

Successful fuzz executable SHA-256:  
`2CA4D4DE6826988A51377D33B5B7D2B605782245CDB849616FBD97224315E325`

## Temporary artifacts

The dedicated build directory was inspected. It contained the successful fuzz executable, two object files, and compiler PDB outputs, including a PDB left by the failed AddressSanitizer link. Each exact file was to be hash-inspected before removal. Only these generated build artifacts were removed; no project checkpoints, traces, model states, or reports were touched.

## Interpretation and limitations

This deterministic harness is a useful input-boundary and immutability check, not a replacement for coverage-guided fuzzing, sanitizer runs, formal verification, or security review. Random byte buffers rarely pass the record digest and hash-chain gates, so this run primarily exercises malformed-input rejection and memory-boundary invariants; it does not deeply explore every valid transition path. Existing deterministic transition tests remain separate.

No file I/O, durable identity reservation, writer lock, FlushFileBuffers policy, ACL/reparse-point enforcement, crash recovery, callback replay, recipient receipt, or exactly-once behavior is implemented.

The candidate 192-byte record layout remains provisional. Product/storage decisions and independent review remain blocking before a file writer is considered.

Phase 1 remains **NOT ACCEPTED**. Production remains **NO-GO**.
