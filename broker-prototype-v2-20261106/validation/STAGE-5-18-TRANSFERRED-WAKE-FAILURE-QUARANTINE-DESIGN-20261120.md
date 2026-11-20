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

## 11. 2026-11-20 Gate 1 source/lock audit addendum

Audit report: `validation/STAGE-5-18-GATE-1-SOURCE-LOCK-AUDIT-20261120.md`.

The Worker, Adapter and Submitter lock acquisitions and callback boundaries were audited in the current worktree. No Adapter-lock -> Worker-lock nested acquisition was found in the audited modules. Submitter issue/publish/not-issued callbacks run outside Submitter lock; the Worker release callback runs outside Worker lock. The only current nested Worker -> Adapter lock is the test-only transferred-degraded callback, matching the proposed direction.

Gate 1 is **PARTIAL**, not PASS: the current test-only handoff hook invokes its callback for any TRANSFERRED_DEGRADED result, covering both pipe-owner handoff failure (OWNERSHIP_INVARIANT) and wake signal failure (WAKE_SIGNAL). It does not yet enforce WAKE_SIGNAL-only invocation. The test callback also uses the existing Adapter retained list rather than a distinct production in-flight quarantine registry. The production wait remains INFINITE; the 100 ms finite wait remains test-only.

Immediate next slice: make the test seam cause-specific, with a negative owner-handoff-failure case proving the quarantine callback is not invoked. No code, ABI, v1 inventory, ownership, retention, recovery or cleanup behavior changed in this audit. No tests/builds were run.
## 12. 2026-11-20 Gate 1 closure addendum

The source/lock audit and test-only callback gating follow-up are recorded in `STAGE-5-18-GATE-1-SOURCE-LOCK-AUDIT-20261120.md`. The test-only handoff path now invokes its degraded callback only after integrated handoff and pipe-owner handoff succeed and the Worker wake signal fails. Owner-handoff invariant failure no longer invokes the callback. The full Adapter API build script passed 10/10 iterations (12,570 assertions total); all 16 declared EXE/OBJ outputs were verified absent. A dedicated runtime injection of owner-handoff failure was intentionally not added because it would require manufacturing invalid ownership state; the negative branch is source-verified.

Gate 1 is PASS for the audited source/lock and test-only callback contract. This does not change the overall Stage 5.18 PARTIAL status: production atomic in-flight quarantine, versioned diagnostic inventory and bounded production wait remain outstanding. The callback/registry and bounded wait changes are not present in the non-test-macro Production path.

# Stage 5.18 — Gate 2 deterministic test-only quarantine publication

Date: 2026-11-20
Status: PARTIAL — positive atomic-publication prototype evidence passed; multi-operation contention and production implementation remain outstanding.

## Scope

This slice implements a **RAYA_ADAPTER_TESTING-only** independent in-flight quarantine registry. It does not add a production registry, production diagnostic API, Worker v1 inventory extension, recovery, cleanup, or reconciliation behavior.

## Implementation

- Added Adapter test-only `quarantine_head/quarantine_count` and a separate envelope `quarantine_next/quarantined` link/flag.
- The existing Adapter retained list and its v1 inventory remain separate and unchanged.
- The test-only quarantine callback is entered under Worker lock and validates:
  - exact operation pointer matches the envelope's embedded operation;
  - Worker epoch/operation identity and Adapter instance/operation identity match;
  - integrated handoff and pipe-owner handoff have committed;
  - the operation is still QUEUED at the Worker queue head;
  - queue/outstanding/reservation/caller-active and collection/owner state are consistent;
  - the operation is not already retained or quarantined.
- Under the approved nested order Worker lock -> Adapter lock, it rejects duplicates and publishes the envelope to the independent quarantine list exactly once.
- A test-only read-only snapshot checks list cycles/count consistency and copies envelope, operation and Adapter identity plus WAKE_SIGNAL cause.
- Adapter Destroy rejects a non-empty quarantine registry. An isolated test proves this guard even when active and ordinary retained counts are zero.
- The deterministic wake-failure test performs a second Worker wake only after quarantine publication while Worker lock is still held. The Worker thread wakes and must contend on that lock; after the callback returns it exits through the degraded path. The test verifies the queue-owned operation is visible in the quarantine registry and absent from both ordinary Adapter retained inventory and Worker v1 retained inventory.

## Validation

The prescribed `build-integrated-submit-adapter-v2-api-test.cmd` passed 10/10 iterations under Visual Studio 2026 Build Tools 18.7.3 x64 with `/W4 /WX`, exit code 0:
- Adapter API regression: 10 x 1,087 assertions across 68 cases = 10,870 PASS.
- Strict dual-ACK isolation: 10 x 120 = 1,200 PASS.
- NOT_ISSUED notification isolation: 10 x 25 = 250 PASS.
- Managed Worker WAIT_FAILED isolation: 10 x 31 = 310 PASS.
- Total: 12,630 assertions PASS.

Non-test-macro compatibility:
- `build-integrated-submit-adapter-v2-compile.cmd`: PASS, exit code 0.
- `build-integrated-submit-adapter-v2-native-test.cmd`: 10/10 runs, 1,020 assertions each = 10,200 PASS.
- The test build, native test and compile scripts' declared outputs were checked; all 18 relevant EXE/OBJ paths were absent after completion.

## Gate 2 disposition

| Requirement | Result |
|---|---|
| Independent in-flight quarantine registry seam | PASS — test-only prototype |
| Distinct from Adapter retained and Worker v1 retained inventory | PASS |
| Exact envelope/operation/Adapter identity validation | PASS |
| Duplicate publication rejection / one-record assertion | PASS |
| Publication while Worker lock held | PASS — callback boundary and second-wake contention test |
| Queue/outstanding/reservation/owner/collection preservation | PASS |
| Worker and Adapter v1 retained inventories remain empty for this operation | PASS |
| Adapter Destroy rejects quarantine-only state | PASS |
| Negative publication-failure test | PASS - isolated, fail-before-mutation hook |
| Full multi-operation/worker-contention stress matrix | NOT RUN |
| Production registry and versioned diagnostics | NOT IMPLEMENTED |
| Bounded production Worker wait | NOT IMPLEMENTED |

**Gate 2 remains PARTIAL.** The positive test-only atomic publication seam is proven for the single-operation deterministic wake-failure fixture. Before production design acceptance, broaden queue membership validation beyond a single queue head and add multi-operation/Worker-contention coverage, broaden queue membership validation beyond a single queue head, and complete the bounded-wait and versioned-diagnostic gates. Do not reinterpret this test-only registry as production ownership/recovery support.

No retained record was released, unlinked, replayed, ACKed or reconciled. No Production behavior, ABI, or v1 inventory semantics changed.

## 2026-11-20 Gate 2 negative publication-failure evidence

Added a narrowly scoped `RAYA_ADAPTER_TESTING`-only failure hook. It rejects publication while the Adapter lock is held and before any quarantine-list/count/link mutation; it does not alter Worker state or synthesize ownership state. The existing Worker handoff callback then records OWNERSHIP_INVARIANT as a secondary cause while preserving the original WAKE_SIGNAL cause. A process-isolated named-pipe/overlapped-I/O case (`--quarantine-publication-failed`) proves: Worker exits DEGRADED; both expected causes are present; no quarantine record is falsely published; Adapter and Worker v1 retained inventories remain empty; queue/outstanding/reservation, operation owner/collection state, buffer, pipe, completion event and OVERLAPPED remain intact; issuer lease remains held; user callback remains suppressed. No destroy/recovery/cleanup is attempted; process exit reclaims fixture-local resources.

The prescribed API build script now runs this negative process-isolated case 10 times. Results under VS 2026 Build Tools 18.7.3 x64 `/W4 /WX`: negative case 10 x 43 = 430 PASS; positive Adapter API regression 10 x 1,089 = 10,890 PASS; strict dual-ACK 1,200 PASS; NOT_ISSUED 250 PASS; Managed Worker WAIT_FAILED 310 PASS; total 13,080 assertions PASS. Non-test-macro compile PASS; native Adapter suite 10 x 1,020 = 10,200 PASS. Script-declared EXE/OBJ outputs were cleaned and absence checked.

Gate 2 remains PARTIAL: the negative publication-failure requirement is now PASS, but multi-operation/worker-contention coverage and queue-membership validation beyond the single queue-head fixture remain outstanding. Production registry, versioned diagnostics and bounded production wait remain unimplemented. Test hook only; no Production behavior/ABI or v1 inventory semantics changed. No retained record was released, unlinked, replayed, ACKed or reconciled.

## 2026-11-20 Gate 2 multi-operation source trace

A bounded source trace was completed before changing the fixture. `dedicated-io-submitter-v2.c:125-177` removes and issues one request at a time, invokes the publish callback outside the Submitter lock, then returns to the queue loop; additional requests may remain queued while the first request is being published. `integrated-submit-adapter-v2.c:280-343` currently validates the transferred operation only when it is the Worker queue head and requires `core.queued == 1` and `core.outstanding == 1`. Therefore a deterministic two-operation case can be constructed without directly mutating internal queue state: suppress the first test wake while reporting it successful, enqueue a second real Adapter operation, then fail the second wake. The second handoff will see two committed Worker queue members; the current callback's single-head/single-count predicate will reject publication and mark OWNERSHIP_INVARIANT. This is the expected fail-closed limitation, not a passing multi-operation quarantine test.

The Worker uses an infinite production wait and the second failed wake does not itself signal it. Until Gate 3 bounded observation is implemented/tested, a multi-operation negative fixture must remain process-isolated and must not claim that the Worker exits. Do not manufacture queue links/counters or signal the Worker after the failure merely to make the test pass. The next valid test-only slice is to add deterministic first-wake suppression/second-wake failure, assert the two real queue members and fail-closed publication result, and terminate only by the dedicated process boundary; then separately implement and validate the approved production bounded-wait design. No Production change, ABI change, v1 inventory change, cleanup/recovery, or retained-record action is authorized by this source trace.

## 2026-11-20 Gate 2 deterministic multi-operation negative fixture

Added `--multi-operation-wake-failure`, compiled only by the existing `RAYA_ADAPTER_TESTING` API harness. The fixture holds the Worker thread at its pre-wait test hook before submitting any I/O. It then submits two independent real named-pipe READ operations through `raya_integrated_adapter_submit`; the first wake callback reports success without calling SetEvent, and the second wake returns ERROR_INVALID_HANDLE. No Worker queue link, count, reservation or operation state is written by the test. A four-handle test fixture capacity supports the two real operation handle pairs.

The process-isolated assertions prove that both operations are committed in the Worker queue (queued=2, outstanding=2, reserved_slots=2), each remains QUEUED and pipe-owner transferred, and the second publication is rejected by the existing single-head/single-operation callback. WAKE_SIGNAL and secondary OWNERSHIP_INVARIANT causes are present; the Adapter quarantine list remains empty; ordinary Adapter retained and Worker v1 retained inventories remain empty; callbacks remain suppressed; Adapter Destroy rejects the live state. The Worker is deliberately not asserted to exit: after releasing the pre-wait test gate it proceeds into the unchanged real INFINITE wait. The process boundary terminates the negative fixture without teardown, synthetic wake, cleanup, replay or reconciliation.

Validation: `build-integrated-submit-adapter-v2-api-test.cmd` passed 10/10 complete iterations with MSVC 2026 Build Tools 18.7.3 x64 and `/W4 /WX`. Per iteration: multi-operation negative fixture 20 PASS; quarantine publication failure 44 PASS; WAIT_FAILED 31 PASS; NOT_ISSUED 25 PASS; strict dual-ACK 120 PASS; full Adapter API regression 1,090 PASS. Aggregate: 13,300 assertions PASS. The script's EXE/OBJ outputs were absent after completion. This supersedes the earlier source-trace-only status for the multi-operation negative case.

Gate 2 remains PARTIAL: the test now proves the multi-operation fail-closed boundary and the current callback's inability to publish a second queued operation. It does not establish successful multi-operation quarantine publication or broaden production queue-membership validation. Production registry, versioned diagnostics and bounded production wait remain unimplemented. Stage 5.16 strict dual-ACK and Stage 5.19 v1 inventory contracts remain unchanged. No Production/ABI change or recovery action was made.

## 2026-11-20 Gate 3 bounded-wait source audit (no implementation)

A source-only review of `reaper-managed-worker-v2.c:541-640` confirms the intended timeout safety boundary:

- At the top of each Worker loop, while holding Worker lock, DEGRADED is checked before `ResetEvent`, queue promotion, owner transition, wait-plan build, or any operation claim. A DEGRADED Worker exits with `ERROR_INVALID_STATE` without touching queue-owned operations.
- After `WaitForMultipleObjects` returns, the Worker reacquires the same lock and resolves the wait result. `WAIT_TIMEOUT` resolves to `RAYA_WAITPLAN_TIMEOUT`; that branch checks DEGRADED under lock and exits if set. Otherwise it releases the lock and loops to rebuild the wait plan. It does not claim, dequeue, collect, release, unreserve, or mutate a retained/quarantine record.
- The wake-failure path marks DEGRADED while holding that same Worker lock. Therefore the timeout/handoff race is serialized: if timeout checks first, the next loop rechecks DEGRADED before queue processing; if handoff marks DEGRADED first, timeout observes it and exits.
- `raya_reaper_waitplan_resolve` maps `WAIT_TIMEOUT` directly to TIMEOUT before selecting any operation handle. No operation claim can be derived from a timeout result.
- Current `RAYA_ADAPTER_TESTING` builds already use a 100 ms finite Worker wait when no WAIT_FAILED injection callback is installed (`reaper-managed-worker-v2.c:607-610`). This is test-only behavior. The non-test Production branch still calls `WaitForMultipleObjects(..., INFINITE)` (`:612-614`). The 100 ms value is not a selected Production timeout and has not been measured for CPU wake overhead or completion latency.

Gate 3 remains PARTIAL / NOT PASS. The source contract is traced, but process-isolated runtime proof of Worker exit after a wake failure through the finite-timeout path, timeout-vs-handoff race coverage, normal idle/completion regression under that path, and wait-interval measurement are still outstanding. No Production source, ABI, lifecycle behavior, timeout value, ownership, recovery, or cleanup behavior was changed. Production finite-wait implementation remains blocked pending explicit owner authorization; the prior authorization covered design/documentation only.

## 2026-11-20 Gate 3 finite test-wait runtime exit evidence

Extended the process-isolated multi-operation wake-failure fixture to prove the Managed Worker exits after the transferred wake failure is observed through the finite test wait. The fixture retains its deterministic pre-wait gate so both real Adapter READ operations are committed before wake failure. Under RAYA_ADAPTER_TESTING, a non-failing installed wait hook now proceeds to the same 100 ms finite wait used by the no-hook test path; a hook that explicitly injects WAIT_FAILED still takes the existing failure branch. The non-test compilation branch remains WaitForMultipleObjects(..., INFINITE).

After releasing the gate, the test waits boundedly for the Worker thread and verifies its exit code is ERROR_INVALID_STATE. It reasserts the two queued operations, both reservations, QUEUED operation/owner states, WAKE_SIGNAL plus OWNERSHIP_INVARIANT causes, empty quarantine/retained/Worker Registry inventories, suppressed callbacks, and rejection of Adapter Destroy. No cleanup, synthetic wake, replay, release, unlink, or reconciliation is performed; the dedicated process boundary preserves fixture resources until process exit.

Validation: prescribed VS 2026 Build Tools 18.7.3 x64 /W4 /WX API build passed 10/10 iterations. Per iteration: multi-operation wake-failure/finite-wait exit 22 assertions; quarantine publication failure 44; WAIT_FAILED 31; NOT_ISSUED 25; strict dual-ACK 120; full Adapter API regression 1,090. Aggregate: 13,320 assertions PASS. Build script cleanup completed.

Gate 3 is now PARTIAL with the specific process-isolated finite-test-wait Worker-exit requirement PASS. Still outstanding: timeout-vs-handoff race runtime coverage, normal idle/completion regression under the finite test-wait path, and wait-interval measurement. The 100 ms interval remains test-only and is not a Production timeout selection. The non-test Production wait remains INFINITE. Gate 2 remains PARTIAL because successful multi-operation quarantine publication and broader queue-membership validation are not established. No Production behavior, ABI, recovery semantics, or v1 inventory semantics changed.