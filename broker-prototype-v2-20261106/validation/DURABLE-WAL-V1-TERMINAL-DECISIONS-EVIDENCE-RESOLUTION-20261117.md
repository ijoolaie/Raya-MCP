# Durable WAL v1 - Terminal Decision Evidence Resolution
Date: 2026-11-17
Status: evidence review; partial decisions resolved; product/persistence policy remains unapproved.

## 1. Scope
Follow-up inspection of the operation-kind/terminal policy proposal, adapter, Worker, pipe ownership/finalizer, durable contract validators, and WAL scanner. Design-only. No implementation or format changes.

## 2. Evidence-backed findings
### Adapter ISSUE and HANDOFF order
`adapter_issue` invokes ReadFile or WriteFile. A synchronous TRUE or ERROR_IO_PENDING is reported as legacy ISSUE_ACCEPTED; other native errors become ISSUE_NOT_ACCEPTED. Submitter invokes Publish after Issue acceptance. Adapter Publish calls `raya_managed_worker_handoff`.

On `RAYA_MANAGED_HANDOFF_NOT_TRANSFERRED`, Adapter explicitly documents that OS already accepted I/O, does not route through NOT_ISSUED cleanup, records ownership uncertainty, and retains/quarantines the envelope. On successful handoff, the Worker integrated queue accepts the operation, pipe owner changes to REAPER_QUEUED, and the Worker wake is signaled. A transferred-but-degraded result still means ownership transfer occurred, but the Worker/Adapter path marks degraded and retains the operation; it is not normal completion.

### Worker terminal path
Worker waits for the operation event, validates the exact issuing handle/OVERLAPPED pair, and calls GetOverlappedResult. INCOMPLETE remains pending and is retried. UNCERTAIN is terminal-held/quarantined and causes Worker exit. Terminal success and terminal-aborted are both classified as terminal OS completion states; both proceed through finalizer handle close, release commit, owner release, integrated registry release, and only then the release callback.

The Adapter release callback acknowledges the Submitter request, invokes the caller completion callback with completion error/bytes/read buffer, and frees the envelope. This is the strongest current evidence for a LOCAL_LIFECYCLE_TERMINAL in the live process. It is not a recipient application receipt. An ACK here is Submitter bookkeeping after Worker release, not a remote acknowledgement.

Worker thread exit with DEGRADED, wait failure, uncertain completion, finalizer failure, or ownership invariant failure is not a successful per-operation terminal. A successful Worker thread exit only means its own integrated queue can exit; it is not recipient commit evidence.

### Handoff without ISSUE
In the Adapter path, handoff is sequenced after Issue acceptance. The Worker public API itself exposes reserve and handoff as separate calls, and handoff accepts an operation whose pipe-owner state is CALLER_PENDING or CALLER_UNCERTAIN. Therefore the lower-level component API does not universally enforce a prior OS ISSUE event. A future durable policy cannot globally assume ISSUE must always precede HANDOFF without constraining its supported producer/API profile.

### Persistence boundary
The WAL scanner source contains no file open/read/write/flush/truncate APIs. It scans caller-provided bytes. Current WAL work therefore does not provide an on-disk journal, durable namespace allocator, crash-recovery replay, or restart-surviving Worker completion evidence. Any claim of restart-durable local terminal evidence is unsupported.

### Recipient capability mismatch requiring a gate
`raya-durable-contract-v1.h/.c` declares recipient idempotency, receipt, and status-query capability bits and permits `RAYA_DURABLE_GUARANTEE_RECIPIENT_COMMIT` when all bits are set. However, the callback table currently exposes only ISSUE and HANDOFF callbacks; there is no recipient receipt verifier, status-query callback, or authenticated evidence type in this contract. The validator checks bit consistency, not that a recipient protocol exists or that a receipt is valid. Thus those bits are declarations, not an implemented/verified recipient-commit guarantee. Do not enable or advertise RECIPIENT_COMMIT on this implementation.

## 3. Resolution of the seven open decisions
| Decision | Evidence result | Safe disposition |
|---|---|---|
| 1. Meaning and caller mapping of GENERIC/READ/WRITE/MESSAGE | No non-durable production assignment/callsite found. Adapter does distinguish read/write internally, but does not assign durable operation kinds. MESSAGE has no recipient protocol. | UNRESOLVED. Keep all kind policies unassigned; do not infer from enum labels. |
| 2. Is local lifecycle terminal useful independently? | Yes. Worker completion + finalizer release + owner release + integrated release + Adapter Submitter ACK + caller callback/free forms a local lifecycle terminal path. | Conceptually ACCEPT local scope only for live in-process evidence. It is not durable across process restart until persistent WAL writer/recovery exists. |
| 3. Recipient protocol, dedup, authenticity, status, proof of non-commit | No such protocol found. Capability flags are not callbacks or verifiers. | UNRESOLVED / unsupported. Recipient commit guarantee must fail closed. |
| 4. OS_ACCEPTED + HANDOFF_NOT_TRANSFERRED | Adapter explicitly retains/quarantines because OS accepted the operation and ownership handoff failed. | RESOLVED operationally as QUARANTINE; not NOT_SUBMITTED, not replay-safe, not terminal success. Manual recovery policy remains undefined. |
| 5. Can handoff exist without ISSUE? | Adapter ordering says no for this path; lower-level Worker API does not enforce that globally. | PROFILE-SPECIFIC. Require ISSUE before HANDOFF only in an Adapter-specific policy; generic policy must allow producer-specific sequencing or reject unsupported profiles. |
| 6. Policy ownership/versioning/unknown policy/migration | Existing code has no policy registry or persisted policy identity; WAL envelope v1 is fixed 192 bytes. | UNRESOLVED approval. Proposed immutable policy ID/version and fail-closed unknown policy remain design recommendations; require new format version or approved extension. |
| 7. Worker terminal completion and restart survival | Per-operation local terminal is reached only after terminal OS result, finalizer close/release, owner release, registry release, and release callback. Degraded/uncertain paths quarantine. No persistence/restart evidence. | Local terminal definition RESOLVED for runtime; restart survival NOT IMPLEMENTED. |

## 4. Candidate local-lifecycle evidence contract (not yet an approved WAL encoding)
For the current Adapter profile, a candidate local terminal evidence chain is:
1. ISSUE accepted by OS (synchronous completion or ERROR_IO_PENDING) OR proven not submitted.
2. If accepted, HANDOFF transferred to Worker; transferred-degraded is retained/quarantined and cannot advance to terminal.
3. Worker observes terminal completion (success or aborted) using exact event/issuing-pair evidence and GetOverlappedResult.
4. Finalizer closes required handles and commits release.
5. Pipe owner transitions to RELEASED and integrated registry releases the operation.
6. Adapter release callback successfully acknowledges Submitter, invokes caller callback, and releases envelope.

This is a candidate *volatile runtime* chain, not a crash-durable contract. If any stage is uncertain, degraded, retained, or failed, remain nonterminal/quarantined. For OS ISSUE_PROVEN_NOT_SUBMITTED, the Adapter's not-issued cleanup path must itself complete and release all local resources before local-lifecycle terminal; ISSUE evidence alone is insufficient.

Do not use a generic boolean `COMMITTED` to represent all six steps. Preserve plane-specific evidence and a separate local lifecycle outcome.

## 5. Additional contract defect / implementation gate
The current capability validator can accept a configuration claiming recipient-commit guarantee based solely on capability bits. There is no recipient verifier callback in `raya_durable_callbacks_v1`. Before any runtime can claim recipient commit, a future version must require a typed verifier interface, bind evidence to operation ID + payload digest + recipient identity + protocol/version, define authentication and status freshness, and validate positive/negative receipts. Until then, safest policy is to reject `RAYA_DURABLE_GUARANTEE_RECIPIENT_COMMIT` at runtime integration and document that the current pure config validator only validates declaration consistency, not operational capability.

## 6. Next work boundary
Recommended next slice: design a v2 capability/recipient-verifier contract and a local-terminal evidence vocabulary, with no code changes until the user approves semantics and persistence decisions. Keep operation kinds unassigned. Do not modify the 192-byte WAL v1 envelope, scanner, or add writer/replay/recovery in this slice.

No source/header/test/project config/README/production file changed. No build/test run. No install/download/process termination. No temporary artifacts created.

Phase 1 remains NOT ACCEPTED. Production remains NO-GO.