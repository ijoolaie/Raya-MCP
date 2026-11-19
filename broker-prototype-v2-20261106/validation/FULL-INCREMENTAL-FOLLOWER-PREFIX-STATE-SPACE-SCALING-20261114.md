# Full Incremental Model — Follower Prefix State-Space Scaling — 2026-11-14

## Purpose

Compare completed bounded TLC runs to identify which state-space dimensions drive cost, and record the limits of the larger RequestId=2 explorations. This is analysis of existing runs; no new TLC exploration was started.

## Controlled comparisons

All runs below used the same derived test module and the same seven invariants: TypeOK, CommitIndexBound, NextIndexBound, MatchIndexBound, CommittedPrefixAgreement, LeaderMatchWithinFollowerLog, and LeaderMatchPrefixAgreement.

### Effect of MaxTerm with MaxLog=1, MaxRequestId=1

| MaxTerm | Generated | Distinct | Depth | Result |
|---:|---:|---:|---:|---|
| 1 | 7,891 | 834 | 11 | PASS |
| 2 | 1,357,030 | 125,734 | 18 | PASS |

Increasing MaxTerm from 1 to 2 increased generated states by approximately 172x and distinct states by approximately 151x.

### Effect of MaxTerm with MaxLog=2, MaxRequestId=1

| MaxTerm | Generated | Distinct | Depth | Result |
|---:|---:|---:|---:|---|
| 1 | 23,938 | 2,516 | 13 | PASS |
| 2 | 6,795,741 | 625,574 | 21 | PASS |

Increasing MaxTerm from 1 to 2 increased generated states by approximately 284x and distinct states by approximately 249x. The MaxTerm expansion is therefore substantial in both completed MaxRequestId=1 comparisons.

### Effect of MaxRequestId at MaxTerm=1, MaxLog=2

| MaxRequestId | Generated | Distinct | Depth | Result |
|---:|---:|---:|---:|---|
| 1 | 23,938 | 2,516 | 13 | PASS |
| 2 | 1,571,005 | 143,322 | 19 | PASS |

Increasing MaxRequestId from 1 to 2 increased generated states by approximately 65.6x and distinct states by approximately 57.0x.

## Larger RequestId=2 explorations

### MaxTerm=2, MaxLog=2, MaxRequestId=2

The run did not complete. It was stopped after repeated orchestration timeouts and resource growth to approximately 4.3 GB working set. No invariant violation was captured, but there is no completion summary. Status remains **INCONCLUSIVE / NOT PASS**.

### MaxTerm=2, MaxLog=1, MaxRequestId=2

The managed one-hour run reached:
- 209,987,820 generated states
- 23,103,563 distinct states
- 8,209,097 states still queued
- depth 17
- approximately 2,386 MB observed working set

It timed out without completing. No invariant violation appeared in captured output, but the state space was not exhausted. Status remains **INCONCLUSIVE / NOT PASS**. The checkpoint at `states/26-11-14-15-56-18.508` is retained.

These incomplete runs must not be described as passing. Their counts are progress snapshots, not complete state-space totals.

## Findings

1. **MaxTerm is a major state-space multiplier.** At MaxRequestId=1, increasing MaxTerm from 1 to 2 multiplied generated states by roughly 172x at MaxLog=1 and 284x at MaxLog=2.
2. **MaxRequestId also has a large cost.** At MaxTerm=1 and MaxLog=2, increasing MaxRequestId from 1 to 2 multiplied generated states by roughly 65.6x.
3. **The combined MaxTerm=2 / MaxRequestId=2 region is currently intractable under the existing execution budget.** Even reducing MaxLog to 1 did not finish within one hour.
4. These are empirical results for this model and these exact bounds. They do not establish a general complexity law, and they do not identify a correctness defect.

## Recommended next step

Do not repeat the identical MaxTerm=2 / MaxRequestId=2 runs. Preserve the current evidence and checkpoint. If further verification is needed, first narrow the property or transition slice under investigation, or run a deliberately smaller focused model that isolates the interaction between term changes and multiple request identifiers. Keep the full-model configurations marked inconclusive until a complete run is obtained.

## Integrity, scope, and cleanup

- Base model SHA256 verified unchanged: `C85CF1906D92DF31A62F8DA63392673809D3A314F30012242FB2B6E54B203A57`.
- Zero TLC processes remained after the audit.
- Zero root-level `*.tmp.cfg` files were present.
- The existing checkpoint was present and retained.
- No source project, base model, base config, or Production files were changed.
- No test evidence was deleted.
- No commits or resets.

## Startup-only TLC state-store cleanup (2026-11-15)

Removed three `FollowerPrefixCheck` state directories: `26-11-14-13-52-13.319`, `26-11-14-13-51-08.842`, and `26-11-14-13-49-30.933`. Each contained exactly four files: one model `.st` file of 24 KiB, 8 KiB, and 8 KiB respectively, plus two zero-byte fingerprint files; none contained numeric state-pool files or checkpoint/recovery files. These were initialization-only artifacts with no explored-state evidence and were not resumable. No model, config, report evidence, trace, checkpoint, or archive files were removed.
