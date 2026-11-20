# Stage 5.16 Strict Dual-ACK Architecture Design Gate - 2026-11-20

## Owner decision

The project owner selected **preserve the strict Stage 5.16 acceptance criterion**. The criterion is not weakened, replaced by mixed-stage retention, or treated as optional stress coverage.

Acceptance still requires all of the following on one shared Managed Worker:
- two distinct Adapter/Submitter instances;
- two distinct operations independently accepted/issued by the operating system;
- each operation independently reaches its own terminal completion and post-terminal Submitter ACK failure;
- each failure is retained with its own stable Adapter/Worker identity and first-cause evidence;
- no fabricated operation, synthetic retained record, lifecycle mutation, forced cleanup, replay, or bypass of fail-closed ownership rules.

Stage 5.19 remains dependent on this exact Stage 5.16 proof.

## What this decision authorizes

It authorizes a separately reviewed architecture-design work item to find a way to satisfy the unchanged criterion while preserving fail-closed isolation. It does not select an architecture, authorize Production changes, authorize a test hook, or permit weakening any lifecycle/recovery contract. Implementation remains gated on a concrete design review and bounded test strategy.

## Source-backed constraint

The current Worker performs its release callback synchronously on its single completion thread. The Adapter ACK-failure path retains the envelope and degrades the shared Worker before returning. The Worker exits at the next loop entry when it observes DEGRADED. Therefore the current synchronous callback topology cannot process a second completion on that Worker after the first ACK failure.

A timing barrier inside the first ACK callback is not a valid solution: it blocks the only Worker thread that must process the second completion. The existing mixed-stage test remains useful evidence for two Adapter-associated retained records, but its second operation is rejected before OS issue and does not satisfy this criterion.

## Design questions that must be answered before implementation

1. Can terminal completion notification/ACK delivery be separated from the Worker completion loop without transferring or duplicating ownership of the operation envelope?
2. If ACK delivery is dispatched asynchronously, which component owns the queued notification, its lifetime, its failure evidence, and its shutdown/join contract?
3. Can one Adapter/Submitter ACK failure quarantine only that Adapter-associated envelope and Submitter while allowing the shared Worker to finish already-issued independent operations, without permitting new unsafe work or releasing uncertain resources?
4. What exact state transition prevents a failed ACK from being retried, replayed, or silently dropped, while still allowing the second independent ACK failure to be observed?
5. How are callback ordering, concurrent Adapter shutdown, Worker shutdown, dispatcher failure, dispatcher wake failure, and partial initialization/teardown made fail-closed?
6. What externally observable snapshots prove two OS-issued operations, two terminal completions, two independent ACK failures, two retained records, and exact identity/cause correspondence?
7. Does the proposal preserve the current single-Worker topology and resource ownership, or does it introduce a new shared coordination object whose lifecycle must be specified?

## Candidate to investigate - not selected

A bounded terminal-notification dispatcher outside the Worker completion thread is one candidate for source-level feasibility review. The Worker would publish a terminal notification only after its own terminal/finalizer ownership transition is valid; a separately owned dispatcher would perform each Submitter ACK. This could allow the Worker to finish a second already-issued operation before the first ACK failure is delivered.

This candidate is **not yet shown safe**. In particular, the current `retain_envelope` path couples Adapter retention to shared Worker degradation/publication. The design must establish whether ACK-failure quarantine can be isolated per Adapter without losing Worker-level identity/accounting, and how the Worker/dispatcher stop or drain after failure. Do not implement this candidate until these ownership and lifecycle questions are answered.

## Explicit non-goals / prohibited shortcuts

- Do not suppress or defer the Worker DEGRADED check merely to keep dispatching.
- Do not continue processing new work after an uncertain ownership/ACK failure without a reviewed state machine.
- Do not add a sleep, timing race, scheduler stress, or callback barrier as acceptance evidence.
- Do not synthesize a second operation, mutate internal state/counters/lists, or use a second Worker to satisfy a same-Worker criterion.
- Do not retry ACK, release, close, callback, or I/O; do not replay uncertain writes.
- Do not alter Stage 5.16 or 5.19 acceptance wording to fit available evidence.

## Required next work

1. Read-only source trace of Worker release callback, Adapter retained publication, Submitter ACK transition, and all relevant shutdown/destroy paths.
2. Produce at most two concrete architecture candidates with explicit ownership/state diagrams and failure matrices; reject candidates that require an unsafe post-DEGRADED Worker continuation.
3. Select no candidate until its lock ordering, object lifetimes, callback cardinality, failure isolation, and teardown are proven against the public lifecycle contracts.
4. Define a deterministic test fixture using existing supported seams where possible; any new test-only seam requires a separate narrow review and must expose only a real synchronization boundary.
5. Only after design review, implement in an isolated branch and prove the strict dual-ACK scenario with actual OS-issued operations.

## Current status

- Owner decision to preserve strict 5.16 criterion: RECORDED.
- Architecture candidate: NOT SELECTED.
- Architecture implementation: NOT AUTHORIZED by this document.
- Stage 5.16: PARTIAL / NOT ACCEPTED.
- Stage 5.19: PARTIAL / DEPENDENCY BLOCKED.
- Stage 5.18: PARTIAL.
- Stage 5.20: INCOMPLETE; Gate D still NO ACTION APPROVED.
- Gate E: BLOCKED. Gate F: PENDING review/acceptance.
- Stage 5 / Phase 1: NOT ACCEPTED. Production: NO-GO.

No source, header, ABI, test, build, lifecycle, ownership, cleanup, recovery, retry, callback, or replay behavior is changed by this design-gate record. No tests/builds are claimed.