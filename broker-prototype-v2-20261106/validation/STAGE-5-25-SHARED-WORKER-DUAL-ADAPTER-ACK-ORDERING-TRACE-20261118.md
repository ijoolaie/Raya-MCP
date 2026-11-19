# STAGE 5.25 — Shared-Worker Dual-Adapter ACK/Quarantine Ordering Trace — 2026-11-18

## Scope
Read-only source trace of the failed Stage 5.24 dual-Adapter/one-Worker runtime experiment. No production or test source was changed in this pass.

## Findings

1. Adapter submission registers Worker identity, then calls `raya_managed_worker_admit`, then Submitter admission/reservation/enqueue. Therefore an Adapter envelope can be retained even when Worker admission fails; that does not prove an OS I/O was issued or that Worker terminal completion occurred. See `integrated-submit-adapter-v2.c`, `adapter_submit_impl`, around lines 429–455.

2. Worker terminal completion closes the operation handles, commits finalizer release, releases ownership, removes the operation from the integrated registry, and then calls the operation release callback from the Worker thread. See `reaper-managed-worker-v2.c`, around lines 554–580.

3. For Adapter-associated operations, `adapter_worker_release` synchronously calls `raya_submitter_acknowledge`. If that ACK fails, it calls `retain_envelope` before returning to the Worker loop. See `integrated-submit-adapter-v2.c`, around lines 193–204.

4. `retain_envelope` publishes the envelope into the Worker retained list when the Worker does not own the operation or the operation is already terminal-released, and then unconditionally marks the shared Worker degraded with the retain cause. See `integrated-submit-adapter-v2.c`, around lines 139–180.

5. The Worker main loop checks `RAYA_REAPER_DEGRADED` at the top of its next iteration and exits with `ERROR_INVALID_STATE`. See `reaper-managed-worker-v2.c`, around lines 397–405.

6. The Submitter also transitions its own lifecycle to DEGRADED on failed notification and exits its submitter thread on the next loop. This is Submitter-local; the shared Worker degradation in this scenario is caused by Adapter `retain_envelope`, not by a cross-Submitter shared ACK state. See `dedicated-io-submitter-v2.c`, around lines 173–199 and 87–96.

## Causal conclusion

The observed Adapter B state (Adapter retained envelope, zero Worker close callbacks) is consistent with this exact ordering:

- Adapter A reaches Worker terminal release.
- Adapter A's synchronous Submitter ACK fails.
- Adapter A's `retain_envelope` marks the shared Worker DEGRADED.
- The Worker loop exits before it can process Adapter B's pending operation.

Adapter B may still have an Adapter retained envelope because its submission path can retain after Worker admission rejection. Such a record is not evidence of an issued OS operation or terminal completion. The prior experiment did not establish the precise B retain stage/issue effect from the captured diagnostic summary, so that detail remains unclaimed.

## Barrier feasibility

A test-only barrier placed inside or immediately before Adapter A's synchronous ACK cannot allow the same Worker thread to process Adapter B's terminal completion: the Worker thread is executing the release callback and cannot advance its loop until the callback returns. Changing this would require altering callback/ACK architecture or the production degradation semantics, neither authorized.

A different experiment could allow Adapter A's ACK to succeed and inject failure only for Adapter B. That can demonstrate two independent Adapter paths sharing a Worker, but it cannot demonstrate two independently ACK-failed retained operations. It is a different acceptance criterion and must not be reported as 5.16 completion.

## Decision

- Do not add a barrier that blocks the Worker release callback.
- Do not weaken Worker degradation or change production callback/ACK ordering.
- Keep Stage 5.16 multi-Adapter retained runtime BLOCKED / NOT COMPLETE.
- Keep all quarantined resources untouched.
- No tests/builds run in this pass; this was a source trace only.
- No source/header/build script/README/TLA+ model/config changed.
- Disk check before any build: C free 13,209,452,544 bytes; D free 4,809,883,648 bytes.

## Next safe work
1. Capture exact per-envelope issue/publish/Worker-admission/terminal/retain-stage fields in a read-only quiescent snapshot for a bounded single-ACK-failure shared-Worker run, if existing test hooks can expose them without changing semantics.
2. Separately test a successful Adapter A completion followed by Adapter B ACK-failure retention; label it as one successful and one retained Adapter operation, not two retained submissions.
3. Keep production enumeration/reconciliation APIs and positive recovery tests blocked pending explicit design approval and implementation authorization.
