# Broker v2 — randomized adapter stress and worker fault regressions (2026-11-15)

## Scope and safety
All work was performed in:
`D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`

No Production changes, no source-project changes, no commits/resets, no historical evidence deletion, and no changes to IranPriceScout.

## Randomized integrated adapter stress
Added deterministic-seeded per-producer shuffling and small bounded scheduling jitter to `integrated-submit-adapter-v2-multi-native-test.c`. Producer threads submit their disjoint 10-operation ranges in shuffled order. Completion writes to the 70 named pipes are also shuffled with bounded jitter. Each callback remains checked for exactly-once invocation, success, one-byte transfer, and correct payload. Producers exit before the I/O completions are released.

New build runner:
`build-integrated-submit-adapter-v2-randomized-stress.cmd`

- Build: Visual Studio 2026 Build Tools 18.7.3, x64, `/W4 /WX /TC`.
- 10 consecutive full runs passed.
- Each run: 809 assertions; 70 integrated live operations; 7 producers; all producer threads exited before completions.
- Total: 8,090 assertions across 10 runs.
- No failed run; process exit code 0.
- The current Managed Worker uses one worker thread, so callbacks are serialized by design; this test validates exactly-once callback behavior under randomized submission/completion ordering, not parallel callback execution.

## Worker and Reaper fault regressions
All five existing build scripts were run after prechecking their output paths were absent. Each script runs its executable 10 times and cleans its own outputs.

| Test | Per-run result | Runs | Assertions |
|---|---:|---:|---:|
| Managed Worker close failure | 34 PASS | 10 | 340 |
| Issuing-pipe close failure | 36 PASS | 10 | 360 |
| `ERROR_OPERATION_ABORTED` terminal collection | 26 PASS | 10 | 260 |
| Concurrent polling with injected `WAIT_FAILED` | 715 PASS, 0 failures; completed 70/70 | 10 | 7,150 |
| Native wake failure retention | 381 PASS, 0 failures; queued/outstanding/reserved 70; recovery blocked | 10 | 3,810 |

Total across these five fault suites: 11,920 assertions.

Combined with randomized adapter stress: 20,010 assertions across 60 executions.

## Cleanup and environment
- All target executables, object files and the temporary `.build-wakefailure` directory were absent after the scripts completed.
- No pre-existing unrelated artifacts were targeted.
- No relevant Java/TLC or test process remained active at the end of the run.
- No evidence files were deleted.

## Interpretation / remaining gaps
- The randomized native adapter test is a finite stress test, not a proof of all possible interleavings.
- Callbacks are serialized by the current single Managed Worker thread; parallel callback behavior is not claimed.
- Existing worker fault tests demonstrate retention/blocking under injected close/wake/wait failures; they do not prove full recovery from those faults.
- Adapter storage must remain alive and caller entry must be quiesced before successful destroy; concurrent destroy calls must be serialized.
- Full end-to-end integration and acceptance remain outstanding.

Production remains NO-GO. Phase 1 remains NOT ACCEPTED.
