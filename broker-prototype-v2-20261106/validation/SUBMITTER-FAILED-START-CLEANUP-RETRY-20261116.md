# Submitter Failed-Start Cleanup Retry — 2026-11-16

## Scope

A temporary copy of the actual Submitter and dependent project sources was used. The prototype added an explicit `raya_submitter_cleanup_failed_start` operation, an atomic API-entry gate, and a serialized cleanup owner. No project source/header/test/build file was changed.

The new cleanup path accepts only the narrow failed-start state: `initialized`, `DEGRADED`, `started == FALSE`, `thread == NULL`, partial-cleanup marker set, and all queue/producer/issue/issuer counters empty. It closes only non-NULL event handles, nulls a handle only after successful close, retains failed handles for retry, deletes the critical section only after all handles close, and leaves the API gate closed after cleanup succeeds. A failed retry leaves the gate closed but releases cleanup ownership so another retry can be attempted. Ordinary API entry is rejected during/after failed-start cleanup.

The prototype marks partial cleanup in both relevant startup failure branches: second Event creation fails and closing the first Event fails; thread creation fails and one or more Event closes fail. If startup fails but cleanup succeeds immediately, the existing zeroed/uninitialized behavior remains.

## Focused failed-start evidence

Toolchain: Visual Studio 2026 Build Tools 18.7.3 x64, `/W4 /WX /TC`.

- Three deterministic cases: second Event creation failure + first Event close failure; thread creation failure + first Event close failure; and a subsequent cleanup retry whose close also fails before a later retry succeeds.
- 25 executions; 20 assertions per execution; **500 assertions PASS**.
- Confirmed no worker thread was created, retained handles were not lost, APIs were rejected while cleanup was pending, and successful cleanup nulled handles and marked the object uninitialized/done.

## Existing regression suites

| Suite | Runs | Assertions per run | Total |
|---|---:|---:|---:|
| Existing Submitter Win32 failure matrix | 10 | 164 | 1,640 |
| Integrated Adapter API, normal mode | 10 | 724 | 7,240 |
| Integrated Adapter API, test mode | 10 | 858 | 8,580 |
| **Total** | **30** | | **17,460** |

All runs passed; all builds used `/W4 /WX /TC`.

## Combined evidence

- 55 executions across focused and regression suites.
- **17,960 assertions passed.**
- No compiler warnings/errors.
- The focused failed-start retry was tested against the injected Win32 API; the Adapter regressions exercised normal Submitter startup/shutdown and existing Adapter paths, not an injected failure of Adapter-owned Submitter startup.

## Limits and decision

- This remains a temporary proof-of-concept. No public API or struct layout change is approved for the project.
- The owner must keep object storage alive until all potential callers are quiesced; an in-object gate cannot protect memory already freed before a caller's first access.
- Construction/restart remains externally serialized.
- The new cleanup operation is not yet wired into Integrated Adapter initialization rollback. Adapter-level injected startup-failure cleanup and caller ownership rollback need a dedicated test before considering integration.
- Custom `close_handle` failure semantics are modeled by a fake API that returns FALSE without closing the handle; this is not a universal proof for arbitrary custom implementations.
- No Application Verifier/page-heap or callback-reentrancy suite was run.

Decision: the narrow failed-start cleanup retry behavior is validated in a temporary copy, but integration into the actual Adapter startup rollback is still an open item. No Production change or acceptance is warranted. Production remains NO-GO; Phase 1 remains NOT ACCEPTED.
