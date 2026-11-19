# Sync Handoff Ownership Outcome — 2026-11-07

## Finding

The legacy `raya_reaper_sync_handoff` performed the Core ownership transfer before calling `SetEvent`. If the wake signal failed, it returned FALSE even though the operation was already queued and ownership had moved to the worker side. A caller treating FALSE as proof of retained caller ownership could free an operation that remained linked in the queue.

## Change

Files changed:
- `reaper-sync-v2.h`
- `reaper-sync-v2.c`

Added explicit outcomes:
- `RAYA_REAPER_HANDOFF_NOT_TRANSFERRED`: Core handoff did not happen; caller retains ownership.
- `RAYA_REAPER_HANDOFF_TRANSFERRED`: ownership transferred and wake signal succeeded.
- `RAYA_REAPER_HANDOFF_TRANSFERRED_DEGRADED`: ownership transferred, but wake signaling failed; Core is marked DEGRADED and ownership remains transferred.

Added `raya_reaper_sync_handoff_ex` and `raya_reaper_worker_handoff_ex` to return the explicit outcome through both public layers.

Compatibility behavior changed safely:
- `raya_reaper_sync_handoff` now returns TRUE for either transferred outcome.
- It returns FALSE only for NOT_TRANSFERRED.
- Therefore the Boolean no longer reports a post-transfer wake failure as if transfer had not occurred. Callers needing the degraded distinction must use the explicit API and inspect the returned enum.

## Failure-injection test

Added:
- `reaper-sync-handoff-outcome-test.c`
- `build-reaper-sync-handoff-outcome-test.cmd`

The test forces the actual `SetEvent` API call to fail by temporarily substituting `INVALID_HANDLE_VALUE` for the sync wake handle, then restores the original handle for controlled test draining.

Coverage:
1. Explicit API reports TRANSFERRED_DEGRADED after the real SetEvent failure.
2. Operation remains queued, outstanding remains 1, caller_active is 0, and Core is DEGRADED.
3. Compatibility Boolean wrapper returns TRUE after the same post-transfer signal failure.
4. A pre-transfer rejection reports NOT_TRANSFERRED and leaves the operation caller-pending.
5. After test-only controlled draining, no queued/outstanding operation remains. The degraded instances are not falsely marked STOPPED; test teardown closes synchronization resources only after the operation has been drained.

## Results

- MSVC x64 `/W4 /WX`: PASS.
- 25 independent process runs.
- 32 assertions per run; 0 failures.
- 800 assertions total.
- No stderr; build command exit code 0.

## Limits

- This tests the legacy FIFO sync handoff contract, not the integrated Registry/WaitPlan worker.
- The compatibility wrapper reports ownership only; callers requiring wake-health detail must use `raya_reaper_sync_handoff_ex`.
- No actual overlapped I/O or production transport is involved.
- After exposing the explicit outcome through the Worker API, both Sync and Worker regression suites were recompiled independently with MSVC `/W4 /WX` and passed (17 Sync assertions; 16 Worker assertions; 0 failures). Temporary build directories were removed.
- The legacy FIFO Worker still exits on a later wait error after DEGRADED; it is not made production-safe by this ownership-result correction.
- The full 28-group Named Pipe acceptance matrix remains outstanding.

No changes to `pipe-io-v2.c`, Production transport, service provisioning, tunnel, or IranPriceScout. No commit/reset. Production remains **NO-GO**.
