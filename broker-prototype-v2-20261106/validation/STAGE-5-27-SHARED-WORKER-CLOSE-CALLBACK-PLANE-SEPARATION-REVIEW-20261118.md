# Stage 5.27 — Shared-Worker Close Callback Plane Separation Review (2026-11-18)

## Scope
Read-only source review to resolve the remaining ambiguity before another dual-Adapter/one-Worker runtime attempt. No production or test source was modified.

## Findings
1. The shared Worker close callback and each Adapter's close callback are distinct callback planes.
   - `raya_managed_worker_set_close_callback` stores `w->close_handle/close_context` under the Worker lock and only accepts configuration while the Worker is accepting and has no outstanding/reserved/registered operations.
   - `raya_integrated_adapter_start_with_apis` copies the Adapter API into that Adapter instance and starts its own Submitter. It does not install or replace the Worker's close callback.
   - Adapter envelope finalizer cleanup calls `adapter_close_handle`, which dispatches through that Adapter's own `a->api.close_handle` and `a->api.context`.
   - Worker-owned operation finalizer cleanup calls the Worker-level `w->close_handle/w->close_context`.
2. `adapter_worker_release` performs synchronous acknowledgement on the owning Adapter's Submitter. On failure, it retains that Adapter envelope and returns. Thus the fault intended to isolate Adapter B must be injected through B's Submitter signaling path; a Worker-close callback counter is not, by itself, proof that B's ACK failed.
3. The test harness currently uses `test_close` in both callback planes in some single-Adapter tests. That conflates their counters/fault triggers and makes a multi-Adapter trace harder to interpret.
4. A correct dual-Adapter experiment should:
   - configure the shared Worker's close callback exactly once, before any operations;
   - use per-Adapter close callbacks with separate counters and no shared global state;
   - arm ACK failure only for the selected Adapter's Submitter event, independently of Worker-close accounting;
   - record each Adapter's full post-quiescence retained snapshot and the shared Worker's retained inventory;
   - unregister callback/handle tracking only after the shared Worker has joined and no callback can execute.
5. A success on Adapter A followed by an ACK/quarantine on Adapter B is a useful secondary runtime proof of two Adapter identities sharing one Worker. It still does not satisfy the stronger Stage 5.16 requirement for two independent retained Adapter submissions.

## Decision
Do not rerun the earlier conflated setup unchanged. Separate the two callback planes in the test harness first, then attempt the controlled A-success/B-ACK-failure experiment. Do not add barriers inside the Worker's synchronous release callback; that would prevent the same Worker thread from processing the peer operation.

## Validation / safety
- Read-only inspection only; no build or test run.
- No source, header, test, build script, README, TLA+ model, or configuration changed.
- No process was started or stopped; no retained resource was released, replayed, or deleted.
- Disk free space observed before this review: C: 12,827,332,608 bytes; D: 4,809,850,880 bytes. Recheck before any later build.
- Existing checkpoint, trace, model, configuration, report, archive, state-store, and old root EXE/OBJ artifacts remain untouched.

## Status
Stage 5.27 review complete. Multi-Adapter runtime acceptance remains BLOCKED pending a callback-plane-separated test. Stage 5 implementation/recovery acceptance remains BLOCKED / NOT ACCEPTED. Phase 1 remains NOT ACCEPTED. Production remains NO-GO.
