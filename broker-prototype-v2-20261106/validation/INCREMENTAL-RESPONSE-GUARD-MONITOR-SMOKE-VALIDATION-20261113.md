# Incremental AppendEntries Response Guard Monitor — Smoke Validation

Date: 2026-11-13
Working copy: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`
TLC: `2026.11.06.222357` (revision `d8fb231`)
Java: `25.0.2`

## Scope

This is a bounded smoke validation of the abstract `WitnessV2AppendEntriesIncrementalResponseGuardMonitor` model. It checks the response-acceptance monitor and three guard-removal mutants. It is not a full Raft proof, does not validate election implementation, and does not establish production readiness. `AssumeLeader` remains an abstract environment action.

## Model adjustments

- Parenthesized the success/failure `IF` expression inside `HandleAppendResponse` so the response-acceptance ghost assignments are visibly outside that branch.
- Made the three ghost-flag updates explicit with `IF` expressions:
  - `badRoleAcceptance'`
  - `badSourceAcceptance'`
  - `badDestinationAcceptance'`
- Added a separate smoke config with `Nodes={n1,n2,n3}`, `MaxTerm=1`, `MaxLog=1`, `MaxRequestId=1`, `NodeSymmetry`, and `MessageBound`.
- The role-guard mutant uses `MaxTerm=2` (with `MaxLog=1`, `MaxRequestId=1`) so the trace can include a term change and demotion before the delayed response is handled.

## TLC results

| Run | Configuration | Result | Generated | Distinct | Queue at stop | Depth |
|---|---|---:|---:|---:|---:|---:|
| Corrected baseline | MaxTerm=1, MaxLog=1, MaxRequestId=1 | PASS | 6,642 | 730 | 0 | 11 |
| Remove source-online guard | MaxTerm=1, MaxLog=1, MaxRequestId=1 | Expected invariant violation | 1,278 | 220 | 88 | 7 |
| Remove destination-online guard | MaxTerm=1, MaxLog=1, MaxRequestId=1 | Expected invariant violation | 1,166 | 214 | 94 | 8 |
| Remove role guard | MaxTerm=2, MaxLog=1, MaxRequestId=1 | Expected invariant violation | 4,162 | 1,042 | 662 | 6 |

The baseline explored its complete reachable state graph (zero queue). The mutant runs stopped on counterexamples; their nonzero queue counts are expected and do not indicate incomplete counterexample detection.

### Counterexample traces

- Source offline guard removed: `WitnessV2AppendEntriesIncrementalResponseGuardMonitorWithoutSourceOnlineGuardSmoke_TTrace_1794594012.tla` and `.bin`.
- Destination offline guard removed: `WitnessV2AppendEntriesIncrementalResponseGuardMonitorWithoutDestinationOnlineGuardSmoke_TTrace_1794594019.tla` and `.bin`.
- Role guard removed: `WitnessV2AppendEntriesIncrementalResponseGuardMonitorWithoutRoleGuardSmoke_TTrace_1794594035.tla` and `.bin`.

Each mutant violates `ResponseAcceptanceGuards` by accepting an AppendEntries response while its corresponding precondition is false.

## Full-model status

The earlier unbounded monitor run timed out and is not a pass. The earlier bounded run also has no recoverable completion record in the current working copy; its old metadata is retained, but there is no authoritative TLC completion summary. The smoke results above do not replace that full-model validation.

## Files

Updated:
- `WitnessV2AppendEntriesIncrementalResponseGuardMonitor.tla`

Added:
- `WitnessV2AppendEntriesIncrementalResponseGuardMonitor.Smoke.cfg`
- Three `...Without*GuardSmoke.tla` mutant modules and their smoke configs
- `WitnessV2AppendEntriesIncrementalResponseGuardMonitorWithoutRoleGuardSmoke.Term2.cfg`
- This validation report
- Separate TLC metadata directories and mutant counterexample traces

No files were deleted. No source project, production deployment, or unrelated project was changed. No commit or reset was performed.

## Acceptance status

**Smoke validation: PASS for baseline; all three guard-removal mutants detected.**

**Full-model validation: NOT COMPLETE.**

**Production: NO-GO. Phase 1: NOT ACCEPTED.**
## Additional term-change-bound baseline

A second corrected baseline was run with `MaxTerm=2`, `MaxLog=1`, and `MaxRequestId=1` to match the role-guard mutant's term bound. TLC completed successfully: **279,132 generated states, 29,462 distinct states, 0 queued states, depth 18**. Runtime was approximately 2 seconds. Configuration: `WitnessV2AppendEntriesIncrementalResponseGuardMonitor.Smoke.Term2.cfg`.
## Log-bound-2 validation (MaxTerm=2, MaxLog=2, MaxRequestId=1)

Using `WitnessV2AppendEntriesIncrementalResponseGuardMonitor.Smoke.Log2.cfg`, the corrected baseline completed the full reachable graph: **1,253,643 generated states, 132,050 distinct states, 0 queued states, depth 21** (about 15 seconds).

All three guard-removal mutants were also detected at this larger log bound:

| Mutant | Result | Generated | Distinct | Queue at counterexample | Depth |
|---|---:|---:|---:|---:|---:|
| Source-online guard removed | Expected `ResponseAcceptanceGuards` violation (TLC exit 12) | 5,219 | 1,408 | 941 | 7 |
| Destination-online guard removed | Expected `ResponseAcceptanceGuards` violation (TLC exit 12) | 5,159 | 1,388 | 926 | 7 |
| Leader-role guard removed | Expected `ResponseAcceptanceGuards` violation (TLC exit 12) | 3,740 | 1,029 | 688 | 7 |

The nonzero queues are expected because TLC stops on the first counterexample.

Counterexamples:
- `WitnessV2AppendEntriesIncrementalResponseGuardMonitorWithoutSourceOnlineGuardSmoke_TTrace_1794594211.tla`
- `WitnessV2AppendEntriesIncrementalResponseGuardMonitorWithoutDestinationOnlineGuardSmoke_TTrace_1794594222.tla`
- `WitnessV2AppendEntriesIncrementalResponseGuardMonitorWithoutRoleGuardSmoke_TTrace_1794594233.tla`

This increases the bounded evidence for the three guards, but it still does not complete the original full-bound run (`MaxTerm=2`, `MaxLog=2`, `MaxRequestId=3`).
## Request-ID bound 2 final result

The `MaxTerm=2`, `MaxLog=2`, `MaxRequestId=2` baseline subsequently completed successfully using four TLC workers. TLC reported **23,002,530 generated states, 2,326,024 distinct states, 0 queued states, depth 31**, exit code 0, and runtime of **1 minute 12 seconds**. No invariant error was found. Metadata: `monitor-smoke-requestid2-meta-20261113`.

This is the largest completed monitor baseline in this validation sequence. The original full bound (`MaxTerm=2`, `MaxLog=2`, `MaxRequestId=3`) is still NOT COMPLETE; no full-bound pass is claimed.\n\n## Controlled Request-ID bound 3 trial (2026-11-13)\n\nA resource-capped trial was started against `WitnessV2AppendEntriesIncrementalResponseGuardMonitorBounded.cfg` in the working copy, using Java 25.0.2, TLC 2026.11.06.222357, `-Xmx4g`, and 2 workers. The managed run had a 180-second timeout. It was automatically stopped at the timeout; this is a timeout, not a TLC invariant failure and not a successful completion.\n\nLast progress line before timeout: **17,308,302 generated states, 2,163,772 distinct states, 551,438 queued states, depth 21**. No invariant violation had been reported before the run was stopped. At the last resource check, the Java process used approximately **0.86 GiB private memory**, system free physical memory was **11.06 GiB**, and D: free space was **5.50 GiB**.\n\nTLC metadata directory: `validation\response-guard-requestid3-controlled-20261113`. Inspection found no checkpoint/recovery marker; the default checkpoint interval is 30 minutes, while this run ended after 3 minutes. The partial state-store/queue metadata was therefore not a resumable checkpoint. After verifying that no related TLC process remained active, the abandoned run's exact metadata subdirectory was removed: **182 files, 182.98 MiB**. This was the only cleanup performed. The parent directory remains. The original full bound remains **NOT COMPLETE**; the partial progress must not be represented as a pass or as exhaustive invariant validation.

The bounded trial changed no model/config source files. The cleanup removed only the abandoned, non-recoverable run metadata created by this trial. No model/config, trace evidence, other validation artifacts, archive, source project, or Production files were deleted or changed. No commit/reset was performed.

## Checkpointed Request-ID bound 3 continuation (2026-11-13)

A second controlled run used the same original bounded configuration, Java 25.0.2, TLC 2026.11.06.222357, `-Xmx4g`, 2 workers, and `-checkpoint 1`. It ran for 180 seconds and timed out, but TLC explicitly reported a completed checkpoint. Initial progress reached 16,998,304 generated states, 2,124,845 distinct states, 540,988 queued states, depth 21. The checkpoint directory is `validation\response-guard-requestid3-checkpointed-20261113\26-11-13-23-50-58.595`.

A first recovery attempt omitted the original seed/fingerprint; TLC warned that the settings differed. That process was terminated immediately and its output is not treated as valid evidence. Recovery was then repeated with the exact original values: seed `7826000411190093263`, fingerprint `21`, and 2 workers. TLC reported `Recovery completed. 2124249 states examined. 540808 states on queue.` No settings mismatch warning appeared in this correctly matched recovery.

The correctly matched continuation ran for a further 180 seconds and reached progress depth 24: **20,749,801 generated states, 4,231,969 distinct states, 869,766 queued states**. It timed out again; no invariant violation was reported before timeout. The latest checkpoint was retained. Metadata currently contains **229 files, 243.61 MiB**. At the final resource check, free physical memory was **10.54 GiB** and free D: space was **5.37 GiB**. No related Java process remains active.

This is partial progress only. It is not a completed model check, not an exhaustive invariant pass, and not a full Raft proof. The checkpoint metadata is intentionally retained because it contains the recovery state.

## Further Request-ID bound 3 checkpoint continuation (2026-11-14)

The checkpoint was recovered again with the exact original seed `7826000411190093263`, fingerprint `21`, and 2 workers. TLC confirmed recovery of **6,193,065 states examined, 1,025,052 states on queue**. The next 180-second managed run timed out after recording a completed checkpoint at 00:07:21. Its last progress line was depth 27: **25,333,633 generated states, 8,096,605 distinct states, 1,045,474 queued states**. No invariant violation was reported before timeout.

The retained metadata now contains **237 files, 300.86 MiB**. The checkpoint files `queue.chkpt`, `vars.chkpt`, and `settings.chkpt` are present. No related Java process remains active; free D: space is **5.32 GiB**. All files in this run directory are retained because the state store and checkpoint are required for further recovery. No disposable files were identified in this run directory.

This remains partial progress only: no exhaustive pass or full Raft proof is claimed.

## Additional checkpoint continuation (2026-11-14)

A further recovery used the same checkpoint with the matching seed `7826000411190093263`, fingerprint `21`, and 2 workers. TLC confirmed recovery of **8,096,040 states examined, 1,045,417 states on queue**. The 180-second continuation timed out after TLC completed a new checkpoint at 00:11:58. Last progress: depth 29, **27,775,697 generated states, 9,896,101 distinct states, 874,330 queued states**. No invariant violation was reported.

Post-run inspection confirmed no related Java process remains active. The current checkpoint directory contains **210 files, 303.13 MiB**, including `queue.chkpt` (1,593,684 bytes), `vars.chkpt` (18,020 bytes), and `settings.chkpt` (86 bytes). Free physical memory was **10.89 GiB** and free D: space **5.31 GiB**. TLC's own checkpoint rotation changed the metadata file count; no manual deletion was performed because the remaining files are part of the recoverable state store.

The original Request-ID bound 3 remains **NOT COMPLETE**. The checkpoint is retained for a later exact-parameter recovery.
## Request-ID bound 3 completed model check (2026-11-14)

The exact checkpoint recovery completed successfully using TLC 2026.11.06.222357, Java 25.0.2, `-Xmx4g`, 2 workers, checkpoint interval 1 minute, seed `7826000411190093263`, fingerprint `21`, the original bounded configuration, and the original model. TLC confirmed recovery of **9,895,489 states examined, 874,512 states on queue**. The run then completed without an invariant error: **40,790,504 states generated, 12,232,006 distinct states, 0 states left on queue, complete graph depth 39**, exit code **0**, runtime **2 minutes 56 seconds**. The TLC-reported optimistic fingerprint collision probability was `1.9E-5` (actual-fingerprint estimate `8.7E-6`).

This completes exhaustive state exploration for this specific finite configuration (`Nodes={n1,n2,n3}`, `MaxTerm=2`, `MaxLog=2`, `MaxRequestId=3`, symmetry reduction, and `MessageBound`). It is a bounded model-checking result for the response-guard monitor, not a proof of unbounded behavior, not a full Raft proof, and not a production acceptance.

After completion, no related Java process remained active. The TLC run directory contains **7 files totaling 256,862,784 bytes**; the retained files are the model state stores, checkpoint state-store files, and fingerprint stores. TLC no longer leaves the active `queue.chkpt`, `vars.chkpt`, or `settings.chkpt` recovery triplet after the completed search. No files were manually deleted. Free physical memory was approximately **10.56 GiB** and free D: space approximately **5.37 GiB**. No source/config or Production files were changed; no commit/reset was performed.



## Request-ID bound 3 guard-removal mutant controls (2026-11-14)

The three existing guard-removal mutant modules were each checked against the same finite bounds as the completed Request-ID=3 baseline: `Nodes={n1,n2,n3}`, `MaxTerm=2`, `MaxLog=2`, `MaxRequestId=3`, `NodeSymmetry`, and `MessageBound`. TLC 2026.11.06.222357 / Java 25.0.2, two workers, `-Xmx2g`.

| Mutant | TLC result | Generated | Distinct | Queue at counterexample | Depth | Exit |
|---|---|---:|---:|---:|---:|---:|
| Leader-role guard removed | Expected `ResponseAcceptanceGuards` violation | 5,032 | 1,350 | 912 | 7 | 12 |
| Source-online guard removed | Expected `ResponseAcceptanceGuards` violation | 5,512 | 1,460 | 979 | 7 | 12 |
| Destination-online guard removed | Expected `ResponseAcceptanceGuards` violation | 5,567 | 1,480 | 995 | 7 | 12 |

All three negative controls were detected. The traces end with `responseAccepted=TRUE` and the corresponding bad ghost flag set to TRUE. Nonzero queue counts are expected because TLC stops at the first invariant counterexample. Trace files: `WitnessV2AppendEntriesIncrementalResponseGuardMonitorWithoutRoleGuardSmoke_TTrace_1794632261.tla`, `...WithoutSourceOnlineGuardSmoke_TTrace_1794632265.tla`, and `...WithoutDestinationOnlineGuardSmoke_TTrace_1794632270.tla` (TLC also produced corresponding binary trace files).

Three temporary Request-ID=3 mutant config files were removed after the runs. The model modules, generated counterexample traces, and per-run TLC metadata directories were retained. No model source, baseline config, source project, Production files, or archive was changed. No commit/reset was performed.

Interpretation: the Request-ID=3 baseline completed its finite state exploration, and all three individual guard-removal mutants are detected under the identical finite bounds. This remains bounded monitor evidence, not a full Raft proof or Production acceptance.

## Report reconciliation and artifact-location verification (2026-11-14)

This section supersedes earlier interim statements in this report that the original Request-ID=3 full bound was `NOT COMPLETE`. The exact checkpoint recovery later completed with exit code 0, 40,790,504 generated states, 12,232,006 distinct states, zero queued states, and depth 39, as documented above. The earlier timeout and checkpoint progress entries remain as historical execution records, not current status.

The three Request-ID=3 mutant counterexample `.tla` and `.bin` files were verified in the project root (the working-copy root), not inside their respective per-run metadata directories. The three metadata directories are separate and retained. The final states of the text traces were inspected directly: the role mutant ends with `responseAccepted=TRUE` and `badRoleAcceptance=TRUE`; the source mutant ends with `responseAccepted=TRUE` and `badSourceAcceptance=TRUE`; the destination mutant ends with `responseAccepted=TRUE` and `badDestinationAcceptance=TRUE`. This confirms each mutant's expected guard-specific invariant violation.

The stale intermediate `Report SHA256` line was removed because later additions had changed the report bytes. No checksum is embedded in this report; the current SHA-256 is recorded separately in the execution handoff. No model/config, source project, archive, or Production files were changed. No commit/reset was performed.

## TLC metadata cleanup addendum (2026-11-15)

The abandoned bounded Request-ID=3 run metadata at `monitor-bounded-meta-20261113\26-11-13-17-46-31.982` was removed after verifying that no TLC process was active and that the exact run directory contained 100 files (91,782,509 bytes) but no checkpoint/recovery files. This was an incomplete, non-resumable run; its partial progress is not treated as a completed result. The parent metadata directory remains. No model, configuration, trace, report evidence, checkpoint, archive, source project, or Production file was removed or changed. No commit or reset was performed.

## Legacy incomplete monitor state-store cleanup (2026-11-15)

The legacy state store `states\26-11-13-17-40-11.157` was removed after reconciling its creation time with the original monitor model/config (`WitnessV2AppendEntriesIncrementalResponseGuardMonitor.cfg`, created before the run; `MaxTerm=2`, `MaxLog=2`, `MaxRequestId=3`, without symmetry reduction or `MessageBound`). The directory contained 229 files totaling 198,028,637 bytes, including 225 sequential numeric state-pool files and model state-store files. It had no checkpoint/recovery files. The matching report records the earlier monitor run as timed out/incomplete; later bounded monitor validation completed with symmetry and `MessageBound`, so this uncheckpointed state pool was neither resumable nor needed for the completed bounded result. No model, configuration, trace, report history, checkpoint, archive, source project, or Production file was removed or changed. No commit/reset was performed.
