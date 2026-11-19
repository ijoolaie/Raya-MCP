# AppendEntries response from an offline peer — source-online guard witness (2026-11-13)

## Purpose

Check the source-online precondition in the abstract `HandleAppendResponse` action. The modeled sequence is deliberately constrained:

1. A is a leader in term 3. An old failure response from F to A is queued (term 3, requestId 4).
2. F transitions offline while the response remains queued.
3. The response is considered only after F is offline. A remains online and leader.
4. The response uses requestId 4, equal to A's lastRequestId for F, so the requestId guard does not reject it independently.

If the source-online guard is incorrectly removed, the old failure is accepted and A's nextIndex decreases from 6 to 5.

## Positive model

- Model: `WitnessV2AppendResponsePeerOfflineGuard.tla`
- Config: `WitnessV2AppendResponsePeerOfflineGuard.cfg`
- TLC: 2026.11.06.222357 (revision d8fb231), Java 25.0.2
- Result: PASS; exit code 0; no invariant violation.
- State space: 5 generated, 4 distinct, 0 queued; complete graph depth 3; maximum outdegree 2.
- Invariants: `TypeOK`, `CursorLowerBound`, `OfflinePeerResponseNotAccepted`.

The response cannot be handled after F goes offline because the base action's source-online precondition (`"F" \\in online`) is false. The response can instead be dropped.

## Negative control: source-online guard removed

- Model: `WitnessV2AppendResponsePeerOfflineGuardWithoutSourceOnlineGuard.tla`
- Config: `WitnessV2AppendResponsePeerOfflineGuardWithoutSourceOnlineGuard.cfg`
- Expected invariant violation observed; TLC exit code 1.
- Violated invariant: `OfflinePeerResponseNotAccepted`.
- State space at counterexample: 4 generated, 4 distinct; trace depth 3.
- Trace:
  - `WitnessV2AppendResponsePeerOfflineGuardWithoutSourceOnlineGuard_TTrace_1794573965.tla`
  - `WitnessV2AppendResponsePeerOfflineGuardWithoutSourceOnlineGuard_TTrace_1794573965.bin`

Counterexample:
1. Initial state: A and F online; the old failure response is queued; nextIndex is 6.
2. `TakePeerOffline`: F goes offline; the old response remains queued; nextIndex stays 6.
3. The mutant accepts the old response while F is offline; nextIndex changes 6→5 and `acceptedFailure` becomes TRUE.

The mutant retains the phase constraint (F must already be offline), destination-online guard, leader-role guard, term equality, and requestId equality. Only the source-online check is removed.

## SHA-256

| Artifact | SHA-256 |
|---|---|
| Positive model | `3672A911ADF4B861F9F33D2390EB3D7C17477A9D58D997C7AA9DA51199B638FB` |
| Positive config | `B10ADC72D6D09B56D76940BDBDB730A36873F8B934F474A04BC7C059147524D5` |
| Mutant model | `613C05C393D127C5490FB8D2406FAAA795F36A8F998922E3EA9D4DDE7756735D` |
| Mutant config | `B10ADC72D6D09B56D76940BDBDB730A36873F8B934F474A04BC7C059147524D5` |
| Counterexample trace (.tla) | `67BFE79968DC39B65B955EB18D17D511F7E97032E54DC707CC0991BDE0A8EFA4` |
| Counterexample trace (.bin) | `2F8A406A624242B69B9002CB0765FE7C8F9E90B678C1B1409EC436AF1866107A` |

## Diagnostic history

An initial TLC attempt before adding the explicit offline-phase constraint found a shorter path that handled the response while F was still online. That run was not used as evidence. Its trace and metadata are retained; no diagnostic files were deleted. The final positive and negative runs above were rerun with the phase constraint present in both models.

## Scope and limitations

This is a bounded, abstract witness for the source-online precondition of response handling. It does not model transport delivery semantics, process restart, full Raft, election safety, durable state, or production code. In particular, it assumes the response remains queued while F transitions offline; this is a modeling scenario for isolating the guard, not a claim about a particular network transport.

## Change boundary

Only the two model/config pairs and this report were created or updated, plus TLC trace/metadata output. No source/base model, Production code, README, running service, tunnel, or archive was changed. No files were deleted, moved, committed, or reset.
