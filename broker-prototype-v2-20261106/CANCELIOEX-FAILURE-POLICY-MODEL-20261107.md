# CancelIoEx Failure Policy Model — 2026-11-07

Status: deterministic policy-model test only. This is not a test of the Windows CancelIoEx API, not a test of a real pending I/O request, and not Pipe I/O/Reaper runtime acceptance.

## Purpose

Exercise the ownership rule for all three cancellation outcomes without claiming that a particular native API failure can be induced reliably against a live pending operation:

- CancelIoEx reports success (injected result 0).
- CancelIoEx reports ERROR_NOT_FOUND (injected result 1168; completion-race classification).
- CancelIoEx reports a different error (injected ERROR_ACCESS_DENIED, 5).

The model asserts that the CancelIoEx return value alone never proves operation completion and never changes ownership. While the completion event remains unsignaled, the operation stays caller-owned/pending. If the bounded drain expires, ownership transfers once to the reaper queue; only the worker then observes a terminal completion, collects it once, and performs exactly-once cleanup. The caller performs no operation access after handoff.

## Implementation and execution

- Source: cancelioex-failure-policy-test.c
- Build: build-cancelioex-failure-policy-test.cmd
- Compiler: Visual Studio 2026 Build Tools 18.7.3, x64.
- Flags: MSVC /W4 /WX.
- Execution: initial build/run plus 10 additional separate process executions; all exit code 0, no stderr.
- Result: 21 assertions passed per process across three injected outcome cases (7 per case); 10/10 repeated runs passed.

## Interpretation and limitations

This is a small deterministic state/policy model. The injected callback returns a configured numeric error; it does not invoke CancelIoEx, create a pipe, submit overlapped I/O, wait on a kernel event, or run a worker thread. The modeled drain expiry and worker completion are state transitions in the test process, not elapsed-time or kernel events.

Consequently this result proves only that the model's policy assertions are internally consistent. It does not establish that the current implementation follows this policy, that Windows produces ERROR_ACCESS_DENIED for a still-pending operation, or that the proposed Reaper synchronization is race-free. A future implementation test should introduce a narrow injectable CancelIoEx function seam and verify that an injected non-ERROR_NOT_FOUND failure leaves the real pending operation owned and accounted for until a real terminal event/result is collected. Native API behavior and injected wrapper-policy behavior must remain separately labeled.

## Decision

The non-ERROR_NOT_FOUND cancellation-failure policy has a deterministic model test, but its native/API and implementation-level acceptance gates remain open. pipe-io-v2.c remains compile-only and has the previously documented completion-collection ownership blocker. Broker Production status remains NO-GO.
