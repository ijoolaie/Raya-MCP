# Orchestrator Admission-Close vs Caller-Entry CAS Race Stress — 2026-11-16

## Result
PASS — 1,000 iterations, 8 concurrent caller threads per iteration, 10,000 explicit CHECK assertions. Disposable standalone Windows C harness; MSVC x64 `/W4 /WX /TC /O2`. This is a gate-algorithm stress test, not a checked-in Orchestrator or component API integration test.

## Race under test
- Each caller attempts to acquire an entrant lease using a compare/exchange loop against one atomic gate word.
- Cleanup concurrently sets the high CLOSED bit with an interlocked OR. The successful CAS and close operation are the linearization points.
- If a caller's increment CAS wins first, its lease is counted and it must drain before storage teardown. If the close wins first, the caller's stale CAS fails and it observes CLOSED on retry, so it is rejected.
- After closure, the harness explicitly attempted another entry and verified rejection. It released admitted callers, joined all eight threads, and checked the low-bit entrant count and independent active count both reached zero.
- The control block remained allocated until all eight threads were joined. The test does not attempt to make a dangling raw pointer safe.

## Evidence
- Output: `PASS iterations=1000 callers_per_iteration=8 checks=10000 late_rejects_verified=1000 active_drained=1000 post_close_admissions=0`.
- 1,000/1,000 iterations passed; no timeout or unexpected post-close CAS admission.

## Harness correction transparency
- Initial compile was rejected under `/WX` for one unused local variable; the harness was corrected and rebuilt.
- An initial diagnostic counter incorrectly classified a caller whose lease CAS had won before closure, but whose subsequent diagnostic load observed the CLOSED bit, as a post-close entrant. That observation is not the admission linearization point. The invalid diagnostic was removed; the final test checks the CAS ordering, closed-state rejection, joined callers, and zero remaining leases. No product source was implicated or modified.

## Scope limits
- Standalone gate algorithm only; does not add a gate to Submitter/Worker or implement a checked-in Orchestrator.
- Does not test memory reclamation while arbitrary external raw pointers may still exist. The stable handle/control-block lifetime must be guaranteed by an external owner or registry/refcount protocol whose own lifetime is independently valid.
- Does not supersede the actual Adapter entrant integration tests or chained cleanup failure gates.

## Integrity and cleanup
- Temporary directory contained $($items.Count) files and $bytes bytes; all source, object, executable, and harness files were inventoried and removed.
- No Working Copy source/header/test/build/model/archive/Production file was modified. No Commit or Reset; no process was terminated.
