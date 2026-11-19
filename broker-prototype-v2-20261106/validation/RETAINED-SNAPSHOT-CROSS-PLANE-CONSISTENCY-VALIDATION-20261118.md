# Retained Snapshot Cross-Plane Consistency Validation — 2026-11-18

## Scope
Strengthened the test-only `validate_retained_snapshot()` in `integrated-submit-adapter-v2-api-test.c`. No production API, component lifecycle transition, retry/release behavior, or resource ownership behavior was changed.

## New consistency checks
- Bound owner terminal collection/release commits and finalizer successful-close/release commits to their valid maxima.
- Require the explicit terminal-collection snapshot bit to agree with the owner terminal-collection count.
- Reject finalizer or owner release commits before terminal collection.
- A committed finalizer release now requires exactly two successful closes and the RELEASED state; RELEASED state and its commit counter must agree.
- An owner release commit requires finalizer release and owner RELEASED state; owner RELEASED state and its commit counter must agree.
- Integrated reservation RELEASED requires both finalizer and owner release commits and zero matching integrated-registry entries.
- Adapter's worker-terminal-released flag requires the complete Worker-side finalizer/owner/reservation release chain and zero registry matches.

## Legitimate retained states preserved
- Pre-handoff caller-uncertain retention: no terminal collection or release commits; Worker membership remains represented.
- Worker-owned pending I/O: Adapter-only retained entry, no terminal collection or release commits; reservation remains HANDED_OFF.
- Post-terminal finalizer close failure: terminal collection recorded, caller terminal owner state, finalizer CLOSE_UNCERTAIN, zero successful closes/release commits, reservation HELD. This remains a valid retained state.
- Post-Worker-release Submitter ACK failure: both finalizer and owner release committed, reservation RELEASED, registry match count zero, while Adapter and Worker retained inventories may both still contain the operation.

## Negative regression cases
Added validator checks proving it rejects (a) finalizer release with fewer than two successful closes and (b) owner release commit missing after finalizer release.

## Validation
- Normal build: `build-integrated-submit-adapter-v2-api-test.cmd`, Visual Studio 2026 Build Tools 18.7.3, x64, `/W4 /WX /TC`.
- 10 consecutive runs, each 751 assertions across 66 cases passed; total 7,510 assertions; exit code 0.
- Test-seam build with `/DRAYA_ADAPTER_TESTING`: 910 assertions across 68 cases passed; exit code 0.
- A first seam compile exposed C4389 signed/unsigned comparison under `/WX`; comparison was explicitly cast and both seam and normal builds were rerun successfully.
- The failed seam compile's exact generated object files were removed (13); the successful seam run's exact temporary executable/object files were removed (15).
- Final expected temporary artifact count: 0.
- Final observed free space: C 12.91 GB; D 4.48 GB.

## Changed file
- `integrated-submit-adapter-v2-api-test.c`: 48,127 bytes; SHA-256 `0BCB96CE0933B9A18714709DB743645124C1C456CA074AEAFC7BEBCD8F0A3FEA`.

## Limitations
- The snapshot is test-only, requires quiescence, and samples component locks separately; it is not an atomic cross-component snapshot.
- Worker retained inventory matching is identity membership, not proof of recipient-side commit.
- Terminal collection and completion error/byte fields are local lifecycle evidence, not a recipient receipt.
- No retained-resource enumerate/reconcile/release/replay production API, durable recovery, or safe retry of uncertain WRITE is introduced or demonstrated.
- No README, build script, or production source/header was changed. No commit or reset was performed.
