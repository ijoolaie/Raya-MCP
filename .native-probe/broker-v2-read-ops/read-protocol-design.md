# Broker v2 bounded read-response framing

Status: pure C frame codec, response TX/RX state machine, and disposable message-mode pipe experiment implemented and locally tested. Not integrated into Broker.

## Wire envelope

Each frame has a 16-byte header followed by exactly the declared payload. Fields are encoded explicitly in little-endian order; untrusted bytes are never cast to a packed C struct.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic bytes `RYA2` |
| 4 | 2 | Protocol version (`2`) |
| 6 | 2 | Frame type |
| 8 | 4 | Sequence number |
| 12 | 4 | Payload length |
| 16 | N | Payload |

Frame types are REQUEST, RESPONSE_HEADER, DATA, END, ERROR, and ACK. The codec recognizes all six values. The implemented response state machine currently handles RESPONSE_HEADER, DATA, END, and ACK; REQUEST payload validation and ERROR semantics remain unspecified.

## Bounds and sequencing

- Maximum response body: 262,144 bytes (256 KiB).
- Maximum DATA payload: 16,384 bytes (16 KiB).
- Maximum DATA frames: 16. TX and RX reject a chunk size that would require more than 16 frames for the declared response size.
- RESPONSE_HEADER has an eight-byte payload: total response bytes (u32 LE), then chunk size (u32 LE).
- DATA chunks must have the exact expected length. All but the final chunk are exactly the negotiated chunk size.
- END has a zero-byte payload and is accepted only after exactly the declared body length has been received.
- Every response frame has a sequence starting at zero and incrementing by one. The receiver rejects gaps, duplicates, reordered frames, data before header, early END, duplicate END, and frames after END.
- The sender permits only one outstanding frame. It waits for an ACK matching that frame before emitting the next one.
- ACK uses the acknowledged sequence in its header and a fixed four-byte little-endian payload containing the same sequence. Stale, future, duplicate, truncated, or non-ACK acknowledgements are rejected.
- The receiver exposes payload as a view into the decoded wire buffer. A future transport must consume/copy it before reusing that buffer.

## Implemented test coverage

`read-frame.c` implements bytewise encoding/decoding and a pure response TX/RX state machine. `read-frame-tests.c` currently has 57 passing assertions, covering exact wire layout, malformed frames, size boundaries, the 16-frame ceiling, ACK validation, sequence/order errors, and terminal END behavior.

`message-pipe-frame-transport-tests.c` adds 12 assertions using real Windows message-mode named pipes:

- Full one-message-per-frame transfers for zero bytes, chunk-plus-one, and exactly 256 KiB.
- ACK round-trip after every response frame.
- A short message-mode read returns `ERROR_MORE_DATA` and is rejected.
- A complete pipe message containing a truncated frame is rejected by the codec.
- A peer disconnect between frames fails closed.

The transport test uses synchronous I/O in a disposable local process. It does not yet implement bounded per-frame ACK deadlines or an overall request deadline, and it does not cover disconnect at every protocol phase.

## Required remaining transport work

1. Add bounded per-frame ACK deadlines and one overall request deadline; do not reset an unlimited timeout for each chunk.
2. Test disconnect before header ACK, between chunks, during final ACK, and after END.
3. Test malformed messages and transport errors using actual message-mode pipes.
4. Define REQUEST payload schema and ERROR numeric-code semantics without raw OS paths or sensitive details.
5. Validate the async transport against the cancellation/reaper ownership model and supported Windows versions.
6. Integrate only after the native transport, deadlines, and cancellation/reaper tests pass.

## Relationship to filesystem security

Framing does not authorize filesystem access or repair path races. The receiver must validate all fields before exposing payload bytes, and the filesystem layer must independently enforce the pinned-root/relative-HANDLE policy. The hard-link mutation issue remains a separate unresolved security acceptance question.

## Non-goals

No production service installation, persistent service, ACL migration, workspace deployment, or integration into Raya Local MCP production handlers.
