# Registry-to-WaitPlan Concurrent Append Test (2026-11-07)

Status: focused concurrency test passed; Worker integration remains incomplete.

## Files
- `reaper-waitplan-concurrency-test.c`
- `build-reaper-waitplan-concurrency-test.cmd`

## Coverage
A real producer thread appends an operation/event pair to the registry while an earlier wait-plan snapshot is retained. Registry mutation and wait-plan build/resolve are serialized with the same `CRITICAL_SECTION`.

Assertions verify:
1. The initial wait-plan snapshot is built while holding the shared lock.
2. A producer thread appends a distinct operation/event pair while the snapshot is retained.
3. The original snapshot remains valid for the original operation after append-only mutation.
4. Resolution maps the signaled slot to the exact original operation pointer.
5. A rebuilt snapshot observes the appended entry.
6. The producer wake event remains distinguishable from operation completion.

## Results
- MSVC `/W4 /WX`: PASS.
- Assertions: 10/10 PASS.
- Repeated independent process runs: 25/25 PASS.
- Generated executable and object files: removed; post-test check confirmed absent.
- `git_diff_check`: exit 0.

## Limits
This is not a multi-producer/remover stress test, not a Worker integration test, and not evidence of terminal I/O completion or safe resource reclamation. The single-remover invariant remains mandatory: producers may append while a wait plan is outstanding, but no other thread may remove/free a worker-owned operation/event referenced by that plan.

The existing `reaper-worker-v2.c` still consumes the FIFO queue through `raya_reaper_sync_take`; it does not use the Registry or Wait Plan. Its callback remains synthetic in current tests. No changes were made to `pipe-io-v2.c`, transport integration, Production configuration, or IranPriceScout.
