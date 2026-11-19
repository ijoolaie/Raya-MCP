# Full Incremental Model — MaxTerm 2 / MaxLog 1 / MaxRequestId 2 — 2026-11-14

## Objective
Determine whether reducing MaxLog from 2 to 1 makes the full incremental model tractable with MaxTerm=2 and MaxRequestId=2.

## Configuration
`WitnessV2AppendEntriesIncrementalFollowerPrefixCheck-MaxTerm2-MaxLog1-Req2.cfg`

Nodes={n1,n2,n3}; MaxTerm=2; MaxLog=1; MaxRequestId=2; SYMMETRY NodeSymmetry. All seven invariants were enabled: TypeOK, CommitIndexBound, NextIndexBound, MatchIndexBound, CommittedPrefixAgreement, LeaderMatchWithinFollowerLog, LeaderMatchPrefixAgreement.

## Final execution status
**INCONCLUSIVE — TIME LIMIT, NOT PASS.**

- Managed session: `ce8d7e10-d892-42b8-963e-da018d6fe99b`
- PID: 349044
- Started: 2026-11-14T12:26:18.344Z
- Ended: 2026-11-14T13:26:19.203Z
- Managed timeout: 3,600,000 ms (one hour)
- Exit code: 1 (managed process timed out; no TLC completion summary was produced)
- Last reported depth: 17
- States generated: 209,987,820
- Distinct states: 23,103,563
- States left on queue: 8,209,097
- Latest observed working set before timeout: approximately 2,386 MB
- No invariant violation was present in the captured output. This is not evidence that the full state space passed; exploration did not complete.

## Checkpoint and cleanup
A TLC checkpoint directory exists at `states/26-11-14-15-56-18.508`. It is retained as recovery/evidence; it has not been removed. Post-run checks found zero remaining TLC processes and zero root-level `*.tmp.cfg` files. No test traces or other evidence were deleted.

## Integrity and boundaries
The base model `WitnessV2AppendEntriesIncremental.tla` SHA256 remains:
`C85CF1906D92DF31A62F8DA63392673809D3A314F30012242FB2B6E54B203A57`

Only the dedicated test configuration and validation report belong to this test addition. The derived test module and all prior evidence remain unchanged. No source-project or Production changes; no commits or resets.

## Conclusion
MaxTerm=2 / MaxLog=1 / MaxRequestId=2 is too expensive to complete within the managed one-hour execution window on this run. This does not demonstrate an invariant failure or a pass. Keep it marked **INCONCLUSIVE**. The next bounded comparison should reduce one more dimension (e.g. MaxTerm=1 / MaxLog=1 / MaxRequestId=2 is already covered by the existing successful run), or explicitly narrow the model/question before attempting another large exploration. Do not restart this identical configuration.
