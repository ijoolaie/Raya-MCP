# Submitter Integrated Atomic API Gate and Race Tests — 2026-11-16

## Scope

A temporary copy of `dedicated-io-submitter-v2.c/.h` was used. No project source, header, test, or build file was modified. The prototype integrates an atomic API entrant gate, a separate exclusive Join token, and serialized partial-destroy retries. It is not a production patch.

## Prototype mechanics

- `api_gate` low 24 bits count public API entrants; high bits encode CLOSING, CLEANUP_OWNER, DONE, and PARTIAL_CLEANUP.
- API entry uses an Interlocked CAS before reading `initialized`, entering the Submitter critical section, or touching events/thread handles. API exit decrements the entrant count.
- `admit`, `enqueue`, `abort_admission`, `acknowledge`, `shutdown`, `join`, and `snapshot` use entry/leave wrappers.
- `join_busy` is a separate Interlocked token so only one caller can wait on/close the thread HANDLE and publish `joined/thread=NULL`.
- `destroy` closes admission, claims the cleanup owner, drains existing API entrants, validates joined/thread/counters, and closes only non-NULL handles. Successful closes null the corresponding field. A failed close leaves the gate closed and releases only the cleanup-owner bit so a serialized retry can continue. After partial close, retry accepts the narrowly tagged degraded cleanup state.
- `degrade_locked` in the temporary copy skips NULL event handles.
- `start/start_with_api` remain construction operations and are assumed externally serialized. Object storage lifetime is still an external owner responsibility.

## Test evidence

Toolchain: Visual Studio 2026 Build Tools 18.7.3 x64, `/W4 /WX /TC`.

| Suite | Executions | Assertions |
|---|---:|---:|
| Existing Win32 failure matrix | 10 | 1,640 |
| Existing native Submitter | 10 | 180 |
| Existing publish/ACK race | 10 | 220 |
| Existing multi-producer | 10 | 1,370 |
| Existing backpressure | 10 | 290 |
| New integrated gate race harness | 10 | 14,149 |
| **Total** | **60** | **17,849** |

All 60 executions passed.

The new race harness covers:
- two concurrent Join calls: exactly one can own the Join token;
- Join versus Destroy: 100 concurrent start races per execution, with retry after Destroy correctly declines because Join has not yet completed;
- two concurrent Destroy calls: exactly one cleanup succeeds, with only the two event handles closed;
- ACK waiting while request is ISSUING: Destroy closes admission and waits for the active ACK entrant; after the request transitions to ISSUED and the notification is signaled, ACK completes, Destroy observes not-yet-joined state and reopens admission;
- partial two-event cleanup: first event closes, second close is injected to fail, the object remains initialized/degraded with the first handle NULL and the second retained, ordinary APIs are rejected, and retry closes only the remaining handle.

## Defect caught during the prototype

An early flag layout placed `PARTIAL_CLEANUP` inside the entrant-count mask, causing the drain loop to wait forever after partial cleanup. The harness exposed this immediately. The count field was narrowed to 24 bits and all four flag bits moved outside it; the complete test matrix was rerun and passed. This is an important reason not to promote the prototype directly.

## Limits / unresolved gates

- This does not prove object-storage lifetime safety. The owner must keep the struct memory alive until all possible callers are quiesced; an atomic field cannot protect a caller that dereferences already-freed storage before its CAS.
- `start/start_with_api` and restart/construction remain externally serialized and are not covered by the entrant gate.
- The harness does not cover cleanup of partially failed startup before a worker thread exists; the current API has no dedicated public cleanup operation for that state.
- The injected `CloseHandle` failure contract is a fake API contract, not proof that every real/custom close failure has identical ownership semantics.
- No Integrated Adapter end-to-end regression, Application Verifier/page-heap run, callback-reentrancy matrix, or production code change was performed.
- ACK may wait indefinitely while holding an API entrant; Destroy correctly waits rather than cancelling it. The owner must arrange the state transition/notification or accept that cleanup cannot progress.
- A caller must not free the object after a failed Destroy; retries require the same storage to remain alive.

## Decision

The temporary integration demonstrates a workable direction for API admission, Join serialization, and serialized partial cleanup under the tested cases. It does not close the external storage-lifetime contract or failed-start cleanup gap. Keep the design in investigation only; do not merge or use in Production. Production remains NO-GO and Phase 1 remains NOT ACCEPTED.
