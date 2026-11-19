# Managed Worker lock-order and critical-section hold-path audit — 2026-11-16

## Scope

Read-only static audit of `integrated-submit-adapter-v2.c`, `reaper-managed-worker-v2.c`, `dedicated-io-submitter-v2.c`, and `reaper-integrated-v2.c` in the working copy. No code was changed and no build or runtime test was performed.

## Lock inventory and ownership

- Adapter lock protects Adapter `active_count` and Adapter Retained list/count. `adapter_submit_impl` increments `active_count` under a short lock and performs allocation, handle creation/duplication, admission and enqueue outside it (adapter source lines 332–341 and 343 onward).
- Worker lock protects integrated Reaper lifecycle, queue, registry, reservations, Worker Retained list and callback configuration. Public admit/reserve/finish methods acquire it only around core state changes (worker source lines 165–195).
- Submitter lock protects submitter queue, request state, producer/issuer counters and lifecycle. Admit/enqueue/ack paths update state under lock; issue and publish callbacks execute outside Submitter lock (submitter source lines 80–87 and 115–116). Some injected Win32 API event operations (`set_event`, `reset_event`) execute while Submitter lock is held (e.g. lines 219–220, 233–234, 246–249, 278).

## Lock ordering findings

1. Production `retain_envelope` acquires Adapter lock, releases it, then acquires Worker lock; it does not nest those two locks (Adapter source lines 116–139).
2. `worker_finish_once` acquires only Worker lock (lines 88–109).
3. The normal release callback is invoked after Worker lock has been released (Worker source lines 432–444); it may then enter Adapter/Submitter paths without holding Worker lock.
4. Submitter `publish` and `not_issued` callbacks are invoked outside Submitter lock (submitter source lines 87, 115 and 103–105). The callback may acquire Worker or Adapter lock without a Submitter→Worker/Adapter nested edge.
5. The test-only transferred-degraded quarantine hook deliberately nests Worker→Adapter while the Worker handoff lock is held (worker source lines 218–224). This hook and its callback are compiled only with `RAYA_ADAPTER_TESTING`; it is not the production lock graph. The production `retain_envelope` path does not nest Adapter→Worker, so the inspected production paths show no reverse nested edge.
6. The read-only retained snapshot locks Submitter, Adapter, Worker and Submitter sequentially, never simultaneously (Adapter source lines 536–616).

No production ABBA cycle was found in the inspected call paths. This is a scoped source audit, not a whole-program proof; external callbacks supplied through API seams and uninspected callers could add edges.

## Potential hold-time contributors

- Worker lock is held while promoting all queued operations and building a wait plan (Worker source lines 261–304). The wait plan is bounded by the configured wait-set limit (up to 63 operation events plus wake event), but work grows with the current registry/queue population.
- Worker lock is also held while ResetEvent/SetEvent is called and during core queue/registry transitions. Production SetEvent is a Win32 call; test builds may substitute a callback.
- Submitter lock is held around event API calls in enqueue, abort, acknowledge and shutdown. The default APIs are Win32 event calls; a slow/reentrant custom API callback could extend hold time or introduce lock edges.
- Adapter lock critical sections are short list/count mutations; allocation, pipe I/O setup, handle operations, finalization and callbacks occur outside Adapter lock in inspected paths.
- Completion processing (`GetOverlappedResult`, finalizer close, owner release and user release callback) is performed outside Worker lock; only lifecycle/registry transitions are protected.

## Conclusions and limits

- No production nested lock-order inversion was identified in the inspected four-module call graph.
- The only explicit Worker→Adapter nested edge found is the test-only quarantine hook.
- Potential contention hotspots are Worker queue promotion/wait-plan construction at high outstanding counts and Submitter event API calls while holding Submitter lock.
- This static audit does not measure lock wait time, hold-time percentiles, starvation, priority inversion, or scheduler effects. The prior multi-producer latency benchmark is end-to-end and cannot be used as a lock-latency measurement.
- Next useful measurement, if warranted, is test-only per-lock wait/hold instrumentation with QPC timestamps, separating Adapter, Worker and Submitter locks and reporting p50/p95/max under the 14-producer stress workload. Instrumentation must not add nested locks or alter lock order.
- No production recommendation or lock refactor is justified by this audit alone.

## Safeguards

- No source/header/config/build files modified.
- No temporary files created; no files deleted.
- No commit/reset; no Production/source-project changes; no TLC run.