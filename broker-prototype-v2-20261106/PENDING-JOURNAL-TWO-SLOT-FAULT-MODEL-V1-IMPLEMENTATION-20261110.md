# Pending Journal Two-Slot Store Fault Model v1 — Implementation Report

Date: 2026-11-10
Status: deterministic in-memory fault model implemented; no real filesystem adapter

## Files
- pending-slot-store-model-v1.h/.c
- pending-slot-store-model-v1-test.c
- build-pending-slot-store-model-v1-test.cmd

## Model
The model maintains separate volatile and durable copies of both 256-byte slots. A simulated power loss discards volatile state and restores the durable snapshot. Commit follows select-current -> write inactive -> flush -> readback -> exact byte compare -> slot decode -> publish. Published outputs are left unchanged on uncertain outcomes.

Faults:
- write fails before changing the inactive slot;
- partial write remains volatile and is discarded on power loss;
- partial write is modeled as having reached durable storage before the failed operation returns;
- Flush failure with no durable update;
- Flush reports failure although the new image reached durable storage;
- readback failure after successful flush;
- readback mismatch after successful flush.

Recovery always reads the durable image and uses the production slot selector.

## Verification
- MSVC x64 /W4 /WX: PASS.
- 65 assertions per fresh process.
- 25 fresh-process runs: 1,625 assertions PASS.
- Tests verify successful commit, preserved prior slot, both partial-write persistence outcomes, write failure, both Flush failure outcomes, readback failure/mismatch, fail-closed handling of torn durable inactive slot, one-valid/one-empty recovery, two valid revisions, malformed inactive slot, unprovisioned initialization candidate, and unchanged publication outputs on uncertainty.

## Important observed behavior
A partial inactive-slot write that never reached durable storage is discarded by simulated power loss; the previous valid slot is then selected. If a torn partial image did reach durable storage, the previous slot's bytes remain intact, but the current selector returns ERROR because the other slot is malformed and nonzero. This is consistent with the existing fail-closed rule, but means automatic recovery is intentionally unavailable in that case; a separately authorized recovery procedure is required. The model must not silently fall back to the lower valid slot.

Flush failure and readback failure/mismatch are all reported as UNCERTAIN, never success. After restart, durable state may show either the previous revision or the new revision depending on the modeled persistence outcome. The coordinator must reconcile the selected journal record against the local epoch ledger and authenticated Witness before proceeding.

## Scope limitations
- This is an in-memory deterministic model, not Windows file I/O.
- No real short-write loop, FlushFileBuffers, cache flush, device power loss, process termination, handle identity, file ID, reparse point, DACL, mutex, multi-process contention, or filesystem behavior was exercised.
- The model's durable/volatile boundary is an explicit test abstraction, not a claim about NTFS or hardware.
- No actual journal file, service, ACL, mutex, service SID, or Production integration was created.

Production status: NO-GO.