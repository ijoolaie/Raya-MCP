# WORKER FAILED-START CLEANUP OWNER GATE — TEMPORARY RACE REGRESSION — 2026-11-16

## Result
A temporary-copy prototype added an atomic cleanup-owner token for failed-start cleanup and passed a deterministic two-caller race test. No Working Copy source/header/test/build files were modified. No Production changes, commit, or reset.

## Temporary implementation
- Added a temporary `volatile LONG failed_start_cleanup_state` to the Worker structure.
- States: 0 = idle, 1 = cleanup owner, 2 = reserved/done (the successful path zeroes the structure, so 2 is not retained).
- Cleanup attempts acquire exclusive ownership with `InterlockedCompareExchange(state, 1, 0)`; a competing cleanup caller returns without touching the lock or Handle.
- The owner validates the failed-start/DEGRADED state and requires no thread, no join, no active callers, no queued/outstanding/reserved operations, no registry entries, and no retained entries.
- On close failure, the Handle remains owned and the cleanup token returns to idle for a later retry.
- On successful close, the event is nulled, the critical section deleted, and the object zeroed.
- The failed-start startup close in the temporary copy was routed through the configured close callback to enable deterministic failure injection.

## Race test
100 iterations. Each iteration:
1. Force thread factory failure and first wake-event close failure; verify initialized + DEGRADED state and retained Handle.
2. Start cleanup caller A and hold it inside the injected close callback after it has acquired the owner token.
3. Start cleanup caller B while A owns cleanup; verify B returns false without entering the close callback or touching the Handle.
4. Release A; its close attempt fails and leaves the object/Handle intact.
5. Retry cleanup serially; close succeeds and the object is reset.

Results:
- 100/100 iterations passed.
- 1,100 assertions passed.
- Visual Studio 2026 Build Tools 18.7.3 x64, `/W4 /WX /TC`.
- No runtime assertion failures.

## Important limitation
The owner token serializes overlapping cleanup attempts while the Worker object remains alive. It does not make object-storage lifetime self-managing: the owner must prevent new calls and quiesce all possible callers before successful cleanup zeroes the object and deletes its critical section. A late caller racing after successful destruction is outside this prototype's guarantee. This is not a complete general-purpose lifetime gate and is not merge-ready.

## Harness corrections
Three initial race harness attempts exposed harness synchronization mistakes (literal newline insertion in a temporary header; a close callback blocking during the synchronous startup-failure close; and both cleanup threads sharing one start event). Those temporary harnesses did not pass and were not counted. The final harness used separate start gates and blocked only the second close callback; its 100-iteration run passed.

## Cleanup and unchanged project
Four disposable temporary directories were removed after inspection:
- 140 files / 638,328 bytes
- 143 files / 821,734 bytes
- 143 files / 822,644 bytes
- 143 files / 822,788 bytes

No active compiler, linker, test, or TLC process remained.

Working Copy hashes unchanged:
- reaper-managed-worker-v2.c: FDCD768B31CD6FF30F435A5588D6E1000394A7B355A2E37C1A8117D6C78225E4
- reaper-managed-worker-v2.h: F45DB7509C2045A37925A86BE06F4871109BA5348A383B75447ACD3C46423818
- dedicated-io-submitter-v2.c: D8ABAFD8D3B731DA141E8B1792534A00EB888055D0F15EB0EDFCCE2D01843941
- integrated-submit-adapter-v2.c: 6E2427C09974EE47B382F80CD11B21FEAAC16D12ECABC6A6155A11E9E8F62FBE

Disk free after cleanup: C 13,545,758,720 bytes; D 2,111,705,088 bytes.

## Next step
Test the Worker cleanup retry together with Submitter failed-start cleanup at the Adapter startup rollback boundary, still in a disposable copy. Verify partial cleanup failures on both components, retry order, retained HANDLE ownership, and successful fresh startup. Do not merge or claim Production readiness.

Production remains NO-GO; Phase 1 remains NOT ACCEPTED.
