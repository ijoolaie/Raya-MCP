# Full Incremental Model — Leader Match Index / Follower Prefix Check — 2026-11-14

## Purpose
Extend the focused DeliverAppend test to the actual multi-node state model and check whether a leader's per-follower matchIndex remains bounded by, and refers to, the current follower log prefix.

## Test construction
- Test module: `WitnessV2AppendEntriesIncrementalFollowerPrefixCheck.tla`
- Derived from `WitnessV2AppendEntriesIncremental.tla` in the working copy.
- Only module identity and two test invariants were added; transition operators remain copied from the base model.
- No changes were made to the base model.

Added invariants:
```tla
LeaderMatchWithinFollowerLog ==
  \A n, p \in Nodes :
    role[n] = Leader => matchIndex[n][p] <= Len(log[p])

LeaderMatchPrefixAgreement ==
  \A n, p \in Nodes :
    role[n] = Leader =>
      IF matchIndex[n][p] = 0 THEN TRUE
      ELSE SubSeq(log[n], 1, matchIndex[n][p]) =
           SubSeq(log[p], 1, matchIndex[n][p])
```

The first checks that matchIndex never extends beyond the follower's current log length. The second is stronger: the prefix through matchIndex must be identical between the leader and follower.

## TLC results

Both runs used three nodes, NodeSymmetry, and the full transition relation from the incremental model. Both invariants above were active in both runs.

### Run A
Config: `WitnessV2AppendEntriesIncrementalFollowerPrefixCheck.cfg`
- MaxTerm=1, MaxLog=1, MaxRequestId=1
- TLC exit 0; complete graph; no invariant violation
- 7,891 states generated
- 834 distinct states
- 0 states left on queue
- Complete graph depth 11

### Run B
Config: `WitnessV2AppendEntriesIncrementalFollowerPrefixCheck-Term2.cfg`
- MaxTerm=2, MaxLog=1, MaxRequestId=1
- TLC exit 0; complete graph; no invariant violation
- 1,357,030 states generated
- 125,734 distinct states
- 0 states left on queue
- Complete graph depth 18

In both runs, the configured invariants also passed: TypeOK, CommitIndexBound, NextIndexBound, MatchIndexBound, CommittedPrefixAgreement, LeaderMatchWithinFollowerLog, and LeaderMatchPrefixAgreement.

## Interpretation
Within these bounded executions, a leader's recorded matchIndex remained both within the follower's current log length and consistent with the actual log prefix. The stronger prefix-equality invariant is the relevant evidence: a mere length bound would not detect a follower log with different entries at already acknowledged indices.

This is bounded model-checking evidence only. It does not prove unbounded safety or production behavior. The model still uses abstract `AssumeLeader` rather than a full election protocol; it does not model disk persistence, crash/restart, real transport, or every implementation-level event. The existing simplified response handling remains unchanged. In particular, this test does not establish full Raft correctness or Production acceptance.

## Artifacts
- `WitnessV2AppendEntriesIncrementalFollowerPrefixCheck.tla`
- `WitnessV2AppendEntriesIncrementalFollowerPrefixCheck.cfg`
- `WitnessV2AppendEntriesIncrementalFollowerPrefixCheck-Term2.cfg`
- this report: `validation/FULL-INCREMENTAL-LEADER-MATCH-FOLLOWER-PREFIX-CHECK-20261114.md`

## Change and cleanup boundary
Only the test module, its two configs, and this report were added/changed in the working copy. The original `WitnessV2AppendEntriesIncremental.tla` and its base config were not edited. No source project or Production files changed. No commits or resets. No test trace was produced by the successful runs, so no evidence files were removed.
