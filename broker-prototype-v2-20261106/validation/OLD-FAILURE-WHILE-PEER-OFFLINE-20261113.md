# AppendEntries failure response while its peer is offline — 2026-11-13

## Purpose

Check the online-state guards in the abstract AppendEntries response handler. The focused scenario is a queued failure response from F to leader A, where F becomes offline before A handles the response.

The response deliberately matches A's current term (3) and last request ID for F (4). The test therefore isolates the requirement that the response source must be online at handling time.

## Positive model

- Model: `WitnessV2AppendEntriesOldFailureWhilePeerOffline.tla`
- Config: `WitnessV2AppendEntriesOldFailureWhilePeerOffline.cfg`
- TLC: 2026.11.06.222357 (revision d8fb231), Java 25.0.2
- Result: PASS; exit code 0; no invariant violation.
- State space: 8 generated, 5 distinct, 0 queued; complete graph depth 3; maximum outdegree 3.
- Invariants: `TypeOK`, `NoResponseAcceptedWhilePeerOffline`, `AcceptedResponseRequiresPeerOnline`.

In the positive model, the response cannot be handled while F is offline. It may be handled after F is restored, provided the leader role, term and request ID still match. The model also permits dropping the queued response.

## Negative control: source-online guard removed

- Model: `WitnessV2AppendEntriesOldFailureWhilePeerOfflineWithoutPeerOnlineGuard.tla`
- Config: `WitnessV2AppendEntriesOldFailureWhilePeerOfflineWithoutPeerOnlineGuard.cfg`
- Expected result observed: invariant violation, exit code 12.
- Violated invariant: `NoResponseAcceptedWhilePeerOffline`.
- State space at counterexample: 6 generated, 5 distinct; trace depth 3.
- Trace:
  - `WitnessV2AppendEntriesOldFailureWhilePeerOfflineWithoutPeerOnlineGuard_TTrace_1794573544.tla`
  - `WitnessV2AppendEntriesOldFailureWhilePeerOfflineWithoutPeerOnlineGuard_TTrace_1794573544.bin`

Counterexample:
1. Initially A and F are online; A is Leader in term 3. The queued response is a failure from F, term 3, requestId 4.
2. F is taken offline; A remains online and remains Leader.
3. The mutant handles and consumes F's response while F is offline. `acceptedWhilePeerOffline` becomes TRUE.

Only the source-online precondition was removed. The receiver-online, role, term and request-ID conditions remain.

## SHA-256

| Artifact | SHA-256 |
|---|---|
| Positive model | `D24B06D2416D3F19D59418152788C99135CE7787F60E0C596C0AAD2D6832B3A6` |
| Positive config | `2B0208CE763DE49C9AA4BEC13AAA88F3A87130B339BD0E133C70D00A983AC82F` |
| Mutant model | `8C91676E2C91F4E5D33C081C5D45C40C74848C2BBA56416C718FCB255CEDAD78` |
| Mutant config | `2B0208CE763DE49C9AA4BEC13AAA88F3A87130B339BD0E133C70D00A983AC82F` |
| Counterexample trace (.tla) | `FAA508BBE973976D96410DF1415A17C6A1D96759A8F8B78546C44C09F60F2198` |
| Counterexample trace (.bin) | `AD6776E9DBF5103555B383CFD5882745395AB6875F22FF1C457BEE1D2B1E4CEE` |

## Interpretation and limitations

This confirms the modeled source-online guard is effective: without it, the queued response can be consumed while its source is offline. It does not establish that every implementation should retain messages during a network partition or that an offline node's messages are authentic or current; those are outside this abstraction.

Crucially, `SetOnline` in the base model changes reachability only. It does not represent process crash/restart, volatile-state loss, re-election, or durable recovery. This result must not be interpreted as a crash-recovery proof or as a full Raft proof.

## Change boundary

Only the four model/config files, this report, and TLC's counterexample trace files were created for this check. No base model, source code, Production code, README, service, tunnel, or archive was changed. No files were deleted, moved, committed, or reset.
