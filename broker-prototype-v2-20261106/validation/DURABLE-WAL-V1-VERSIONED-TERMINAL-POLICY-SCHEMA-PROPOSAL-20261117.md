# Durable WAL v1 - Versioned Operation and Terminal Policy Schema Proposal
Date: 2026-11-17
Status: design proposal only; policy not approved; no source or format changes.

## 1. Scope and inspected evidence
Working Copy: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`.
Reviewed `raya-durable-contract-v1.h`, `raya-durable-wal-v1.h/.c`, `raya-durable-runtime-v1.h/.c`, `integrated-submit-adapter-v2.c`, `dedicated-io-submitter-v2.c`, and the existing versioned callback and WAL design reports.
A recursive search of non-durable `.c/.h` files found no `operation_kind =` assignments and no `RAYA_DURABLE_OPERATION_*` callsites. Existing durable operation-kind assignments occur in durable test fixtures only. The current prototype therefore has no production caller-to-kind mapping to ground a per-kind terminal policy.

Adapter facts:
- `adapter_issue` dispatches ReadFile for read envelopes and WriteFile for write envelopes.
- TRUE and ERROR_IO_PENDING map to legacy ISSUE_ACCEPTED. Other errors map to ISSUE_NOT_ACCEPTED using GetLastError.
- `adapter_publish` calls `raya_managed_worker_handoff` (or the test hook variant). NOT_TRANSFERRED retains/quarantines the envelope; successful handoff transfers internal ownership to Worker.
- Worker completion later calls the request completion callback and acknowledges the Submitter request. This is local completion/ownership lifecycle, not a recipient durable receipt.
- No recipient-side idempotency, durable receipt, or status-query protocol was found in this path.

## 2. Required semantic planes
Keep these independent in every future schema:
1. ISSUE: `PROVEN_NOT_SUBMITTED`, `OS_ACCEPTED`, `UNKNOWN`. This is native OS submission evidence only.
2. HANDOFF: `TRANSFERRED`, `NOT_TRANSFERRED`, `UNKNOWN`. This is Worker ownership evidence only.
3. RECIPIENT: `COMMITTED`, `PROVEN_NOT_COMMITTED`, `UNKNOWN`. This plane is unavailable in the current implementation and must not be synthesized from ISSUE, HANDOFF, local completion, ACK, or local journal state.

The existing WAL's generic effect results (`COMMITTED`, `PROVEN_NOT_COMMITTED`, `UNKNOWN`) cannot safely be interpreted as a single business outcome across ISSUE and HANDOFF. A future schema should encode the evidence plane explicitly, or map each plane to an immutable, versioned evidence-type identifier. Do not silently reinterpret existing v1 records.

## 3. Terminal scope must be explicit
Propose three non-interchangeable terminal scopes:
- `LOCAL_LIFECYCLE_TERMINAL`: this process-local request no longer has work owned by the Submitter/Adapter/Worker. It says nothing about recipient application.
- `TRANSPORT_TERMINAL`: the transport/OS operation reached a documented terminal state. It still does not prove recipient application or durable commit.
- `RECIPIENT_COMMIT_TERMINAL`: only available with an independently verifiable recipient receipt/status protocol bound to operation identity and payload digest.

A terminal record must carry a `terminal_scope`, `policy_id`, `policy_version`, `outcome`, and a reference/bitset identifying every required evidence plane. The scanner must reject terminal records whose required evidence is missing, UNKNOWN, IN_PROGRESS, or incompatible with the declared scope. The exact field encoding is intentionally unspecified pending approval and format versioning.

## 4. Operation kind policy matrix (provisional, not normative)
| Kind | Current evidence | Required-effect policy now | What cannot be claimed |
|---|---|---|---|
| GENERIC | Identity label only; no production caller mapping | UNASSIGNED / reject policy selection | No implied ISSUE, HANDOFF, or recipient requirement |
| READ | Adapter read path exists, but no durable operation-kind assignment | UNASSIGNED / reject policy selection | No inference that local read completion equals recipient commit |
| WRITE | Adapter write path exists, but no durable operation-kind assignment | UNASSIGNED / reject policy selection | No inference that OS acceptance or Worker transfer equals remote write commit |
| MESSAGE | No recipient message protocol/callsite mapping found | UNASSIGNED / reject policy selection | No implied message delivery, deduplication, or recipient receipt |

The labels READ/WRITE/MESSAGE are not enough to define mandatory evidence. A future approved policy may associate a kind with required planes, but only after real callsites and product semantics define whether a logical operation is one-way, request/response, read, write, or message delivery. Until then, do not guess a required-effect bitset.

## 5. Important outcome combinations requiring explicit product decisions
- ISSUE `OS_ACCEPTED`, HANDOFF `TRANSFERRED`: local ownership has moved to Worker; recipient result remains UNKNOWN/unavailable. A local-lifecycle terminal may be possible only after Worker reports a documented terminal state; recipient-commit terminal is forbidden.
- ISSUE `OS_ACCEPTED`, HANDOFF `NOT_TRANSFERRED`: do not classify as not submitted. The OS already accepted the operation; ownership remains with caller/Adapter and existing code retains/quarantines it. No retry/replay policy is approved.
- ISSUE `OS_ACCEPTED`, HANDOFF `UNKNOWN`: quarantine; no teardown/replay based on an assumed owner.
- ISSUE `PROVEN_NOT_SUBMITTED`: this proves only that the native submission was not accepted. It does not by itself prove the logical operation is complete, safe to retry, or recipient-not-committed unless the policy separately proves no other effect occurred.
- Any required plane `UNKNOWN`: no terminal outcome for a policy requiring that plane; quarantine and require evidence-based reconciliation.
- RECIPIENT `COMMITTED` or `PROVEN_NOT_COMMITTED`: impossible under current capabilities; reject unless a future versioned recipient verifier validates the receipt/evidence.

## 6. Candidate versioning model
Use a separate immutable policy registry rather than deriving rules from enum ordinals:
- `policy_id`: stable identifier for a semantic contract.
- `policy_version`: monotonically versioned policy definition; never mutate an existing version's meaning.
- `operation_kind`: classification label, not a policy by itself.
- `required_evidence_mask`: explicit set of evidence planes required for the selected terminal scope.
- `allowed_outcomes`: per-plane allowed terminal dispositions.
- `terminal_scope`: one of the three scopes above.
- `recipient_verifier_id/version`: mandatory only for recipient scope.

The journal header or operation-acceptance record must bind the selected policy ID/version so recovery never applies today's policy to an older operation. Policy unknown to the scanner means fail closed (unsupported policy), not a best-effort interpretation. Any on-disk addition requires a new format version or an explicitly approved extension mechanism; do not alter the current 192-byte envelope in place.

## 7. Scanner implications
The current scanner's terminal rule accepts a request when at least one of ISSUE or HANDOFF has a determinate result and neither plane is IN_PROGRESS/UNKNOWN. It does not encode required planes, policy identity, terminal scope, or recipient evidence. This is structurally testable but not sufficient for semantic terminal validation.

Until a policy schema is approved:
- Keep the scanner structural/test-only; do not claim business-terminal correctness.
- Do not add terminal policy behavior by guessing from operation kind.
- Do not add a WAL writer, replay, startup recovery, compaction, or automatic retry.
- Preserve UNKNOWN quarantine and the existing read-only behavior.

## 8. Decisions required before implementation
1. Define the product meaning of GENERIC, READ, WRITE, and MESSAGE and map each real producer/caller to exactly one kind.
2. Decide whether local lifecycle completion is useful as a terminal state independent of recipient outcome.
3. Define the actual recipient protocol, durable deduplication key, receipt authenticity, status query, and affirmative proof of non-commit.
4. Decide whether OS_ACCEPTED + HANDOFF_NOT_TRANSFERRED is a permanent local failure, a manually recoverable state, or another quarantined state; do not infer replay safety.
5. Define whether handoff can exist without ISSUE and whether any operation kind has ISSUE-only or HANDOFF-only valid terminal semantics.
6. Approve policy registry ownership, policy version retention, unknown-policy handling, and format migration.
7. Define what constitutes Worker terminal completion and whether that evidence survives process restart.

## 9. Recommendation and gate
Recommendation: retain current typed ISSUE/HANDOFF result separation; add no per-kind required-effect rules until the seven decisions above are resolved. The current production-like adapter can support a future local-lifecycle evidence model, but not recipient-commit semantics. The scanner should remain a structural parser/probe, not an authorization to mark logical operations complete.

No source/header/test/project configuration/README/production file was modified. No build or test was run because this slice is a design-only review. No WAL writer, replay, startup recovery, or persistent integration was added.

Verified unchanged source SHA-256:
- `raya-durable-wal-v1.c`: `E1BD46915B859751DAB20B1E62352A39C72C105B75979DB0DE9CCC3B007159E5`
- `raya-durable-wal-v1.h`: `CFA50B740F67013171B3199F769F331A5AFECBD95A86A66D77185AABEA2B6DF9`
- `raya-durable-contract-v1.h`: `2FA73C0B77968D48909982379EC851DD5ECE1B0FD7DEEC7FD960AA04B7984B31`
- `integrated-submit-adapter-v2.c`: `A9B39D0D4094E8418F820E53B3E28FEE64FC7B0A9EC417450F25EC422EE8D87F`
- `dedicated-io-submitter-v2.c`: `037A18DA615B82A462580ABE57158F5B6D3E39AD5B8D25C974831AF74D8AACFD`

Phase 1 remains NOT ACCEPTED. Production remains NO-GO.