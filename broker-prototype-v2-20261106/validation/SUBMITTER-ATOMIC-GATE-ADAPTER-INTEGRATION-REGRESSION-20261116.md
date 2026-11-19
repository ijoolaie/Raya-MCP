# Submitter Atomic Gate — Adapter Integration Regression — 2026-11-16

## Scope and isolation

Integrated the temporary Submitter atomic API gate prototype into a temporary copy of the actual project C/H files and compiled it with the real Integrated Adapter, Managed Worker, Reaper, and Adapter API test suites. All edits and build outputs remained under a unique `%TEMP%\RayaSubmitterAdapterGate_*` directory. No project source/header/test/build file was modified.

The temporary Submitter copy added:
- an Interlocked entrant counter with CLOSING, CLEANUP_OWNER, DONE, and PARTIAL_CLEANUP flags;
- API entry/leave wrappers for admit, enqueue, abort-admission, acknowledge, shutdown, join, and snapshot;
- a separate Interlocked `join_busy` token for exclusive thread-HANDLE join/close;
- serialized Destroy ownership, API drain, joined/thread/counter validation, and per-handle partial-close retry;
- NULL guards in degraded signaling after an event handle has already been closed.

Construction (`start/start_with_api`) remains externally serialized. The temporary gate does not solve object-storage lifetime; callers/owner must keep the object memory alive and quiesce possible callers before freeing it.

## Existing Integrated Adapter test suites

Toolchain: Visual Studio 2026 Build Tools 18.7.3 x64, `/W4 /WX /TC`.

| Suite / build mode | Runs | Assertions per run | Total assertions |
|---|---:|---:|---:|
| Adapter API, normal mode | 10 | 724 / 66 cases | 7,240 |
| Adapter API, `RAYA_ADAPTER_TESTING` | 10 | 858 / 68 cases | 8,580 |
| Adapter multi-native, `RAYA_ADAPTER_TESTING` | 10 | 809; 70 live operations, 7 producer exits, completion window >63 | 8,090 |
| **Subtotal** | **30** | | **23,910** |

All 30 executions passed. Both macro and non-macro builds compiled cleanly with warnings treated as errors. The normal build preserves the baseline path; the test build also exercises the existing test-only Worker/Adapter diagnostic and wake seams.

## Direct Submitter lifecycle race harness

A separate temporary harness used the same temporary Submitter implementation and real Win32 synchronization. Ten executions passed, covering:
- two simultaneous Join calls; exactly one Join token winner;
- 100 Join-vs-Destroy races per execution;
- 50 two-Destroy races per execution; one cleanup owner and exactly two event closes;
- ACK entrant blocked in the ISSUING state while Destroy closes admission and drains entrants; ACK completes after state publication/notification, and Destroy safely declines because Join has not yet completed;
- partial close where one event closes and the second close fails; ordinary API calls are rejected while the gate remains closed, and retry closes only the remaining handle.

The ten race-harness executions passed with 14,149 assertions total (per-run counts vary with scheduling).

## Combined result

- 40 executable runs across the four test suites.
- 38,059 assertions total.
- All runs passed.

## Interpretation and limits

This is evidence that the prototype gate did not break the existing Adapter API and 70-operation multi-native regression, and that the focused Join/Destroy/ACK/partial-close races behaved as expected on this host.

It is not a production approval. Remaining gaps:
1. Object storage lifetime is external; a gate inside the object cannot protect a caller that touches already-freed memory before its first CAS.
2. Start/restart construction remains externally serialized and was not raced.
3. Failed-start cleanup before thread creation is not covered by this integration run.
4. No Application Verifier/page-heap run or full callback reentrancy/deadlock matrix.
5. No production source was changed, and no claim is made that the prototype is ready to merge.

Decision: retain as temporary proof-of-concept only. Production remains NO-GO; Phase 1 remains NOT ACCEPTED.
