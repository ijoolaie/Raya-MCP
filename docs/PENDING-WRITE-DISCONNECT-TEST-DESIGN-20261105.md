# Deterministic pending-response-write disconnect test design — 2026-11-05

## Why the current evidence is insufficient
The existing ACK-window test proves that a second client can be admitted while the first client is connected but not reading. It does not prove the broker was blocked inside response WriteFile. The current fixture response is about 50 bytes, while CreateNamedPipeW configures an output buffer of MAX_PAYLOAD+128 (4224 bytes); the response may complete immediately and then wait only for the application-level ACK.

## Test objective
Prove the exact behavior when a client disconnects while the broker's overlapped response WriteFile is pending, and verify that the service remains usable and releases the single pipe instance within the configured 2-second write deadline.

## Isolated test design
1. Preserve the current broker source/binary. Create a separately named test-only source/build output.
2. In that variant only, configure a deliberately tiny named-pipe outbound buffer (for example, 1 byte) while returning a bounded response larger than that buffer. Keep the request and payload limits unchanged.
3. Add a test-only pending-write marker that is emitted only after WriteFile returns ERROR_IO_PENDING. Prefer a narrowly scoped named event or disposable marker in the existing protected fixture; do not log payload contents or credentials.
4. Start the disposable service with the variant and a disposable fixture. A native client connects, sends one valid read request, and intentionally does not read the response.
5. The controller waits for the pending-write marker, then closes the client handle deterministically.
6. Assert that the service observes the disconnect/cancellation, exits the write wait within the configured bound, drains the OVERLAPPED completion before releasing its event storage, and returns to the accept loop.
7. Immediately run a second valid client request and assert exact response framing/content. Record independent process exit codes and timestamps.
8. Repeat with: (a) client closes before WriteFile starts, (b) client closes after write completion but before ACK, and (c) client never reads and remains connected until ACK timeout. Keep these cases separate.
9. Stop/delete the disposable service and verify no broker process, service registration, ProgramData fixture, marker, or image directory remains.

## Acceptance criteria
- The controller proves the first WriteFile was pending before disconnecting the client.
- Disconnect is classified distinctly from write timeout, ACK timeout, malformed input, and service stop.
- No use of unbounded FlushFileBuffers or unbounded waits.
- OVERLAPPED event/HANDLE lifetime is safe: completion is drained before event close.
- Service remains RUNNING and a subsequent client succeeds.
- Exact exit codes, Win32 errors, timings and cleanup checks are captured.
- The normal broker protocol suite passes after the adversarial test.

## Scope and constraints
- Disposable prototype only; no Production handlers, service, workspace ACL/owner, ROOTS, tunnel, or runtime settings are changed.
- Do not install the broker as a persistent service or migrate real workspace data.
- If the test requires broader protected ownership changes or a deployment decision, stop and request separate approval.
