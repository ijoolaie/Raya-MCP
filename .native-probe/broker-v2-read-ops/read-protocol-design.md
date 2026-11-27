# Broker v2 bounded read-response framing — design note

Status: proposal for review; not implemented and not wired into the test Broker.

## Why the current probe protocol is insufficient

The current pinned-read probe sends a textual response header and the entire payload as one named-pipe message. Its payload cap is 4 KiB, while the existing client reads into a 512-byte response buffer. That is adequate only for the small fixture used by the probe. It is not a sound transport for the Local MCP read contract (up to 256 KiB), and simply increasing the one-shot buffer would leave message-size and partial-read behavior implicit.

## Proposed frame envelope

Use one named-pipe message per frame. Encode fields explicitly in little-endian order; do not cast untrusted bytes to a packed C struct.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic bytes `RYA2` |
| 4 | 2 | Protocol version (`2`) |
| 6 | 2 | Frame type |
| 8 | 4 | Sequence number |
| 12 | 4 | Payload length |
| 16 | N | Payload |

Initial frame types:

- `REQUEST`: operation code, fixed root identifier, and bounded relative UTF-16 path; never a caller-supplied absolute path.
- `RESPONSE_HEADER`: total response byte count and negotiated chunk size.
- `DATA`: one sequential payload chunk.
- `END`: zero-length terminal frame.
- `ERROR`: bounded numeric error code; no raw OS paths or sensitive details.
- `ACK`: acknowledges exactly one server frame sequence.

## Proposed bounds and sequencing

- Maximum response body: 262,144 bytes (256 KiB).
- Maximum data chunk: 16,384 bytes (16 KiB).
- Maximum data frames: 16, plus header and terminal frames.
- Each frame's declared payload length must equal the actual message length minus the 16-byte envelope.
- Sequence numbers start at zero and increase by exactly one; duplicate, skipped, reordered, unknown, or post-END frames fail closed.
- The receiver validates magic, version, type, sequence, length, and aggregate byte count before exposing payload bytes to the caller.
- The sender waits for an ACK matching the exact frame sequence before sending the next frame. ACKs have fixed-size payloads and are validated as complete pipe messages.
- A disconnect, malformed ACK, timeout, or size overflow aborts the response; partial data is discarded by the caller.

## Required implementation tests

1. Exact boundary: zero bytes, one byte, one chunk, chunk plus one, exactly 256 KiB.
2. Reject 256 KiB plus one, oversized chunk, truncated header, truncated payload, extra trailing bytes, unknown type/version/magic.
3. Reject wrong sequence, duplicate ACK, stale ACK, ACK for future frame, DATA before header, DATA after END, duplicate END.
4. Disconnect before header ACK, between chunks, during final ACK, and after END.
5. Ensure all frame reads use a buffer large enough for the maximum envelope plus chunk, and handle message-mode `ERROR_MORE_DATA` as a protocol failure rather than accepting a truncated frame.
6. Enforce one overall request deadline and a bounded per-frame deadline; do not reset an unlimited timeout for each frame.

## Cancellation and overlapped-I/O lifetime blocker

The current probe's `wait_io` calls `CancelIoEx` and then waits indefinitely for the event before freeing the OVERLAPPED and event. This preserves memory lifetime if completion is eventually delivered, but makes stop/timeout completion unbounded. It must not be "fixed" by closing the event or reusing the OVERLAPPED early.

Before production use, choose and test an explicit lifetime model (for example, a bounded operation owner/reaper that retains the OVERLAPPED, event, pipe handle, and buffers until cancellation completion is observed). Bound the number of outstanding operations and define shutdown behavior if a driver never completes cancellation. This design decision is separate from frame format.

## Non-goals

This proposal does not authorize filesystem access, define the final request schema, change the current probe, claim production readiness, or resolve hard-link mutation after a single-link check. Framing should be implemented only after the request/response contract and cancellation owner model are agreed and tested independently.
