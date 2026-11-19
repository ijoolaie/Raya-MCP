# Submitter Cleanup Owner Serialization Prototype — 2026-11-16

## Result
PASS — disposable-copy component prototype; not integrated into project sources or public API.

## Test design
- Copied the actual Submitter C/H into a disposable directory.
- Added a temporary cleanup-owner atomic token using `InterlockedCompareExchange`.
- Retry accepted only the post-Join partial-close state: initialized, joined, DEGRADED, `wake_event == NULL`, `changed_event != NULL`, and empty queue/counters/leases.
- One cleanup thread acquired ownership and blocked inside the injected close callback. A second simultaneous cleanup thread was invoked while the first held ownership and was rejected without calling CloseHandle.
- The initial Destroy close and first owned retry each failed on the remaining Changed Event. The subsequent retry succeeded.

## Evidence
- 100 iterations; 13 successful control/assertion gates per iteration (1,300 total condition checks).
- x64 MSVC compile succeeded with `/W4 /WX /TC`.
- All 100 iterations passed.
- Concurrent contender did not close the handle, acquire cleanup ownership, or mutate the object while the first cleanup owner was active.
- Repeated failure retained the remaining handle; final retry closed it once and left `initialized == FALSE` and `cleanup_owner == 0`.

## Limitations
- This is a Submitter-only prototype. It does not validate Adapter→Submitter→Worker cursor progression, Owned/Borrowed behavior, caller admission/drain, or restart after rollback.
- It assumes external API callers have already been quiesced and the component storage remains alive until cleanup completes. A cleanup token alone does not solve late callers or object lifetime.
- Production API design still needs a documented cleanup state machine that covers failed-start and post-Join partial-destroy states, plus owner release on every failure path.

## Integrity
- No working-copy source/header/test/build file changed.
- Disposable directory was inspected and removed after testing.
- No Production change, Commit, or Reset.
