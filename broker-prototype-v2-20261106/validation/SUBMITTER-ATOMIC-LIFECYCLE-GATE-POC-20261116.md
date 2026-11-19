# Submitter Atomic Lifecycle Gate — Temporary Proof of Concept — 2026-11-16

## Scope
A standalone Win32 C proof-of-concept was built in a unique `%TEMP%` directory. It did not copy, compile, or modify the project Submitter implementation. The experiment validates only the atomic admission/close/drain/exclusive-cleanup primitive, not integration into `raya_dedicated_submitter`.

## Primitive exercised
- One aligned `volatile LONG` gate word, accessed only with Interlocked operations.
- Low bits count active entrants; `CLOSING` rejects new entrants; `CLEANUP` grants one cleanup owner; `DONE` marks terminal cleanup.
- Entrants use a CAS loop: either increment before close and become part of the drain count, or observe closing and reject.
- Cleanup owner atomically sets closing plus exclusive-owner bits, waits for entrant count to reach zero, performs one simulated resource close, then marks DONE.
- A second concurrent cleanup claimant is rejected. Subsequent entry and cleanup attempts after DONE are rejected.

## Deterministic race test
- Toolchain: Visual Studio 2026 Build Tools 18.7.3 x64, `/W4 /WX /TC`.
- 200 iterations.
- Four entrant threads per iteration are held after successful gate entry.
- First cleanup thread closes admission and waits for the four leases to drain.
- A second cleanup thread races for cleanup ownership and must lose.
- A late entrant is rejected while existing leases are still held.
- Assert no simulated resource close occurs before entrants drain; release all four entrants; verify exactly one cleanup winner, exactly one close, zero remaining entrants, DONE set, and no later entry/retry.
- Result: PASS, 2,600 assertions (13 checks × 200 iterations).

## Harness correction
The first harness draft incorrectly treated repeated waits on one manual-reset event as proof that four distinct entrants had arrived; that assertion failed. The harness was corrected to wait on the atomic admitted counter reaching four before starting cleanup. The corrected deterministic test then passed all 200 iterations. The failed draft and its executable were confined to the same temporary directory and removed with the successful run.

## Limits / blockers before integration
- This is not a proof of object-storage lifetime. A caller that begins after the containing object has been freed can still dereference freed memory before it can execute the gate CAS. The owner must keep storage alive and quiesce all possible callers.
- It does not test Windows `CloseHandle` ambiguity, partial two-Event cleanup, Submitter callbacks, `join` races, `acknowledge` waits, thread lifetime, queue state, or Integrated Adapter behavior.
- The actual Submitter has no independent stable gate storage today. Adding a gate inside the object still requires an external lifetime contract for callers that may begin concurrently with storage destruction.
- `join` needs its own exclusive operation state because it closes the thread HANDLE; the entrant count alone does not serialize two Join callers.
- Cleanup after partial close must stay permanently closed to ordinary APIs and use only the cleanup-owner path; it must never reopen the deleted critical section.
- Acknowledgement can wait indefinitely while holding an entrant lease. Cleanup must not delete synchronization resources while that lease exists.

## Decision
The primitive behaved as designed under the bounded deterministic race harness. This supports continuing with a temporary-copy integration design, but does not authorize changing project source. Before integration, define and enforce the object-storage owner/quiescence contract, serialize Join, and test partial close/retry plus all public API entry points and the Integrated Adapter regression. Sequential cleanup prototype remains non-integrable as-is.

## Cleanup and safety
- Temporary directory: `C:\Users\Ali\AppData\Local\Temp\RayaSubmitterGatePOC_6de2f04429df4e15932506a9b5ce1bb5`.
- Two temporary files (source and executable), 148,924 bytes, removed after successful test.
- No project source/header/test/build files modified; no project evidence deleted; no commits/resets; no Production/source-project changes; no TLC.