# Managed Worker Wake Failure — Adapter Integration Regression

Date: 2026-11-16  
Scope: Working copy only — `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`  
Production: untouched. No commit, reset, TLC run, or source-project edit.

## Purpose

Exercise an actual overlapped Pipe I/O submission through Submitter → Adapter → Managed Worker, then inject failure of the Managed Worker's own `SetEvent(wake_event)` after ownership has transferred. This closes the gap left by the earlier Integrated Reaper-only wake-failure test.

## Test-only seam

- Added `raya_managed_set_event_fn` and `raya_managed_worker_set_wake_callback` under `RAYA_ADAPTER_TESTING` only.
- The callback can be configured only while the Worker is accepting and has no caller, queued, outstanding, reserved, or registry work.
- Worker wake signaling routes through one helper. Without `RAYA_ADAPTER_TESTING`, that helper calls Win32 `SetEvent` directly; the production struct layout and public API remain unchanged.
- The test-only retained snapshot now permits a joined Worker plus a quiescent Submitter (no queued requests, active producers, or active issue). Issuer leases may remain outstanding. Callers must quiesce direct Submitter API users while taking this diagnostic snapshot.
- The test Build ran in a unique `%TEMP%` directory. It did not invoke the project's cleanup runner and did not touch pre-existing `.obj` files.

## Reproduction and observed state

A named-pipe overlapped `ReadFile` is accepted (`ERROR_IO_PENDING`). The Managed Worker wake callback then fails exactly once for its own wake event.

Observed and asserted:

- `raya_managed_worker_handoff` has transferred the operation; result is `TRANSFERRED_DEGRADED` internally and the Adapter publish callback returns success to the Submitter.
- Worker lifecycle becomes `DEGRADED` with `RAYA_RECOVERY_CAUSE_WAKE_SIGNAL`.
- The Worker thread remains blocked after the failed wake. A test-only external `SetEvent` is required to let it observe `DEGRADED` and exit with `ERROR_INVALID_STATE`. This external kick is only test cleanup/observation, not a product recovery mechanism.
- Queue/outstanding/reserved counts remain `1/1/1`; caller-active is zero.
- The operation remains the queue head in `RAYA_REAPER_OP_QUEUED`; Owner is `RAYA_PIPE_OWNER_REAPER_QUEUED`; reservation is `RAYA_INTEGRATED_RESERVATION_HANDED_OFF`.
- Registry count is zero because promotion never occurred.
- Adapter Retained and Worker Retained lists are both empty, but Adapter `active_count` remains 1. The test-only snapshot confirms zero entries in both Retained lists.
- Submitter has no queued request, active producer, or active issue, but remains `RUNNING` with one outstanding issuer lease. It cannot receive the completion acknowledgement because the Worker did not collect the I/O.
- The operation buffer remains non-null with length 1; the duplicated issuing-pipe handle and completion-event handle remain valid; `OVERLAPPED.hEvent` matches the registered completion event.
- User completion callback count remains zero.
- The test deliberately does not release the in-flight operation, envelope, buffer, or handles. They remain retained by the live queue/envelope ownership graph until process exit; no unsafe cleanup is claimed.

## Validation

Toolchain: Visual Studio 2026 Build Tools 18.7.3, x64, C compiler `/W4 /WX /TC`.

| Build configuration | Executions | Assertions per execution | Total | Result |
|---|---:|---:|---:|---|
| `RAYA_ADAPTER_TESTING` | 10 | 854 | 8,540 | PASS |
| Baseline (macro absent) | 10 | 724 | 7,240 | PASS |

Both configurations compiled successfully. The test-enabled configuration reported 68 cases; baseline reported 66. The baseline build confirms the test seam is excluded cleanly when the macro is absent.

## Conclusion / remaining defect

The test reproduces a real ownership-tracking gap. A transferred-but-degraded operation remains queued and its resources remain alive, but it is absent from both Retained lists. The Adapter still counts it as active, and the Submitter retains an issuer lease. In addition, a failed wake does not itself wake the Worker so it can observe `DEGRADED`; without an independent signal, the Worker may remain blocked indefinitely.

This is a confirmed regression finding, **not a fix**. Do not destroy the Adapter/Worker/Submitter, free the envelope, release the reservation, or replay/acknowledge the operation. Next work must design an explicit transferred-but-unprocessed quarantine/retention path and a safe wake-failure observation mechanism, then test exact reconciliation. Production remains NO-GO.

## Files changed

- `reaper-managed-worker-v2.c` — centralized wake signaling and test-only callback seam.
- `reaper-managed-worker-v2.h` — test-only callback type, fields, and setter declaration.
- `integrated-submit-adapter-v2.c` — test-only snapshot quiescence condition.
- `integrated-submit-adapter-v2.h` — test-only snapshot contract clarification.
- `integrated-submit-adapter-v2-api-test.c` — end-to-end wake-failure regression and resource-lifetime assertions.
- `validation/MANAGED-WORKER-WAKE-FAILURE-ADAPTER-INTEGRATION-20261116.md` — this report.

No existing evidence or pre-existing object file was deleted.