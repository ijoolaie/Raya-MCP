# STAGE-5-25-SHARED-WORKER-ACK-QUARANTINE-ORDERING-TRACE-20261118

## Scope
Source-only trace of the failed Stage 5.24 two-Adapter / one-Worker experiment. No production behavior or test fault semantics changed.

## Source trace
1. `adapter_submit_impl` allocates Worker identity before admission, then calls `raya_managed_worker_admit`, reserves a Worker slot, admits/enqueues the Adapter's independent Submitter request.
2. Each Submitter has its own `submitter_main`. After OS issue is accepted, it calls `adapter_publish`; successful Worker handoff transfers the operation to the one shared Worker.
3. `managed_worker_main` is a single thread. It promotes queued operations into the wait registry, observes a completion, collects the terminal result, closes handles, commits finalizer release, releases pipe ownership and integrated registry state, and then invokes `op->release` inline on that same Worker thread.
4. The Adapter release callback is `adapter_worker_release`. It sets `worker_terminal_released`, calls `raya_submitter_acknowledge`, and only on successful ACK invokes the client callback and frees the envelope.
5. On ACK failure, `adapter_worker_release` calls `retain_envelope`. That function publishes the post-terminal Worker retained record, then unconditionally calls `raya_reaper_mark_degraded_cause` on the shared Worker's integrated core, and signals its wake event.
6. The release callback is synchronous on the Worker thread. On returning to the top of `managed_worker_main`, the loop checks for `RAYA_REAPER_DEGRADED` under the Worker lock and exits with `ERROR_INVALID_STATE`.
7. The integrated admission path accepts callers only while the core lifecycle is `RAYA_REAPER_ACCEPTING`. Once the shared Worker is degraded, a later Adapter operation cannot be admitted to that Worker.

## Determination
- The failed Stage 5.24 observation is explained by a source-supported ordering: first post-terminal ACK failure quarantines its Adapter envelope and degrades the shared Worker from inside the Worker thread's synchronous release callback; the Worker then exits before it can process another operation's completion.
- A test barrier that merely delays the first ACK cannot allow the same Worker thread to process a second terminal completion: the Worker thread is blocked inside the first release callback. This is not a viable test-only scheduling seam.
- Allowing two independent Adapter-associated operations to reach Worker terminal release before an ACK fault would require changing the production execution/acknowledgement architecture (for example, decoupling ACK processing from the Worker release callback), or a different Worker concurrency model. Neither is authorized or justified in this stage.
- Do not weaken the shared Worker degradation behavior, suppress the fault, release retained resources, or replay uncertain I/O to make the experiment pass.
- The source trace explains why the attempted scenario cannot produce two post-terminal ACK-quarantined Worker records under the current single-threaded synchronous release design. It does not establish that all forms of multi-Adapter use are unsupported; successful multi-Adapter operation remains a separate, unproven runtime question.
- The earlier Adapter B retained count and zero Worker close callbacks do not prove a second Worker terminal-retained record. Adapter envelope retention and Worker retained-list membership remain separate facts.

## Files inspected
- `integrated-submit-adapter-v2.c`
- `reaper-managed-worker-v2.c`
- `reaper-integrated-v2.c`
- `reaper-core-v2.c`
- `dedicated-io-submitter-v2.c`
- `integrated-submit-adapter-v2-api-test.c`
- `validation/STAGE-5-24-SHARED-WORKER-DUAL-ADAPTER-RUNTIME-EXPERIMENT-20261118.md`

## Changes and validation
- Created this source-trace report only.
- No production source/header, test source, build script, README, model, or configuration changed.
- No build or runtime test was run; this was a source-ordering investigation.
- No process was stopped and no artifact was deleted.
- Disk free space observed before investigation: C: 13,209,452,544 bytes; D: 4,809,883,648 bytes.

## Stage status
- 5.16 multi-Adapter retained runtime: BLOCKED / NOT COMPLETE for two independent post-terminal ACK-retained operations on one shared Worker under current synchronous release semantics.
- 5.17 direct managed-pipe retained identity runtime: PASS in prior recorded execution.
- 5.18 first-retain immutability: PARTIAL.
- 5.19 snapshot consistency: PARTIAL.
- 5.20 production enumeration/reconciliation API: draft only; NOT IMPLEMENTED.
- Stage 5 implementation/recovery acceptance: BLOCKED / NOT ACCEPTED.
- Phase 1: NOT ACCEPTED.
- Production: NO-GO.

## Next safe work
1. Treat the two-post-terminal-ACK-retained-on-one-Worker target as blocked by the current execution architecture; do not add a misleading barrier.
2. If further evidence is needed, independently test two successful Adapter/Submitter pairs sharing one Worker, with no injected ACK fault, and keep that result distinct from retained-recovery acceptance.
3. Continue recovery API contract design only within the existing non-production boundary; production enumeration/reconciliation/release remains unimplemented and requires explicit authorization.
