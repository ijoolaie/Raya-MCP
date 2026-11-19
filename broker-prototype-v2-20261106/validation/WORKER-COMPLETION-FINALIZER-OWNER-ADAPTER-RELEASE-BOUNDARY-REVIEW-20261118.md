# Worker Completion, Finalizer, Owner Release, and Adapter Release Boundary Review — 2026-11-18

## Scope
Read-only source review in the Raya MCP Working Copy. No production code, test, build, configuration, or README files were changed by this review. No retained operation was retried, released, unlinked, freed, or replayed.

## Source-backed lifecycle sequence

### 1. Worker completion observation
In `reaper-managed-worker-v2.c`, the worker thread calls the configured `GetOverlappedResult`-compatible function for the operation. The terminal path records:
- `op->completion_error` as success or the observed Win32 error;
- `op->transferred` from the completion API output;
- the integrated terminal result (success or terminal-aborted);
- a terminal collection in `raya_pipe_owner_reaper_collect`.

An incomplete observation remains pending and is retried by the worker's bounded backoff loop. An uncertain completion enters the integrated terminal-hold path and returns without finalizing handles, committing owner release, removing the operation from the integrated registry, or invoking the release callback. This is a quarantine/hold, not proof of recipient-level commit.

The completion result is local OS/overlapped-I/O evidence. It is not an application-level receipt or proof that a remote recipient durably committed a WRITE.

### 2. Finalizer close and commit
After a determinate terminal collection, the Worker calls `raya_pipe_finalizer_close_handles` and then `raya_pipe_finalizer_commit_release`.
- The finalizer closes the issuing-pipe handle, then the completion-event handle.
- A failed close transitions to sticky `RAYA_FINALIZER_CLOSE_UNCERTAIN`; subsequent close calls return failure without blindly retrying a possibly consumed handle.
- The release commit requires both handles null, exactly two successful closes, and no prior release commit.
- Only then does the finalizer enter `RAYA_FINALIZER_RELEASED`.

Thus finalizer close/commit is separately observable from completion. It is not interchangeable with owner release or Adapter callback release.

### 3. Pipe-owner release
The Worker next calls `raya_pipe_owner_release`. It accepts only CALLER_TERMINAL or REAPER_TERMINAL, requires exactly one terminal collection, and rejects a second release commit. Success increments `release_commits` and moves the owner to RELEASED.

A failure here occurs after the finalizer may already have committed. The Worker marks an ownership-invariant degradation and exits the worker thread; it does not roll back closed handles or repeat the finalizer. This is a partial cross-component effect that must remain quarantined.

### 4. Integrated registry release
After finalizer and owner release, the Worker removes the operation from the integrated registry under the Worker lock. If that step fails, it marks an ownership-invariant degradation and exits. The prior handle and owner commits remain committed; they are not reversible.

### 5. Adapter release callback and Submitter ACK
Only after integrated registry release does the Worker invoke the operation's release callback, outside the Worker lock, and promises not to touch the operation afterward.

For Adapter-owned operations, `adapter_worker_release`:
1. sets `worker_terminal_released`;
2. attempts `raya_submitter_acknowledge`;
3. on ACK failure, retains the Adapter envelope and returns without invoking the client callback or freeing the envelope;
4. on ACK success, invokes the client completion callback and frees the envelope.

The Adapter's `worker_terminal_released` flag therefore means the Worker completed its own terminal cleanup and called the Adapter release callback. It does not mean Submitter ACK succeeded, the client callback ran, or the envelope was freed.

The Adapter's `worker_finished` flag is different: it records consumption of the Worker caller-admission lease through `worker_finish_once`; it is not the Worker completion/release-callback marker.

## Diagnostic observability already present
The test-only retained snapshot exposes, for Adapter-retained entries:
- process-local Adapter instance ID and Adapter-scoped operation ID;
- first retain cause and coarse first-retain stage;
- owner state, collection attempts, terminal collection count, owner release commits;
- finalizer state, close attempts, successful closes, finalizer release commits;
- integrated operation/collection/reservation states and Worker registry/inventory counters;
- Submitter Issue/Publish effect planes and request/lifecycle counters;
- flags for retained, Worker-owned, Worker-terminal-released, Worker-admitted, and Worker-finished.

The snapshot requires closed Adapter admission, zero Adapter entrants, a joined Worker with no thread handle, and a quiescent Submitter. It reads Adapter, Worker, and Submitter inventories under separate locks. It is not an atomic cross-component snapshot. It is test-only and returns raw process addresses for correlation; those addresses are not stable identifiers.

## Important distinctions and gaps
1. **Completion vs recipient receipt:** OS completion and transferred-byte count do not establish remote application commit. No recipient-stable idempotency key or recipient status/receipt query is present in this path.
2. **Finalizer vs owner vs integrated release:** each is an independent effect plane. A later failure does not undo earlier commits; a generic retry or generic release operation would be unsafe.
3. **Adapter release callback vs client callback:** Submitter ACK failure occurs after Worker terminal cleanup and intentionally retains the Adapter envelope. The client callback is not called in that branch.
4. **Worker retained inventory is not the complete retained inventory:** post-terminal Adapter ACK failure can be Adapter-only retained state because the Worker has already released the integrated operation. Pre-terminal/Worker-owned holds can appear in both inventories. Inventory cardinality is therefore not expected to match.
5. **No production retained recovery surface:** production exposes retained count, not safe enumeration, evidence-specific reconciliation, or release. The detailed snapshot is compiled only for tests.
6. **No durable recovery:** Adapter/operation IDs are process-local and Adapter-scoped. They are not persisted, propagated as recipient idempotency identities, or sufficient for restart recovery.
7. **No safe replay of uncertain WRITE:** absent recipient idempotency and status evidence, replay could duplicate an effect.

## Recommended next bounded work
Keep retained resources quarantined. If extending diagnostics, add explicit typed completion-observation fields to the test-only snapshot and validate their relationship to owner/finalizer/integrated states. Do not add a generic retained release/retry API. A production recovery design requires a stable end-to-end operation identity, exact per-stage effect records, recipient evidence, and an explicit reconciliation state machine before any release decision.

## Validation and change control
- Review method: source inspection only.
- Build/tests: not run; no source changed.
- Files changed by this review: this report only.
- No commit/reset; no Production change.
- Phase 1 remains NOT ACCEPTED; Production remains NO-GO.
