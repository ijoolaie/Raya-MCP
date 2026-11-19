# API BEHAVIOR VS TERMINAL REQUIREMENTS — SOURCE-BACKED CONTRACT REVIEW
Date: 2026-11-17
Scope: Working Copy public headers and implementation paths
Status: REVIEW REPORT — NO SOURCE CHANGES — PRODUCT SEMANTICS STILL OPEN

## 1. Executive summary

The current concrete I/O API family is asynchronous managed byte-pipe READ/WRITE. It does not implement a durable MESSAGE protocol or recipient acknowledgement contract. The API's caller-visible outcomes describe admission/queueing/retention and later local completion callback behavior; they do not prove remote application processing or durable recipient commit.

The most important constraints:
- Adapter submit returns QUEUED, RETAINED, or REJECTED. QUEUED means admitted to the asynchronous pipeline, not completed I/O.
- RETAINED means resources are deliberately retained and callback suppressed; it is not equivalent to a known failed I/O.
- The completion callback reports a Win32 error, transferred byte count, and (for reads) a buffer valid only during callback. It is a local completion observation, not remote application receipt.
- Worker handoff transfers local ownership only. TRANSFERRED_DEGRADED is not a healthy handoff.
- The issuer thread must remain alive until overlapped I/O reaches terminal completion; Worker handoff does not remove this constraint.
- No durable operation ID is currently threaded through the live Adapter/Submitter/Worker API path.
- No recipient verifier, remote receipt, or status-query protocol is implemented in this API family.

## 2. Public API inventory

### 2.1 Managed pipe submission

Header: managed-pipe-io-v2.h
API:
- raya_managed_pipe_submit
- raya_managed_pipe_submit_with_cancel

Operation families:
- RAYA_MANAGED_PIPE_READ
- RAYA_MANAGED_PIPE_WRITE

Inputs include Worker, pipe HANDLE, direction, write data/length, caller wait timeout, cancellation drain timeout, completion callback/context; the extended variant also accepts cancel callback/context.

Documented behavior:
- Duplicates the pipe handle.
- Allocates private OVERLAPPED/event/buffer state.
- Issues one overlapped byte-pipe operation.
- Callback may run inline before return or later on Worker thread.
- Read callback data is valid only during callback; caller must copy to retain it.
- REJECTED means no I/O was accepted and no callback will run.
- RETAINED means resources are deliberately retained and callback suppressed.
- Issuer thread must remain alive until terminal completion. Handoff does not make kernel request independent of issuer thread.

Terminal implication:
- Callback with a determinate Win32 completion error/byte count is the closest available local transport completion observation.
- It is not proof that a peer application parsed, processed, persisted, or committed the bytes.
- No protocol-level message framing or application ACK is defined here.

### 2.2 Integrated Adapter submit

Header: integrated-submit-adapter-v2.h
API: raya_integrated_adapter_submit

Inputs:
- Adapter, pipe, READ/WRITE direction, optional write buffer, length, completion callback/context.

Return values:
- RAYA_ADAPTER_REJECTED
- RAYA_ADAPTER_QUEUED
- RAYA_ADAPTER_RETAINED

Implementation observations in integrated-submit-adapter-v2.c:
- Validates direction and write buffer.
- Allocates envelope and operation buffer; copies WRITE bytes.
- Duplicates pipe and creates completion event.
- Admits operation to Worker, Submitter, and Worker reservation stages.
- Enqueues Submitter request; returns QUEUED when enqueue succeeds.
- On pre-issue failures, attempts explicit caller-submission-failed ownership transition, finalizer handle closure, release commit, owner release, Worker unreserve/finish as appropriate.
- Any uncertain ownership/finalizer/registry transition retains the envelope and returns RETAINED.
- Successful asynchronous admission returns QUEUED before I/O completion.

Terminal implication:
- QUEUED is an admission milestone, not a terminal result.
- REJECTED is only safely understood according to the specific rejection path; it is not a universal proof of remote non-observation unless the implementation path proves no native I/O was issued.
- RETAINED is indeterminate/nonterminal for resource and callback lifecycle; quarantine is required.
- The completion callback is local evidence only.

### 2.3 Submitter

Header: dedicated-io-submitter-v2.h
API family:
- admit / enqueue / abort_admission
- acknowledge / snapshot
- issue, publish, not_issued callbacks supplied by caller
- shutdown / join / destroy lifecycle functions

Request lifecycle vocabulary:
BUILDING → ADMITTED → QUEUED → ISSUING → ISSUED → ACKED
Alternative states: NOT_ISSUED, UNCERTAIN.

Issue callback dispositions:
- ISSUE_NOT_ACCEPTED
- ISSUE_ACCEPTED
- ISSUE_UNCERTAIN

Publish callback returns int; the integrated Adapter's Publish path performs Worker handoff/ownership transition. This is not a remote recipient receipt.

The Submitter tracks request_id and in-memory Issue/Publish effect states after admission. request_id is monotonic only within one Submitter instance and is not restart-stable.

Terminal implication:
- Submitter ACKED means local Adapter release callback acknowledged the Submitter request.
- It does not mean recipient application commit.
- Submitter request identity is not a durable cross-process operation identity.
- Lifecycle Shutdown/Join/Destroy statuses describe component teardown, not I/O business completion.

### 2.4 Managed Worker

Header: reaper-managed-worker-v2.h
API family:
- admit / finish
- reserve / handoff
- shutdown_ex / join_ex / destroy_ex

Handoff dispositions:
- NOT_TRANSFERRED
- TRANSFERRED
- TRANSFERRED_DEGRADED

Implementation:
- Worker handoff updates integrated operation ownership and pipe owner state under Worker lock.
- It signals Worker wake event.
- If ownership transition or wake signaling fails after integrated handoff, result is TRANSFERRED_DEGRADED and Worker records a recovery cause.
- Worker waits for exact completion event/OVERLAPPED state and uses GetOverlappedResult; incomplete observations are retried. Uncertain completion/degraded states do not produce normal terminal release.
- Release callback is invoked outside Worker lock only after the relevant ownership/finalizer release path is complete.

Terminal implication:
- TRANSFERRED means local ownership transferred to Worker.
- TRANSFERRED_DEGRADED means ownership may have transferred but system is degraded; it is not successful completion.
- Worker thread exit/join is not a recipient commit signal.
- Worker lifecycle result APIs are teardown controls, not per-operation semantic receipts.

### 2.5 Finalizer and owner

Header: pipe-io-finalizer-v2.h
States:
NOT_STARTED, CLOSING_PIPE, PIPE_CLOSED, CLOSING_EVENT, HANDLES_CLOSED, CLOSE_UNCERTAIN, RELEASED.

Contract:
- Initialize only after terminal I/O collection.
- Both owned handles must be distinct.
- A failed close is sticky/uncertain.
- Memory may be freed only after both handles are positively closed and release commit succeeds.

Pipe owner transitions and integrated registry release are additional local ownership gates. They do not represent remote processing.

### 2.6 Orchestrator lifecycle

Header: raya-orchestrator-v1.h
Cleanup cursor currently sequences:
1. Adapter Destroy
2. Submitter admission close
3. Submitter Shutdown
4. Submitter Join
5. Submitter Destroy

This cursor concerns orderly local component teardown after operation admission has stopped. It is not an operation-level terminal policy and does not certify recipient state.

## 3. Behavior-to-terminal matrix

| API / operation | Current observable success | Current observable non-success | What it proves | What it does NOT prove |
|---|---|---|---|---|
| Adapter READ | QUEUED, later completion callback | REJECTED / RETAINED | Local admission; later local I/O completion if callback runs | Peer application consumed data; durable application state |
| Adapter WRITE | QUEUED, later completion callback | REJECTED / RETAINED | Local admission; later local I/O completion if callback runs | Peer parsed or persisted bytes; recipient commit |
| Submitter request | ACKED after Adapter release callback | NOT_ISSUED / UNCERTAIN / retained | Local request bookkeeping and release progression | Durable operation identity; remote receipt |
| Worker handoff | TRANSFERRED | NOT_TRANSFERRED / TRANSFERRED_DEGRADED | Local ownership transfer state | I/O completion or recipient processing |
| Worker collection | Exact terminal collection and release callback | INCOMPLETE / uncertain / DEGRADED | Local overlapped completion observation and ownership path | Application-level protocol acknowledgement |
| Finalizer | Both handles closed + release committed | close uncertain / not terminal | Local handle/resource release | Remote effect or persistence |
| Orchestrator teardown | Components stopped/joined/destroyed | busy/degraded/blocked | Local component lifecycle terminal | All business operations committed remotely |

## 4. Candidate operation family mapping — intentionally conservative

### READ
Current implementation supports byte-pipe READ. The product meaning of successful READ is unresolved:
- local OS completion with bytes,
- local callback delivery,
- complete protocol frame received,
- application-level validation/processing,
- or some higher-level durable action.

No evidence plane can be selected until the caller-visible contract is decided. A local callback with bytes is not automatically application success.

### WRITE
Current implementation supports byte-pipe WRITE. Product meaning is unresolved:
- OS accepted,
- local transport completion,
- peer received bytes,
- peer parsed/validated a frame,
- peer application committed data.

Current API can at most report local transport completion. It cannot claim peer durable commit.

### MESSAGE
No distinct message API, message identity, framing/ACK contract, or recipient receipt verifier was found in the inspected headers and paths. Do not map MESSAGE to WRITE by name alone.

### GENERIC
No production assignment was found. It must remain policy-unassigned.

## 5. Additional architectural constraint: issuer-thread lifetime

The managed-pipe API documentation explicitly states that the thread which issued ReadFile/WriteFile must remain alive until the overlapped operation reaches terminal completion. Worker handoff does not detach the kernel operation from the issuer thread.

This is a correctness/liveness contract for any future durable policy:
- The Submitter issue callback executes on Submitter's dedicated thread.
- The request/issuer lease remains outstanding until Adapter release callback acknowledges it.
- Any future refactor that returns from the issuer thread before terminal I/O completion would violate the documented contract unless the underlying I/O architecture is changed and re-characterized.

This is separate from recipient semantics but affects whether local terminal can be reached safely.

## 6. Gaps before assigning terminal policy

1. No stable durable operation ID is threaded from public Adapter submit through Submitter request, Worker operation, finalizer, and completion callback.
2. No persistent namespace/sequence allocator.
3. No explicit API-level distinction between local transport completion and recipient application commit.
4. No recipient protocol/message ACK or verifier.
5. No caller contract specifying whether partial byte counts, EOF, broken pipe, cancellation, or application-level framing errors constitute success.
6. No documented retry/idempotency policy for a caller retry after uncertain WRITE.
7. No restart-surviving evidence/replay for outstanding operations.
8. No durable policy ID/version bound to an accepted operation.

## 7. Safe interim classification

- Adapter QUEUED: ACCEPTED_FOR_LOCAL_PROCESSING, nonterminal.
- Adapter RETAINED: QUARANTINED, nonterminal; callback may be suppressed.
- Adapter REJECTED: classify per exact code path; do not globally treat as proof of no side effect without path evidence.
- Worker TRANSFERRED: LOCAL_OWNERSHIP_TRANSFERRED, nonterminal.
- Worker TRANSFERRED_DEGRADED: QUARANTINED.
- Worker completion + finalizer/owner/registry release + Adapter callback/Submitter ACK: candidate LOCAL_LIFECYCLE_TERMINAL_VOLATILE, subject to exact same-operation evidence.
- No state in this API family proves RECIPIENT_COMMIT.

## 8. Work performed / boundaries

- Read public headers and selected implementation ranges in Working Copy.
- No source/header/test/config/README edits.
- No builds or tests.
- No WAL format, writer, replay, startup recovery, or recipient verifier work.
- No Production modification, install/download, process termination, or temporary artifact creation.

## 9. Required product decisions

For READ and WRITE independently:
- Is success local transport completion or a higher-level application event?
- What is the contract for EOF, partial transfer, cancellation, broken pipe, and callback suppression?
- Does retry after uncertain WRITE require recipient idempotency?
- Must success survive broker/host restart?

For MESSAGE:
- Is a message protocol actually in scope?
- What framing, identity, recipient ACK, and durable receipt protocol is authoritative?

Until these are answered, operation kind policies remain UNASSIGNED and recipient commit remains unsupported.

---
End of source-backed API review.
