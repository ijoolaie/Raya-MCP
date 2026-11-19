# CALL-SITE AND RESULT-PROPAGATION AUDIT — MANAGED PIPE / LEGACY PIPE / ADAPTER
Date: 2026-11-17
Scope: Working Copy recursive source-reference audit
Status: SOURCE-BACKED AUDIT — NO CODE CHANGES

## 1. Executive result

A recursive search across C/C++ headers and implementation files found no non-test production call site for:
- raya_managed_pipe_submit
- raya_managed_pipe_submit_with_cancel
- raya_pipe_write_all
- raya_pipe_read_exact
- raya_integrated_adapter_submit

All concrete call sites found for the managed-pipe and integrated Adapter submit APIs are in their implementation files, public declarations, or test programs. The legacy pipe-io wrapper functions have declarations and definitions, but no caller was found.

This means the inspected repository contains API implementations and characterization/regression tests, but does not currently demonstrate an application/business caller that consumes these APIs and maps their outcomes into an external result. The previous terminal-policy discussion is therefore an API-contract analysis, not a validated end-to-end application behavior.

Search scope: *.c, *.h, *.cpp, *.cc, *.cxx, *.hpp, *.inl recursively under the Working Copy root.

## 2. Call-site inventory

| Public function | Non-test caller found? | Concrete call-site evidence |
|---|---|---|
| raya_managed_pipe_submit | No | managed-pipe-io-v2-native-test.c only |
| raya_managed_pipe_submit_with_cancel | No | managed-pipe-io-v2-native-test.c, managed-pipe-io-v2-thread-exit-test.c, managed-pipe-io-v2-concurrent-shutdown-test.c |
| raya_pipe_write_all | No | declaration and implementation only |
| raya_pipe_read_exact | No | declaration and implementation only |
| raya_integrated_adapter_submit | No | integrated-submit-adapter-v2-api-test.c, integrated-submit-adapter-v2-native-test.c, integrated-submit-adapter-v2-multi-native-test.c |

The Adapter submit API has broad test coverage but no application caller in the scanned source tree.

## 3. What the tests actually establish

### 3.1 Managed-pipe native tests

File: managed-pipe-io-v2-native-test.c

Observed test cases:
- Timeout with custom cancel callback returning ERROR_NOT_FOUND: API returns HANDED_OFF; later callback reports ERROR_SUCCESS and one byte.
- Local READ completion: API returns CALLBACK_INLINE; callback reports ERROR_SUCCESS and one byte.
- WRITE: API returns CALLBACK_INLINE; callback reports ERROR_SUCCESS and one byte; test peer reads the byte.
- Default cancellation drain: API returns CALLBACK_INLINE; callback reports ERROR_OPERATION_ABORTED and zero bytes.
- Handle-close failure: API returns RETAINED; callback count remains zero; Worker becomes DEGRADED; reserved slot and retained object remain; Join/Destroy fail.
- Pre-issue submission failure after pipe disconnect: API returns CALLBACK_INLINE with nonzero callback error; Worker accounting returns to zero.

Important boundary:
- The test confirms callback delivery and local byte-pipe observations for controlled loopback scenarios.
- It does not establish recipient application processing or durable commit.
- RETAINED is explicitly shown to suppress callback and block normal Worker teardown.

### 3.2 Issuer-thread exit characterization

File: managed-pipe-io-v2-thread-exit-test.c

Test starts a producer thread, submits a pending READ with zero wait/drain budget, observes HANDED_OFF, then exits producer thread. The callback later reports ERROR_OPERATION_ABORTED and zero bytes.

This confirms the header's warning: HANDED_OFF does not detach the pending overlapped operation from issuer-thread lifetime. A producer must not return/exit immediately after HANDED_OFF unless the operation has otherwise reached terminal completion. The current test demonstrates the adverse outcome; it is not a production scheduling strategy.

### 3.3 Adapter tests

The Adapter API tests exercise:
- READ/WRITE admission and result propagation.
- Pre-issue rejection and retained ownership.
- Callback completion and ownership release.
- Concurrent submit/destroy/admission scenarios.
- Native pipe behavior and multi-operation workloads.

They are tests of the Adapter's internal contract. No production caller was found to demonstrate how an application reports QUEUED/RETAINED/REJECTED or consumes the later completion callback.

### 3.4 Legacy pipe-io wrapper

raya_pipe_write_all and raya_pipe_read_exact are not called anywhere else in the scanned source tree. The public header's warnings about partial transfer, closing the stream, and idempotency are therefore contractual requirements, not behavior shown in an in-repository application integration.

## 4. Result propagation and observability gaps

### Managed-pipe API

Return result:
- REJECTED: no I/O accepted, no callback.
- CALLBACK_INLINE: callback invoked before return.
- HANDED_OFF: operation still belongs to Worker; callback later.
- RETAINED: safety retention, callback suppressed.

Callback:
- void callback(DWORD error, DWORD transferred, const void *data, void *context)
- READ data valid only during callback.
- No operation ID, caller correlation token, terminal scope, retry disposition, cumulative bytes, or durable audit record.

Consequences:
1. A caller can correlate callback only through its own context, but no library-enforced operation identity exists.
2. HANDED_OFF is nonterminal; callback may be asynchronous.
3. RETAINED has no callback and no direct operation handle/token returned to caller. The Worker retains the operation internally.
4. A caller that records only callback outcomes cannot account for RETAINED operations.
5. A caller that treats CALLBACK_INLINE as a business success can overstate the result; callback error and transferred count still need interpretation.
6. Since callback may run inline before the submit function returns, caller state must be fully initialized before calling submit and must tolerate reentrant completion.

### Adapter API

The Adapter's return is an admission/ownership status. The later callback carries local completion error/bytes. No production caller demonstrates a higher-level mapping.

### Legacy pipe-io API

The wrapper returns only one status enum. It does not expose cumulative partial byte count or an operation ID. A failed multi-chunk WRITE can have made partial progress before returning TIMEOUT/DISCONNECTED/ERROR.

## 5. End-to-end propagation matrix

| Layer | Observable signal | Is it terminal? | Is it visible to a real application caller in repository? |
|---|---|---|---|
| managed-pipe submit | REJECTED / CALLBACK_INLINE / HANDED_OFF / RETAINED | Only CALLBACK_INLINE is callback-delivered before return; HANDOFF/RETAINED are not terminal outcomes | No production caller found |
| managed-pipe callback | Win32 error + transferred bytes + temporary READ data | Local operation callback; business meaning unspecified | Only tests |
| Worker | transfer/collect/degraded/retained internal state | Local ownership and I/O lifecycle only | Internal implementation/tests |
| Adapter submit | QUEUED / RETAINED / REJECTED | Admission result only | No production caller found |
| Adapter release callback | local error/bytes and release/ACK path | Local resource/request lifecycle | Only tests in scanned tree |
| legacy pipe-io wrapper | OK / TIMEOUT / DISCONNECTED / ERROR / INVALID | Function return; may conceal partial cumulative progress | No caller found |
| recipient application | no receipt/status API in inspected path | Not represented | No implementation found |

## 6. Critical design implications

1. Before implementing durable operation IDs, define where a caller obtains and retains the identity. Current public APIs do not accept or return one.
2. Before implementing durable terminal policy, define the caller-visible terminal event. CALLBACK_INLINE, HANDED_OFF, QUEUED, and Submitter ACK are not interchangeable.
3. RETAINED requires an observable quarantine handle, query API, durable status record, or an explicitly documented operator-only recovery contract. Otherwise a caller cannot reconcile it.
4. Callback reentrancy must be documented: callback may execute inline before submit returns.
5. The callback's void signature cannot report whether the caller accepted/persisted the result. If callback failure matters, a separate acknowledgement/receipt protocol is needed.
6. A future caller must keep callback context alive through asynchronous completion; RETAINED complicates safe reclamation because callback may never occur.
7. For legacy pipe-io, expose partial byte counts or keep the existing conservative "discard stream; do not retry" contract. Do not imply zero progress from TIMEOUT/DISCONNECTED.
8. No operation-kind production mapping should be inferred from READ/WRITE enum values alone; product semantics are absent.

## 7. Recommended next slice

No code change is recommended until the product/API owner decides:
- Which component is the actual production caller and its lifetime owner.
- Whether managed-pipe or Adapter is the supported public path; avoid maintaining two unrelated submit surfaces without a declared boundary.
- How caller correlation and operation identity enter/exit the API.
- How RETAINED is surfaced and reconciled.
- Whether completion callback is only a notification or an authoritative caller-accepted result.
- Whether local transport completion is sufficient for READ/WRITE success.

If implementation work is authorized later, first add an explicit caller-facing contract and focused tests for inline callback reentrancy, callback-context lifetime, RETAINED query/reconciliation, and partial progress. Do not add a recipient-commit claim without an actual recipient protocol/verifier.

## 8. Work boundary

- Recursive source search performed across C/C++ source/header extensions.
- Read relevant public header and native test paths.
- No source/header/test/config/README modifications.
- No build/test execution.
- No process termination, installation, download, or temporary artifact creation.
- Production unchanged.

---
End of call-site and result-propagation audit.
