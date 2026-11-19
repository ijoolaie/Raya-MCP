# READ/WRITE FAILURE, EOF, PARTIAL TRANSFER AND CANCELLATION REVIEW
Date: 2026-11-17
Scope: Working Copy source review only
Status: EVIDENCE REVIEW — NO SOURCE CHANGES — NO BUILD/TEST

## 1. Executive findings

Two distinct pipe-I/O implementations exist and must not be assigned one shared failure policy without API-level unification:

A. managed-pipe-io-v2 (Worker-owned asynchronous operation):
- Issues one ReadFile or WriteFile with private OVERLAPPED/event/buffer.
- On ERROR_IO_PENDING, waits for completion up to caller timeout.
- If not terminal, requests cancellation and waits a bounded drain interval.
- If exact completion evidence is collected, it can complete inline; otherwise ownership is handed to Worker.
- Worker later collects exact overlapped result and performs local release.
- Unknown completion/ownership/finalizer failures are quarantined/retained.

B. pipe-io-v2 (synchronous all/exact wrapper):
- Splits transfer into chunks up to 64 KiB and loops until requested length.
- Uses a duplicated pipe handle and private buffer for each pending chunk.
- On timeout/wait failure, requests CancelIoEx and drains for 2 seconds.
- If drain still does not complete, creates a separate reaper thread to wait for terminal completion; if thread creation fails, intentionally retains kernel-visible state rather than freeing it.
- Public contract explicitly says partial transfer may have occurred, the byte stream must be treated as unusable, and WRITE timeout may already have delivered bytes to peer.

These implementations have materially different caller-visible outcomes, cleanup paths, and thread-lifetime requirements.

## 2. Completion classification evidence

### 2.1 Exact overlapped completion classifier

Files:
- pipe-completion-context-v2.c/.h
- pipe-completion-context-v2-test.c
- getoverlappedresult-live-probe-test.c

The classifier takes three evidence inputs:
- completion event signaled,
- exact issuing handle/OVERLAPPED pair,
- GetOverlappedResult success/error.

Important rules:
- Successful GetOverlappedResult with exact signaled event/pair is terminal success.
- ERROR_OPERATION_ABORTED is terminal-aborted only when the exact event and issuing pair prove the specific operation reached a terminal state.
- ERROR_OPERATION_ABORTED without exact event evidence is uncertain.
- A mismatched event/issuing pair is uncertain.
- ERROR_IO_INCOMPLETE remains incomplete.
- Other errors (including broken pipe, invalid handle, access denied, invalid parameter) are not automatically treated as proof of cleanup.

This is intentionally conservative: an error code by itself is not sufficient evidence that kernel ownership ended.

### 2.2 Older generic completion classifier

Files:
- completion-classify-v2.c/.h
- completion-classify-v2-test.c

This helper only recognizes:
- API success -> TERMINAL_SUCCESS
- ERROR_IO_INCOMPLETE -> INCOMPLETE
- every other failure -> UNCERTAIN

Its test explicitly checks ERROR_OPERATION_ABORTED, ERROR_BROKEN_PIPE, ERROR_PIPE_NOT_CONNECTED, ERROR_NO_DATA, ERROR_HANDLE_EOF, ERROR_INVALID_HANDLE, ERROR_ACCESS_DENIED, and ERROR_INVALID_PARAMETER as uncertain absent stronger context.

Do not confuse this generic helper with the exact event/issuing-pair classifier above.

## 3. managed-pipe-io-v2 behavior

Source: managed-pipe-io-v2.c, lines 144-270.

### 3.1 Pre-issue rejection

The API returns REJECTED before native I/O when:
- Worker/pipe/callback/cancel callback invalid,
- timeout or drain timeout is INFINITE,
- length is zero or above RAYA_MANAGED_PIPE_MAX_IO,
- direction is not READ/WRITE,
- WRITE buffer is null,
- operation/callback/buffer allocation fails,
- Worker admission fails,
- event/duplicate handle setup fails and cleanup succeeds,
- Worker reservation fails and unissued cleanup succeeds.

A failure to close/release or maintain ownership during pre-issue cleanup can instead produce RETAINED. That is a resource-lifecycle quarantine, not a native-I/O completion result.

### 3.2 Native issue result

- ReadFile/WriteFile TRUE: immediate completion path; byte count captured; local owner transitions to caller-complete; finalization/release follows.
- FALSE + ERROR_IO_PENDING: operation is pending; immediate byte-count output is not used as completion evidence.
- FALSE + any other error: implementation records the error and treats submission as not accepted, then runs caller-terminal cleanup. If ownership cleanup fails, it returns RETAINED.

This implementation does not infer remote application state from immediate API success.

### 3.3 Wait, timeout and cancellation

- Event signaled: GetOverlappedResult is called with wait=FALSE.
- Exact terminal success or terminal-aborted classification: transferred count and completion error are captured; caller collection and finalization proceed.
- Other signaled errors: caller collection is attempted but the operation is not released as normal terminal; cancellation/drain path continues.
- WAIT_FAILED: wait error recorded; cancellation/drain path continues.
- Cancel callback return value is ignored for final classification. A successful CancelIoEx request is not itself proof of cancellation completion.
- After cancellation, the code waits for bounded drain interval.
- If event becomes signaled and exact result is terminal, operation can complete locally.
- If still not terminal, handoff to Worker is attempted.
- Worker handoff TRANSFERRED returns HANDED_OFF.
- TRANSFERRED_DEGRADED returns RETAINED.
- Any failed ownership handoff also returns RETAINED.

Implication:
- Timeout is a wait outcome, not necessarily an I/O outcome.
- Cancel request is not terminal proof.
- A later successful completion can win the race with cancellation.
- A later aborted completion is terminal only with exact event/pair evidence.
- RETAINED suppresses callback and preserves resources; caller cannot interpret it as a clean failed operation.

## 4. pipe-io-v2 behavior

Files:
- pipe-io-v2.c/.h

Public functions:
- raya_pipe_write_all
- raya_pipe_read_exact

### 4.1 Transfer semantics

- Total deadline includes setup/copy time.
- Transfer is chunked to at most 65,536 bytes per native operation.
- The wrapper loops until the exact requested length.
- Calls on the same byte stream must be externally serialized; concurrent reads, concurrent writes, or overlapping frame transfers are prohibited by contract.
- Zero-length request returns OK without native I/O.
- A zero-byte completion before requested length is classified DISCONNECTED.
- A timeout/error may occur after partial transfer.
- On timeout/error, caller must treat byte stream as unusable and close its pipe handle.
- A timed-out WRITE may already have delivered bytes to the peer. Retrying on this or a replacement connection requires higher-level idempotency.

### 4.2 Disconnect classification

The implementation maps these terminal GetOverlappedResult errors to DISCONNECTED:
- ERROR_BROKEN_PIPE
- ERROR_PIPE_NOT_CONNECTED
- ERROR_NO_DATA
- ERROR_HANDLE_EOF

On a cancellation drain path:
- ERROR_OPERATION_ABORTED with no wait failure maps to TIMEOUT.
- Other errors map to ERROR.
- If cancellation drain does not signal within 2 seconds, a separate reaper thread is created to wait indefinitely for GetOverlappedResult and free resources afterward.
- If reaper thread creation fails, resources remain intentionally retained; no unsafe free is attempted.

### 4.3 Partial progress caveat

The implementation assigns transferred_out for a completed individual chunk, but transfer() returns immediately when a later chunk fails. The public API does not return cumulative bytes transferred across earlier chunks. Thus callers cannot reconstruct exact partial frame progress from the public status alone.

This reinforces the documented rule: after partial/uncertain transfer, close the stream and do not retry a framed WRITE without protocol-level idempotency.

## 5. READ/WRITE failure decision matrix

| Condition | Local implementation evidence | Safe interpretation | Retry guidance |
|---|---|---|---|
| Invalid arguments / allocation before issue | No ReadFile/WriteFile called | Proven not submitted for that invocation | Correct input/resource then retry as new attempt |
| Worker admission/reservation failure before issue, cleanup complete | No native I/O called | Proven not submitted for that invocation | Retry only after caller confirms no other duplicate |
| Native issue FALSE + non-IO_PENDING error | API rejected native request; cleanup path follows | Local issue not accepted; cleanup still must be verified | New attempt may be considered; preserve request identity if operation-level retry semantics exist |
| Native issue TRUE | Immediate completion and byte count | Local operation completed; not recipient commit | Do not infer peer application success |
| FALSE + IO_PENDING | Kernel accepted pending request | Nonterminal | Wait/collect; never resubmit same logical WRITE merely because pending |
| Wait timeout | Only wait deadline elapsed | Operation may still be pending | Cancel/drain or Worker handoff; no blind retry |
| CancelIoEx succeeds | Cancellation request accepted | Cancellation requested, not completed | Wait for exact terminal result |
| CancelIoEx fails with ERROR_NOT_FOUND | Operation may already have completed | Race; collect original result | Do not assume cancellation or failure |
| Exact event + GetOverlappedResult success | Specific overlapped request terminal | Local transport completion with byte count | Application-level result still unknown |
| Exact event + GetOverlappedResult ERROR_OPERATION_ABORTED | Specific operation terminal-aborted | Local I/O aborted | No remote effect guarantee for WRITE unless additional evidence exists |
| ERROR_BROKEN_PIPE / PIPE_NOT_CONNECTED / NO_DATA / HANDLE_EOF | Disconnection/end condition observed | Local stream disconnected; possible partial transfer | Close stream; READ may have partial bytes; WRITE may have delivered bytes |
| ERROR_IO_INCOMPLETE | Operation remains pending | Incomplete, not terminal | Continue wait/collection |
| Wait failure / unknown completion error / mismatched event or handle | Insufficient evidence | UNCERTAIN / quarantine | No retry until reconciliation |
| Worker TRANSFERRED | Ownership moved to Worker | Local lifecycle continues | No duplicate submission |
| Worker TRANSFERRED_DEGRADED / RETAINED | Ownership/recovery abnormal; callback may be suppressed | QUARANTINED | No retry; manual/evidence-based reconciliation |
| Callback error=SUCCESS, transferred=N | Local completion callback delivered N bytes | Local API completion | Must separately decide whether N==requested length is required; no recipient commit |
| Callback reports error with bytes>0 | Partial or error completion may include progress | Partial local effect possible | READ data policy; WRITE requires idempotency/quarantine policy |

## 6. Critical semantic gaps

1. Public completion callback returns void and carries only (error, transferred, data, context). It has no operation ID, terminal scope, retry class, or durable evidence object.
2. The API does not distinguish "full logical operation complete" from "one chunk completed" except in pipe-io-v2's internal loop; partial aggregate count is not returned on failure.
3. A READ may return fewer bytes than requested. Whether this means a valid short read, EOF, truncated frame, or protocol error is not defined by the byte-pipe API.
4. A WRITE may report successful local byte transfer while the peer application has not processed/persisted the bytes.
5. ERROR_OPERATION_ABORTED is terminal only with exact event/issuing-pair evidence; generic error classification must not promote it to safe release.
6. CancelIoEx return value is not a completion receipt.
7. Timeout and disconnection do not prove that zero bytes were observed remotely.
8. RETAINED deliberately suppresses callback, so caller completion cannot be used as a total audit trail.
9. The legacy pipe-io-v2 and managed-pipe-io-v2 have different issuer-thread and reaper behavior; their contracts must remain distinct until deliberately unified.
10. No end-to-end stable operation identity or recipient idempotency protocol exists.

## 7. Recommended interim policy (no code change)

READ:
- Distinguish exact-length read, short read, EOF/disconnect, timeout, cancellation, and uncertain ownership.
- Do not label short read as successful frame receipt without framing rules.
- On stream-level timeout/partial transfer, close/discard connection state before starting a new framed read.

WRITE:
- Treat native issue acceptance and local completion as separate events.
- Treat timeout, disconnect, cancellation race, and RETAINED as potentially having delivered some bytes.
- Never retry a logical WRITE after uncertain/partial delivery without stable operation identity and recipient-side idempotency/receipt protocol.
- A successful local byte count must not be called recipient commit.

Shared:
- Preserve exact event/OVERLAPPED/issuing-handle evidence for terminal-aborted classification.
- Treat failed cancellation request as nonterminal until collection or quarantine proves otherwise.
- Do not release kernel-visible buffers/OVERLAPPED/handles before exact terminal completion.
- No global mapping to durable operation kinds until caller/product semantics are decided.

## 8. Evidence and work boundary

Reviewed:
- managed-pipe-io-v2.c/.h
- pipe-io-v2.c/.h
- pipe-completion-context-v2.c/.h and tests
- completion-classify-v2.c/.h and tests
- pipe-io-submit-semantics-test.c
- getoverlappedresult-live-probe-test.c

No source/header/test/config/README changes.
No build or test run.
No process termination, installation, download, or temporary artifact creation.

## 9. Decisions required from product/API owner

READ:
- Is the API an exact-length/frame read or a byte-stream read?
- What is success for short read and EOF?
- Should caller receive cumulative partial byte count on failure?

WRITE:
- Is success local OS completion, peer byte receipt, or application commit?
- What idempotency key and recipient status-query/receipt protocol is authoritative?
- Is retry after a terminal disconnect allowed only with new connection and stable operation ID?

Both:
- What should caller see when operation is RETAINED and callback is suppressed?
- How is quarantine surfaced, persisted, and reconciled?
- Is issuer-thread lifetime constraint acceptable or must the API move to a dedicated long-lived issuer?

---
End of source-backed READ/WRITE failure review.
