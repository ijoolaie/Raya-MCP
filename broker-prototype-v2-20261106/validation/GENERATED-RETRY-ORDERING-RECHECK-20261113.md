# Generated AppendEntries retry ordering — independent recheck (2026-11-13)

## Environment
- Working copy: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`
- TLC JAR: `D:\Work\project\Raya-MCP\TLAPlus\tla2tools.jar`
- TLC: `2026.11.06.222357` (revision `d8fb231`)
- Java: Oracle JDK `25.0.2`, Windows 11 amd64
- One TLC worker; model and config were not edited.

## Run

```powershell
java -cp 'D:\Work\project\Raya-MCP\TLAPlus\tla2tools.jar' tlc2.TLC -workers 1 -metadir 'validation\generated-retry-ordering-20261113' -config WitnessV2AppendEntriesGeneratedRetryOrdering.cfg WitnessV2AppendEntriesGeneratedRetryOrdering.tla
```

Result: exit `0`; no error found; 16 states generated, 15 distinct, 0 queued; complete graph depth 14; maximum outdegree 2. All nine configured invariants passed: `TypeOK`, `CommittedPrefixPreserved`, `CursorLowerBound`, `FollowerCommitBound`, `FollowerRepairAtSuccess`, `RequestCursorConsistency`, `RequestIdsMonotonic`, `DelayedDuplicateStale`, `SuccessCursor`.

## What this bounded model covers
The staged scenario backs `leaderNext` down through three generated rejection/retry rounds, then accepts the fourth request and advances the success cursor. A delayed duplicate failure response from request 2 is retained across later stages and is dropped because its request ID is no longer the latest request ID. The model asserts cursor lower bound, monotonic request IDs, stage/cursor consistency, and stale duplicate rejection.

## Limits
This is a finite staged witness, not an unrestricted network scheduler or a proof of general retry correctness. It uses a fixed leader/follower, one term, fixed request/response records and a predetermined sequence. It does not establish arbitrary message delivery, elections, higher-term behavior, persistence, liveness, crash recovery or Production readiness.

## Preservation
The source project on `C:\Users\Ali\local-mcp-bridge` and the TLC archive were not modified. No commit, reset, deletion, service change or tunnel change was performed. No Java/TLC process remained after the run.
