# Integrated Submit Adapter — concurrent Submit/Destroy admission test (2026-11-15)

## Scope
Working copy only:
`D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`

No Production changes, no source-project changes, no Git commit/reset, and no historical evidence deletion.

## Change
- Added an interlocked lifecycle word to `raya_integrated_submit_adapter`: a closing bit and an entrant count.
- Submit and retained-count query acquire an entrant lease before touching adapter state and release it on exit.
- Destroy atomically closes admission. It refuses to delete the critical section while entrants remain or while active/retained envelopes exist. A later serialized destroy call may succeed after drain.
- Documented caller obligations: serialize destroy calls, keep adapter storage alive, quiesce possible callers, and do not begin calls after successful destroy.
- Added a deterministic barrier test with eight Submit threads blocked inside the injected CreateEvent callback. Destroy is attempted while all eight are admitted; it must fail without deleting the lock. New Submit is rejected after closure. Releasing the barrier makes all eight calls unwind; active count and entrant count return to zero; a subsequent Destroy succeeds.

## Validation
### API regression
Build script: `build-integrated-submit-adapter-v2-api-test.cmd`

- Visual Studio 2026 Build Tools 18.7.3, x64, `/W4 /WX /TC`.
- 703 assertions across 65 cases per execution.
- 10 executions passed: 7,030 assertions / 650 case-executions.
- Exit code 0.
- No target executable or object-file leftovers after script cleanup.

### Native integration regression
Build script: `build-integrated-submit-adapter-v2-multi-native-test.cmd`

- PASS: 809 assertions; 70 integrated live operations, 7 exited producers, completion window >63.
- Exit code 0.
- No target executable or object-file leftovers after script cleanup.

## SHA-256
- `integrated-submit-adapter-v2.c`: `7820B88AC535D2657C36AD2C59F0EED1B2A257AB692E90D9C63972C4B2B265EF`
- `integrated-submit-adapter-v2.h`: `4D9BF774029DD28899EDEA9A8972BF2457CE76FD66AA8C358287C07B141C2400`
- `integrated-submit-adapter-v2-api-test.c`: `9CA125574DBEFE43FABF3CC90E3E5870AED05755767C77F44D1348F7C0F19784`

## Limitations / acceptance
This deterministic test proves the entrant-count protection for calls that have acquired the in-object lease before Destroy closes admission. It does not make arbitrary use-after-destroy safe: callers must still keep the adapter storage alive and prevent calls from starting after successful Destroy. Concurrent Destroy calls are not supported and must be serialized. A wider stress/soak run and full lifecycle integration remain outstanding.

No Production acceptance is implied. Production remains NO-GO; Phase 1 remains NOT ACCEPTED.
