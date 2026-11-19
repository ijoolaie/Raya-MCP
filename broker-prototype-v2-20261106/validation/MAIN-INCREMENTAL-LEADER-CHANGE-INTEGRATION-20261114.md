# Main Incremental Model — Leader Change Integration Check (2026-11-14)

## Objective

Run a test-only derived copy of the main Incremental AppendEntries model with a real modeled leader transition, delayed response messages, and additional follower-prefix invariants. The base model and Production were not modified.

## Derived artifacts

- `WitnessV2LeaderChangeIncrementalIntegration.tla` — derived from `WitnessV2AppendEntriesIncremental.tla`; only the module name and two additional invariants were added.
- `WitnessV2LeaderChangeIncrementalIntegration.cfg` — Nodes={n1,n2,n3}, MaxTerm=2, MaxLog=1, MaxRequestId=1, NodeSymmetry.
- Seven invariants enabled: TypeOK, CommitIndexBound, NextIndexBound, MatchIndexBound, CommittedPrefixAgreement, LeaderMatchWithinFollowerLog, LeaderMatchPrefixAgreement.

The two additional invariants require every leader's per-peer matchIndex to stay within the follower's log length and require the actual log prefixes to agree through matchIndex.

## TLC execution

- TLC version: 2026.11.06.222357, revision d8fb231.
- Java: 25.0.2.
- Process PID: 351672.
- Parsing and semantic processing completed successfully.
- Initial state: 1.
- Last progress: depth 10; 196,963 states generated; 31,901 distinct; 14,518 queued.
- TLC terminated with exit code 1 because writing the disk-backed state pool failed: `There is not enough space on the disk`.
- No invariant violation was reported before the storage failure.
- **Result: INCONCLUSIVE / NOT PASS.** The state graph was not exhausted, so no safety conclusion is drawn.

## Disk and recovery state

- D: free space measured immediately after failure: 827,392 bytes.
- The run created state directory `states\\26-11-14-18-00-13.453`, containing 17 TLC state/fingerprint files, 9,013,345 bytes total.
- This directory is retained as incomplete run/recovery evidence. It was not deleted because it is a checkpoint/state-store and its recoverability has not been established.
- No other files were deleted.
- TLC process count after failure: 0.
- Root `*.tmp.cfg` count: 0.

## Integrity and scope

- Main base model SHA256 remains `C85CF1906D92DF31A62F8DA63392673809D3A314F30012242FB2B6E54B203A57`.
- The derived model is test-only.
- No source project, base model/config, or Production files changed.
- No commits or resets.
- No full Raft, liveness, crash-persistence, or Production acceptance claim.

## Next safe step

Do not repeat the run while D has less than 1 MB free. First resolve disk capacity through an explicitly reviewed cleanup or storage expansion. Do not remove old state stores, traces, reports, or archives as generic cleanup; identify candidates and confirm their role before deletion.
