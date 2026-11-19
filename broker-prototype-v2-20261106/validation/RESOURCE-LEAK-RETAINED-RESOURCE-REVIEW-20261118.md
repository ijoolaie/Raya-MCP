# Resource Leak / Retained Resource Review — 2026-11-18

## Scope
Inspection-only audit of `reaper-managed-worker-v2.c/.h`, `reaper-integrated-v2.c/.h`, `integrated-submit-adapter-v2.c/.h`, `dedicated-io-submitter-v2.c/.h`, and existing API/native/fault-injection tests and retained-state reports. No production API, source, header, build script, or README changed. No build/test rerun was performed; existing targeted test evidence was reviewed rather than duplicated.

## Resource inventory and ownership

- Adapter allocates one envelope and one operation buffer per accepted submission; it owns a duplicated issuing-pipe HANDLE and a completion-event HANDLE through the operation finalizer. Normal terminal completion closes both handles, commits finalizer release, commits owner release, releases integrated reservation/accounting, acknowledges the Submitter request, invokes callback, then frees buffer and envelope.
- Submitter owns its worker thread HANDLE and wake-event HANDLE. Its queue/producer/active-issue/issuer-lease counters are drained before Destroy; request/envelope memory remains Adapter-owned. ACK releases the issuer lease. A failed ACK can therefore retain the Adapter envelope even after Worker-side resources have been released.
- Managed Worker owns its worker thread HANDLE and wake-event HANDLE. Per-operation issuing pipe and completion event are operation resources, not Worker-global handles. Worker retained list stores embedded operation nodes and therefore requires their containing envelope/storage to remain alive.
- Integrated Reaper accounts separately for queued/outstanding operations, reserved slots, and registry entries. Registry membership ends at terminal collection/terminal hold; outstanding/reservation accounting may intentionally remain held through finalizer/owner release.

## Normal release paths

- Immediate OS submission rejection / NOT_ISSUED: owner transitions to caller-terminal; finalizer closes both operation handles and commits; owner release and integrated unreserve complete; Worker caller lease is finished; callback runs and Adapter frees the envelope/buffer.
- Synchronous/local completion and ordinary asynchronous terminal success/abort converge on the same resource-release boundary. Existing API/native tests assert callbacks, both handle closes, zero Adapter retained/active counts, zero Worker outstanding/reservations/registry, zero Submitter queue/producer/active/lease counts, and successful ordered component teardown.
- Allocation failure before ownership transfer frees any allocated memory immediately. Event/duplicate-handle setup failures either free the envelope after successful cleanup or retain it if closing the created handle fails.
- Submitter normal shutdown/join/destroy drains queue, producers, active issue and issuer leases before closing its wake event and destroying its synchronization state. Worker Destroy similarly requires STOPPED, zero active/outstanding/queued/reserved/registry/retained state before closing its wake event.

## Retention classes — not ordinary leaks

1. **Pre-handoff / caller-uncertain or ownership invariant:** Adapter envelope and buffer remain linked in Adapter retained inventory. Depending on transfer ownership, operation may remain in Worker retained inventory or in queued/owned state. Issuing pipe/event may remain open. No callback/free is performed. This prevents freeing memory still reachable by an accepted OS I/O or worker/submitter path.
2. **Worker-owned pending I/O / uncertain completion:** Worker exits degraded; registry/outstanding/reserved accounting and operation resources remain held. Adapter envelope stays retained. This is intentional fail-closed quarantine because completion status is not safely replayable or releasable.
3. **Terminal finalizer close failure:** Terminal collection and terminal hold have occurred, registry membership is zero, but outstanding/reserved accounting remains held; finalizer is CLOSE_UNCERTAIN and callback is suppressed. A successful first close may mean one handle is already closed while the other remains open. No automatic retry/release is implemented.
4. **Post-Worker-release Submitter ACK failure:** Both operation handles, finalizer, owner and integrated reservation have been released; Worker outstanding/reserved/registry are zero. Nevertheless Adapter envelope/buffer remains retained because Submitter issuer lease/request ACK did not commit. The embedded operation is also kept in Worker retained inventory for lifetime safety. This is a memory-retention condition despite operation HANDLE release.
5. **Partial cross-plane commits:** Owner/finalizer/integrated release failures can leave different planes committed. Retention prevents use-after-free; it does not reconcile or roll back the already committed plane.

These retained states are not evidence of an accidental double-free or forgotten normal-path cleanup. They are deliberately unreclaimed quarantines. They do represent unbounded process-lifetime memory/handle retention if they occur and remain unresolved.

## Leak / cleanup risk findings

- No ordinary-path leak was identified by source inspection: the success and NOT_ISSUED paths have explicit close/commit/accounting/ACK/callback/free ordering.
- No safe generic cleanup path exists for retained envelopes. The public Adapter API exposes retained count, not production enumeration, reconciliation, release, or replay. The test-only retained snapshot requires quiescence and is not an atomic cross-component snapshot.
- A retained envelope remains allocated (including its buffer) and may retain one or both operation HANDLEs. Its storage cannot be freed while an OS completion, Worker list, Submitter request/lease, or callback context may still reference it.
- Adapter Destroy closes admission but returns BUSY while active or retained entries remain. Worker Destroy rejects nonzero retained inventory/accounting. Submitter Destroy rejects nonzero issuer leases and other in-flight counters. These gates prevent teardown from silently discarding reachable ownership.
- Submitter ACK failure after Worker release is a distinct memory-retention case: operation HANDLEs may already be closed, but envelope/buffer and embedded Worker retained node remain. Existing tests assert zero Submitter lease counters only in the successful ACK path; ACK-failure cases assert the retained envelope and failed teardown.
- The close-failure tests intentionally leave the Worker unjoinable/undestroyable after the worker thread exits degraded. Their test processes close independently owned pipe/client/event handles during test exit, but do not establish a production recovery mechanism for the retained operation.
- Startup failure paths attempt to close already-created wake events and delete critical sections; if a wake-event close itself fails during thread-factory failure, the Worker remains initialized/degraded rather than clearing state. This is fail-closed and requires external process-level handling; no generic recovery API was found.
- Submitter Destroy has explicit partial-destroy progress for wake-event/critical-section cleanup and retry semantics. Worker Destroy has retry for wake-event close failure, but Join/Destroy remain caller-serialized.

## Existing validation evidence reviewed

- `integrated-submit-adapter-v2-api-test.c`: allocation/setup failure, immediate read/write failure, sync read/write success, handoff failure after OS acceptance, ACK-signal failure after resource release, finalizer close failure, worker wake failure, destroy admission/retained gates, and retained snapshot validation.
- Latest retained snapshot cross-plane report: normal build 10 × 751 assertions = 7,510 passed; test-seam build 910 assertions passed. It explicitly validates the post-Worker-release ACK-failure retained state and close-failure retained state.
- `integrated-submit-adapter-v2-native-test.c` and `integrated-submit-adapter-v2-multi-native-test.c`: successful completion checks zero retained count and zero Submitter queued/producer/active/lease counters before teardown.
- `reaper-managed-worker-v2-abort-native-test.c`: terminal abort reaches one terminal collection, two successful closes, finalizer release, owner release and integrated release before callback.
- `reaper-managed-worker-v2-closefail-native-test.c`, `reaper-managed-worker-v2-pipeclosefail-native-test.c`, and `managed-pipe-io-v2-native-test.c`: close failure retains outstanding/reservation/operation ownership and suppresses callback; Destroy is blocked.
- Prior Stage 5.7 and 5.8 reports document terminal collection failure and Worker lifecycle cleanup behavior. Their targeted tests passed as recorded in those reports.

## Conclusion

Normal success, abort and NOT_ISSUED cleanup paths have explicit release ordering and existing counter/handle assertions. No ordinary-path leak was established in this inspection. The principal resource risk is intentional quarantine: uncertain/partial-commit/close-failure/ACK-failure cases retain heap envelope/buffer and, depending on the failure stage, operation HANDLEs and Worker/Submitter ownership state indefinitely. This is safer than premature free or replay, but is not recoverable in-process with current production APIs. The absence of production retained enumeration/reconciliation/release remains a blocking lifecycle capability gap.

No source changes, no build artifacts created, no cleanup performed, no Commit/Reset. Stage 5.9 review complete (inspection only).
