# Generated mixed AppendEntries responses — independent recheck (2026-11-13)

## Environment
- Working copy: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`
- TLC JAR: `D:\Work\project\Raya-MCP\TLAPlus\tla2tools.jar`
- TLC: `2026.11.06.222357` (revision `d8fb231`)
- Java: Oracle JDK `25.0.2`, Windows 11 amd64
- One TLC worker; no source model/config was edited for these runs.

## Guarded run
Command:

```powershell
java -cp 'D:\Work\project\Raya-MCP\TLAPlus\tla2tools.jar' tlc2.TLC -workers 1 -metadir 'validation\mixed-responses-guarded-20261113' -config WitnessV2AppendEntriesGeneratedMixedResponses.cfg WitnessV2AppendEntriesGeneratedMixedResponses.tla
```

Result: exit `0`; no error found; 9 states generated, 8 distinct, 0 queued; complete graph depth 7; maximum outdegree 2. The configuration checks these 11 invariants: `TypeOK`, `CommittedPrefixPreserved`, `FollowerCommitBound`, `CursorLowerBound`, `MatchWithinLeaderLog`, `NextWithinLeaderLog`, `RequestsUseSameBaseCursor`, `FollowerAcceptedFirst`, `FollowerCommitAdvanced`, `BothResponsesGenerated`, `AllResponsesHandled`.

## Unguarded diagnostic
Command:

```powershell
java -cp 'D:\Work\project\Raya-MCP\TLAPlus\tla2tools.jar' tlc2.TLC -workers 1 -metadir 'validation\mixed-responses-unguarded-diagnostic-20261113' -config WitnessV2AppendEntriesGeneratedMixedResponses-Diagnostic.cfg WitnessV2AppendEntriesGeneratedMixedResponses.tla
```

Result: expected TLC invariant failure, process exit `12`; the harness wrapper treated exactly exit `12` as the expected diagnostic outcome. `CursorLowerBound` is violated. The counterexample handles the current failure response before the older success: `leaderNext` decreases from 2 to 1 while `leaderMatch` remains 1. TLC generated trace files `WitnessV2AppendEntriesGeneratedMixedResponses_TTrace_1794555898.tla` and `.bin` in the working directory.

## Interpretation and limits
This recheck reproduces the bounded counterexample to unconditional decrement and the guarded model's finite-state pass. It does not prove that this guard is the only valid implementation, nor does it establish full Raft correctness, liveness, crash recovery, persistence, arbitrary delivery schedules, election correctness, or Production readiness. The model remains a fixed leader/follower, one-term, two-request scenario.

## Preservation
The source project on `C:\Users\Ali\local-mcp-bridge` and the TLC archive were not modified by these runs. No commit, reset, deletion, service change, or tunnel change was performed. No TLC/Java process remained after the runs.
