# Durable WAL v1 — Terminal Semantics Policy Probe

Date: 2026-11-17  
Status: **observed behavior only; terminal policy remains unapproved**  
Working Copy: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`

## Scope

Added one test-only source:

- `raya-durable-wal-v1-terminal-policy-probe.c`

No WAL implementation, header, model, project configuration, README, or integration code was changed. The probe constructs correctly encoded and hashed in-memory journals and tests the existing scanner's terminal transition behavior. It does not prescribe product semantics.

## Observed terminal matrix

The probe establishes an operation, records the specified effect result(s), then appends OPERATION_TERMINAL:

| ISSUE state before terminal | HANDOFF state before terminal | Scanner result |
|---|---|---|
| absent | absent | CORRUPT |
| COMMITTED | absent | VALID_COMPLETE |
| PROVEN_NOT_COMMITTED | absent | VALID_COMPLETE |
| UNKNOWN | absent | CORRUPT |
| absent | COMMITTED | VALID_COMPLETE |
| COMMITTED | COMMITTED | VALID_COMPLETE |
| COMMITTED | UNKNOWN | CORRUPT |

The probe also verified that an ISSUE-only COMMITTED path terminalizes operation kinds 1, 2, 3, and 4 under the current implementation.

**Interpretation:** the current transition validator requires at least one effect plane to be determinate (COMMITTED or PROVEN_NOT_COMMITTED), and rejects any still-IN_PROGRESS or UNKNOWN plane. It does not require both ISSUE and HANDOFF to be present or determinate. This is a description of code behavior, not an approval that all operation kinds may terminate after only one effect plane.

## Validation

Compiler: MSVC x64, `/W4 /WX /TC`, linked with `bcrypt.lib`.

- Terminal policy probe: 11 checks/run × 25 runs = **275 assertions**, zero failures.
- Existing semantic-boundary harness: 4,287 checks/run × 25 runs = **107,175 checks**, zero failures.
- Existing regression harness: 594 checks/run × 25 runs = **14,850 checks**, zero failures.
- Existing deterministic malformed-input fuzz harness: 20,000 cases/run × 25 runs = **500,000 cases** and 3,500,000 assertions, zero failures.

The same deterministic fuzz seed was reused for each run. This is not coverage-guided fuzzing and does not establish memory-safety instrumentation.

## Contract decisions still required

Before approving OPERATION_TERMINAL or a persistent WAL format, define:

1. For each operation kind, which external effect planes are required: ISSUE, HANDOFF, recipient/application commit, or a subset.
2. Whether PROVEN_NOT_COMMITTED is a terminal business outcome, a retryable intermediate state, or both depending on operation kind and retry policy.
3. Whether an operation may be terminal when a required effect was never begun.
4. Whether an operation may be terminal after one effect commits while another optional effect is absent.
5. Whether terminal outcomes need an explicit operation-level result (success, rejected, cancelled, failed, uncertain) rather than a Boolean terminal marker.
6. Whether the WAL must retain the final disposition and provenance for every required effect after terminalization.

The current format's OPERATION_TERMINAL record has no operation-level disposition field. The scanner only checks that at least one effect is determinate and no effect is in progress or unresolved UNKNOWN. That is insufficient to encode a complete business outcome policy without a separately approved operation-kind contract.

## Files and hashes

| File | Bytes | SHA-256 |
|---|---:|---|
| `raya-durable-wal-v1-terminal-policy-probe.c` | 2,326 | `646ACD1D763485D83341C916C20D099CCE28EB341D52E0A2D5AADDDAB84C509B` |
| `raya-durable-wal-v1.c` | 10,195 | `E1BD46915B859751DAB20B1E62352A39C72C105B75979DB0DE9CCC3B007159E5` |
| `raya-durable-wal-v1.h` | 2,106 | `CFA50B740F67013171B3199F769F331A5AFECBD95A86A66D77185AABEA2B6DF9` |

## Build artifact handling

Four test executables and five object files (nine generated files total) were built in a dedicated temporary directory. All nine were verified by exact name, size, and SHA-256 against the recorded inventory, then removed. The temporary directory was confirmed absent. No checkpoint, trace, model state, other report, or state-store file was removed.

## Decision

This probe identifies a contract gap; it does not fix it. Do not change the terminal transition implementation until operation-kind semantics are approved. No file writer, persistent WAL, replay, crash recovery, or component integration is authorized by this test.

Phase 1 remains **NOT ACCEPTED**. Production remains **NO-GO**.
