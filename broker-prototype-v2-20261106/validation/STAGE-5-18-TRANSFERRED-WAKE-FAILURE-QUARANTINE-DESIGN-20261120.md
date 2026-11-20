# Stage 5.18 — Transferred Wake-Failure Quarantine and Worker Wake Observation Design

Date: 2026-11-20  
Status: DESIGN PROPOSAL — source-backed; not implemented  
Scope: broker-prototype-v2 only. No Production behavior, recovery, retry, replay, or retained-record release is authorized by this document.

## 1. Problem statement

A successful integrated handoff commits the operation to the Worker queue, transfers the pipe-owner state to Reaper, and changes the reservation to HANDED_OFF. If signaling the Worker wake event then fails, the operation is already Worker-owned but has not necessarily been promoted into the completion Registry. The existing Adapter post-return retention path is not an atomic publication boundary. The Worker may also remain blocked in its production infinite wait because the failed wake cannot wake it.

This is a transferred-but-unprocessed state, not NOT_ISSUED, not post-terminal retention, and not proof that the operation is unowned or that its I/O is terminal.

## 2. Source-backed facts

- `managed_worker_handoff_internal` holds `w->lock` while it performs integrated handoff, pipe-owner handoff, and `signal_wake_locked`; it returns TRANSFERRED_DEGRADED on wake failure before releasing the lock.
- The existing `RAYA_ADAPTER_TESTING` handoff callback is invoked for a degraded transfer before Worker lock release. It demonstrates the required synchronization boundary but is not a Production publication path.
- `managed_worker_main` currently builds a wait plan under Worker lock and calls `WaitForMultipleObjects(..., INFINITE)` in the non-test build.
- The test build already has a finite-wait timeout branch. After `raya_reaper_waitplan_resolve` returns TIMEOUT, the Worker checks DEGRADED under its lock and loops if not degraded. This is existing source behavior in the test configuration, not proof that a finite production wait has been accepted.
- `retain_envelope` publishes Adapter retention, then separately acquires Worker lock. It publishes Worker retained inventory only if Worker ownership was not established or terminal release already occurred; it then marks the Worker degraded. That path is not suitable for a queue-owned, transferred-but-unprocessed operation.
- Worker queue/outstanding/reservation state remains live in this failure class. The Worker retained-list API is not a representation of the queue-owned operation.
- Existing public Destroy contracts reject outstanding/queued/reserved or retained work. They are not reconciliation APIs.

## 3. Proposed state classification

Introduce a distinct lifecycle class, `TRANSFERRED_DEGRADED_PENDING`, separate from both ordinary Adapter Retained and Worker POST_TERMINAL retained records.

Required immutable evidence:
- Adapter identity and operation identity;
- handoff committed = true;
- Worker ownership transferred = true;
- wake signal failed = true, with captured Win32 error;
- queue membership and operation state at publication;
- reservation state;
- Worker thread state (waiting/running/exited/join observed);
- I/O completion state = unknown until independently observed;
- Submitter issuer-lease / ACK state;
- callback disposition = suppressed/not delivered.

This classification is diagnostic and lifetime-protective only. It does not authorize dequeue, collect, close, free, unreserve, ACK, callback, replay, retry, or reconciliation.

## 4. Candidate A — atomic Adapter in-flight quarantine publication

### 4.1 Transition

Inside the existing Worker-locked handoff critical section:

1. Commit integrated queue ownership and pipe-owner handoff as today.
2. Attempt the normal Worker wake signal.
3. If and only if the result is TRANSFERRED_DEGRADED due to WAKE_SIGNAL, invoke a narrow Adapter publication callback before releasing Worker lock.
4. The callback validates the exact operation/envelope identity and links the envelope into a distinct Adapter in-flight-quarantine registry, setting the immutable first cause/stage and incrementing its own count exactly once.
5. If publication fails or identity validation fails, preserve Worker DEGRADED and add OWNERSHIP_INVARIANT as a secondary failure cause. Do not report publication success.
6. Release Worker lock. No later post-return call may be relied on to establish this atomic state.

The registry must be distinct from the existing post-terminal/ordinary retained list. It must not imply that the Worker released the operation.

### 4.2 Lock-order contract

The only permitted nested lock order for this publication is:

`Worker lock -> Adapter lock`

Requirements before implementation:
- Complete a source audit proving no path holds Adapter lock while acquiring Worker lock.
- Keep Submitter lock out of this callback.
- Do not call Worker APIs from the Adapter callback; the Worker lock is already held.
- Do not invoke user callbacks, allocate unbounded memory, perform I/O, wait, or close handles while either lock is held.
- Callback failure is fail-closed and cannot roll back the committed handoff.

The current sequential lock acquisitions in `retain_envelope` do not by themselves prove the whole codebase has no reverse nested lock order; that audit remains a prerequisite.

### 4.3 Ownership and lifetime

- Worker remains the sole owner of operation lifecycle, queue membership, reservation, and eventual completion resources after transfer.
- Adapter owns the envelope allocation and the new in-flight-quarantine registry membership.
- The Adapter must not free the envelope while the Worker may still reference its embedded operation.
- Worker retained-list membership must remain empty for this pre-terminal queue-owned state. Do not mislabel it POST_TERMINAL or ordinary Worker Retained.
- Worker Destroy remains blocked by queue/outstanding/reservation state. Adapter Destroy must additionally reject a nonempty in-flight-quarantine registry.
- Existing v1 retained inventory semantics must not be silently overloaded. Add a versioned diagnostic inventory/snapshot surface, or an explicitly versioned extension, that reports this state as a distinct category with stable identities and captured cause. A copied snapshot remains read-only and is not a recovery receipt.

## 5. Candidate B — bounded production wait to observe DEGRADED

Replace the production infinite wait with a finite, documented timeout for the Managed Worker wait plan. On TIMEOUT:

1. Reacquire Worker lock.
2. Check lifecycle before resolving/claiming any operation.
3. If DEGRADED, exit fail-closed without touching the queue head or operation.
4. If not DEGRADED, release lock and rebuild the wait plan in the normal loop.
5. A timeout alone is not a failure cause and must not mutate operation, reservation, registry, or retained state.

The timeout bounds detection of a missed wake without depending on another operation event or another successful signal. A starting value such as 100 ms is a test candidate only, not a selected production constant; measure CPU wake overhead, normal idle behavior, and worst-case degradation-observation latency before choosing the final interval.

Race reasoning: the handoff failure marks DEGRADED while holding the same Worker lock. If the Worker checks lifecycle just before the handoff, it releases the lock and enters the wait; the failed wake then occurs, and the next finite timeout returns the Worker to the lock-protected lifecycle check. If handoff wins the lock first, the Worker observes DEGRADED before building/waiting. In neither ordering should the Worker claim or process a queued operation after observing DEGRADED.

A separate supervisor thread/event is deferred. It adds independent thread, queue, signal, join, and destroy failure domains without being necessary to establish bounded observation.

## 6. Combined state machine

```text
CALLER_OWNED
    |
    | integrated handoff + owner handoff committed
    v
WORKER_QUEUE_OWNED
    |
    | SetEvent(wake_event) fails
    v
TRANSFERRED_DEGRADED_PENDING
    |  Adapter in-flight quarantine published under Worker lock
    |  Worker core DEGRADED; queue/reservation remain intact
    |  Submitter issuer lease remains held; no ACK/callback
    |
    +-- Worker finite wait timeout --> observe DEGRADED --> thread exits
    |
    +-- any independent event --> lock-protected lifecycle check --> exit
    |
    +-- no automatic recovery transition
```

No transition from this state to terminal, released, replayable, or reconciled is specified.

## 7. Required implementation/test gates

### Gate 1 — source and lock audit
- Enumerate every Worker/Adapter lock acquisition and prove no Adapter->Worker nested acquisition exists.
- Confirm callback executes only after committed transfer and only for WAKE_SIGNAL, not for ownership invariant, wait failure, or unrelated degraded outcomes.
- Confirm callback has no Submitter lock dependency and no user-code invocation.

### Gate 2 — deterministic publication test
- Use the existing real named-pipe/overlapped-I/O fixture.
- Prove OS acceptance (ERROR_IO_PENDING), committed handoff, exact operation/envelope identity, and injected wake failure.
- Force Worker contention at the lock boundary and prove the Adapter in-flight record is visible before Worker can proceed.
- Assert exactly one registration; Worker retained list remains empty; queue/outstanding/reservation remain intact; buffer/pipe/event/OVERLAPPED remain valid; issuer lease remains held; callback count remains zero.
- Inject publication failure in a separate negative test and prove Worker remains DEGRADED with invariant cause and no false publication success.
- Do not mutate internal counters/lists to manufacture state.

### Gate 3 — bounded wait observation
- Run a process-isolated real Worker test with no other event signaled after wake failure.
- Assert Worker exits after the bounded timeout, with only the expected failure cause, and without claiming/dequeuing/collecting the operation.
- Test a completion event becoming signaled around the failed wake; lifecycle must be checked before operation claim.
- Confirm ordinary idle wait and normal operation completion still behave correctly.
- Measure timeout overhead before selecting a production interval.

### Gate 4 — inventory and lifetime
- Add versioned Adapter and Worker diagnostic snapshots for this state; preserve v1 meaning.
- Snapshot must expose queue-owned pending state separately from Worker Retained records.
- Verify Adapter/Worker Destroy reject this state without mutation.
- No cleanup by direct handle close, structure zeroing, list unlink, free, unreserve, ACK retry, I/O replay, or reconciliation.
- Process exit may reclaim only process-local test fixture memory and must be explicitly documented as test isolation, not product recovery.

### Gate 5 — regression
- Strict Stage 5.16 dual-ACK acceptance remains unchanged.
- Stage 5.19 independent per-plane v1 inventory criteria remain unchanged; new v2 diagnostic visibility must not weaken v1 semantics.
- Full Adapter API and non-test-macro native suites pass.
- Build outputs and isolated fixture artifacts are removed and absence verified.

## 8. Alternatives rejected/deferred

- **Post-return Adapter retention:** rejected as the publication boundary; Worker may race past it.
- **Reuse Worker Retained / POST_TERMINAL:** rejected because the operation is still queue-owned and has not reached terminal release.
- **Treat queue membership as sufficient user-visible inventory:** rejected; it is not exposed by the current Worker v1 retained inventory and does not identify the Adapter envelope/cause.
- **Retry SetEvent:** rejected; a retry is not a proven wake mechanism and changes failure semantics.
- **Continue processing after DEGRADED:** prohibited.
- **Infinite wait plus unrelated event/signal:** rejected; it does not bound observation.
- **Separate supervisor thread:** deferred pending evidence that bounded wait is insufficient.

## 9. Acceptance and authorization boundary

This document selects the bounded-wait plus atomic in-flight-quarantine design as the preferred prototype direction, subject to Gates 1–5. It does not claim implementation, test success, or Production readiness.

Authorized by the user's instruction to proceed with the proposed design work:
- source-backed design specification and documentation updates;
- no Production or recovery behavior changes.

Not authorized by this design step:
- implementing new Production ownership/retention behavior;
- changing public ABI or v1 inventory semantics;
- adding a reconciliation/recovery action;
- releasing, unlinking, replaying, ACKing, or cleaning retained/uncertain operations.

## 10. Current disposition

- Stage 5.16: COMPLETE; strict criterion preserved.
- Stage 5.18: PARTIAL; this design resolves the design proposal only, not runtime evidence.
- Stage 5.19: COMPLETE within the frozen v1 independent-inventory contract.
- Stage 5.20: INCOMPLETE; Gate D remains NO ACTION APPROVED.
- Gate E: BLOCKED. Gate F: PENDING full owner acceptance and PR review.
- Stage 5 / Phase 1: NOT ACCEPTED. Production: NO-GO.

No source, header, ABI, test, build, runtime, ownership, retention, cleanup, recovery, retry, callback, or replay behavior was changed. No tests/builds are claimed.
