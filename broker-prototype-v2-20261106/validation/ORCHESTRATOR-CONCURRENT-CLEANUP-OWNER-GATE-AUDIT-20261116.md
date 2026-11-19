# Orchestrator Concurrent Cleanup Owner Gate Audit — 2026-11-16

## Result
BLOCKED — no checked-in Orchestrator implementation or shared cleanup-owner gate exists. This is a source-contract audit, not a runtime pass.

## Confirmed source facts
- `raya_integrated_adapter_destroy` closes admission through `lifecycle_state`, rejects active entrants, checks retained/active state, then deletes its critical section and zeroes the object. Its public header explicitly requires callers to serialize destroy calls and quiesce/join all possible callers before successful destruction.
- `raya_submitter_destroy` checks `initialized`/`joined`, checks quiescence under its lock, releases that lock, then closes `wake_event` and `changed_event`, deletes the critical section, and clears `initialized`. There is no cleanup-owner CAS or serialization spanning the close sequence. Two concurrent destroy callers could both pass the initial checks and race on the same handles/critical section.
- `raya_managed_worker_destroy` similarly checks state under the worker lock, releases the lock, closes `wake_event`, then deletes the critical section and zeroes the object. There is no cleanup-owner gate spanning the close/finalize sequence. Concurrent destroy callers are not shown safe by the API contract.
- Repository root contains no checked-in Orchestrator implementation; matching files are validation reports only.

## Existing evidence that must not be conflated
- Adapter concurrent admission/drain is tested; that does not establish concurrent Adapter Destroy serialization.
- Submitter cleanup-owner serialization was tested only in a disposable Submitter-only prototype.
- Chained Adapter→Submitter→Worker failure/retry was tested with a disposable orchestration wrapper; it explicitly did not test concurrent cleanup callers.
- Worker close-failure retry was tested in a disposable copy; it did not establish concurrent Worker Destroy safety.

## Required design before runtime integration test
1. A single Orchestrator cleanup-owner token must serialize the entire cursor operation, not merely each component call.
2. The token must have a defined release/retention rule on every failure and cancellation path.
3. Cursor advancement must occur only after a component cleanup has completed successfully and its object is no longer touched.
4. Successful prior stages must never be re-entered on retry.
5. Orchestrator storage and all component storage must remain alive while any cleanup owner or API caller can reference them.
6. Borrowed Worker must never be shut down, joined, or destroyed by the Orchestrator.
7. Component-level Destroy calls must either be serialized by the Orchestrator contract or independently made concurrency-safe; current Submitter and Worker APIs do not demonstrate the latter.

## Decision
Do not run a purported real concurrent three-component cleanup test against the current public APIs: there is no production Orchestrator and the component Destroy paths are not generally safe for concurrent callers. Next work should be a disposable design prototype of the Orchestrator-wide owner + cursor, followed by sequential API integration tests. No production/source edits are authorized by this audit.

## Integrity
- Read-only inspection of current Working Copy source and existing validation reports.
- No source, header, test, build, model, archive, or Production files changed.
- No process was terminated; no Commit or Reset.