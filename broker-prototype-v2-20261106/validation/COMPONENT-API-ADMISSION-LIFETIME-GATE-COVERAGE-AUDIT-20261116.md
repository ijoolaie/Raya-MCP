# Component API Admission and Lifetime Gate Coverage Audit — 2026-11-16

## Decision
BLOCKED for checked-in Orchestrator integration. The Adapter has an entrant gate for two public APIs, but the Submitter and Worker have no equivalent public-call lifetime gate. A successful Adapter admission-race stress test must not be generalized to the other component APIs or to the Orchestrator control block.

## Scope and method
Read-only inspection of the current Working Copy headers and implementations for all public lifecycle, request, operation, and diagnostic entry points. No concurrent destruction test was run against unguarded APIs because such a test would intentionally race unsynchronized initialized checks, critical-section deletion, and object zeroing; a crash or undefined behavior would not be a safe diagnostic. No source was modified.

## Findings by component

### Adapter
- `lifecycle_state` implements CLOSED high-bit + entrant-count low bits.
- `raya_integrated_adapter_submit` and `raya_integrated_adapter_retained_count` acquire/release an entrant lease.
- `raya_integrated_adapter_destroy` closes admission and rejects while entrants or retained/active work remain. Successful destroy deletes the lock and zeroes the object.
- `start` is not covered by the entrant gate and must be externally serialized with every other lifecycle operation.
- `destroy` itself is not protected by a separate cleanup-owner token; concurrent Destroy calls are forbidden by the public contract and must be serialized by the owner.
- Header requires storage to remain alive, all possible callers to be joined/quiesced, and new calls to be prevented after successful destruction. This is necessary: a gate embedded in freed storage cannot protect a stale raw pointer.

### Submitter — critical gap
- Header exposes `start`, `start_with_api`, `admit`, `enqueue`, `abort_admission`, `acknowledge`, `shutdown`, `join`, `destroy`, and `snapshot`.
- None acquires an atomic entrant lease or cleanup-owner token.
- Most APIs perform an ordinary `initialized` read before entering the critical section. `join` and `destroy` also read `started`/`joined` outside the lock and perform waits/handle closes outside it.
- `destroy` releases the lock before closing two handles and deleting the critical section. Concurrent Destroy callers can pass the same preconditions; concurrent API callers can race lock deletion/object teardown unless an external owner quiesces them.
- `start_with_api` begins with `memset(s,0,...)`; concurrent start or any access while construction begins is unsupported and must be externally serialized.
- Failed-start and post-Join partial-destroy states retain handles/state, but the current public API does not provide a general lifetime gate plus exact retry owner protocol. Existing cleanup retry reports are disposable prototypes, not checked-in behavior.
- `acknowledge` can wait indefinitely on `changed_event`; its caller lease must remain held across the wait, and shutdown/cleanup must not free the event or object until the waiter exits.

### Worker — critical gap
- Header exposes operation initialization and Worker start, test seams, admit, finish, reserve, handoff, shutdown, join, and destroy.
- None acquires a public API entrant lease or cleanup-owner token.
- Public calls use ordinary `initialized`/`started` checks and then access the Worker critical section, registry, handles, and operation state.
- `join` waits and closes the thread handle outside the Worker lock. `destroy` checks state under the lock, releases it, closes wake_event, then deletes the lock and clears initialized. External serialization/quiescence is mandatory.
- Worker thread lifetime is separately protected by its thread handle and successful Join; that does not protect arbitrary external API callers.
- Operation memory and callback/release contexts have their own ownership rules and cannot be freed merely because the public Worker call count reaches zero.

## Cross-component contract
1. Adapter entrant count protects only Adapter Submit and retained-count calls; it does not automatically protect direct Submitter or Worker callers.
2. Orchestrator must be the exclusive lifecycle authority or every direct public API must have its own integrated gate. Mixing the two without a documented ownership boundary leaves bypass paths.
3. The Orchestrator gate must be in stable storage whose lifetime is guaranteed by an external owner/registry/reference mechanism. A raw pointer cannot safely acquire a reference after the pointed-to memory has already been freed.
4. Close admission first, wait for admitted calls to drain, serialize cleanup ownership, retain storage and all failed handles across retry, and only then permit final storage reclamation after external caller quiescence.
5. Do not release a caller lease before an API's last access to component state. For blocking `acknowledge`, this includes the entire wait and all return-path state accesses.
6. Construction/start and final free must be externally serialized unless a separately-lived control block owns those transitions.

## Required work before acceptance
- Choose and document one architecture: (A) Orchestrator-only public facade with component structs/functions private to it; or (B) independently gated Submitter and Worker APIs plus Orchestrator-level cleanup serialization.
- For architecture B, cover every public entry point, including snapshot/test seams where applicable, start/failed-start cleanup, blocking ACK, Join, partial Destroy retry, and callbacks/reentrancy restrictions.
- Define whether callback reentrancy into shutdown/destroy is forbidden; Submitter callbacks execute on the Submitter thread and self-join/self-wait must not be permitted.
- Add a stable external handle/control-block lifetime protocol; do not claim an embedded gate solves stale raw pointers.
- Add deterministic tests for each API family, caller drain, cleanup owner contention, failed-start retry, partial handle close retry, Owned/Borrowed Worker, and late-handle lookup through the stable external owner.
- Only after that integration exists should chained failure tests be treated as evidence for the actual product implementation.

## Integrity
- Read-only audit; no source/header/test/build/model/archive/Production changes.
- No Commit or Reset; no process termination.
- Existing temporary prototypes and prior reports remain separate evidence and are not represented as checked-in functionality.
