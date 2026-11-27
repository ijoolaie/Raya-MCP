# Broker v2 bounded read protocol

Status: pure C frame codec, REQUEST/ERROR payload contracts, response TX/RX state machine, monotonic deadline arithmetic, and disposable message-mode pipe experiment implemented and locally tested. Not integrated into Broker.

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

Frame types are REQUEST, RESPONSE_HEADER, DATA, END, ERROR, and ACK. The codec recognizes all six values.

## REQUEST payload

The fixed eight-byte header contains:
- operation code (u16 LE): READ_FILE=1 or LIST_DIRECTORY=2;
- rootId length in bytes (u16 LE);
- relative-path length in UTF-16 code units (u16 LE);
- reserved field (u16 LE), required to be zero.

It is followed by a 1..64 byte ASCII rootId using only letters, digits, underscore, and hyphen, then the relative path as UTF-16LE. The whole payload must fit in the 16 KiB frame limit. READ_FILE requires a nonempty path; LIST_DIRECTORY may use an empty path to enumerate the configured root. UTF-16 surrogate pairs are validated and nonempty paths pass the standalone relative-path validator. REQUEST must be sequence zero.

This parser only validates syntax. It does not map rootId to configured ROOTS, authorize a caller, or acquire a request-scoped root HANDLE. Those remain separate security gates.

## ERROR payload

ERROR is a terminal sequence-zero frame with an eight-byte payload:
- numeric error code (u32 LE);
- retryable flag (one byte, 0 or 1);
- three reserved zero bytes.

Defined codes: BAD_REQUEST=1, UNKNOWN_ROOT=2, NOT_FOUND=3, ACCESS_DENIED=4, UNSUPPORTED_OPERATION=5, RESOURCE_LIMIT=6, INTERNAL=7. No raw OS error code, absolute path, or sensitive path detail is sent. The client acknowledges the ERROR with an ACK whose sequence and four-byte payload both echo zero; the connection can then close.

The parser enforces code bounds, retryable flag, reserved bytes, type and sequence. Mapping real Windows failures into these categories remains an integration responsibility.

## Response bounds and sequencing

- Maximum response body: 262,144 bytes (256 KiB).
- Maximum DATA payload: 16,384 bytes (16 KiB).
- Maximum DATA frames: 16. TX and RX reject a chunk size that would require more than 16 frames for the declared response size.
- RESPONSE_HEADER has an eight-byte payload: total response bytes (u32 LE), then chunk size (u32 LE).
- DATA chunks must have the exact expected length. All but the final chunk are exactly the negotiated chunk size.
- END has a zero-byte payload and is accepted only after exactly the declared body length has been received.
- Every response frame has a sequence starting at zero and incrementing by one. The receiver rejects gaps, duplicates, reordered frames, data before header, early END, duplicate END, and frames after END.
- The sender permits only one outstanding frame. It waits for an ACK matching that frame before emitting the next one.
- ACK uses the acknowledged sequence in its header and a fixed four-byte little-endian payload containing the same sequence. Stale, future, duplicate, truncated, or non-ACK acknowledgements are rejected.

## Deadline model

A request deadline is created once from a monotonic millisecond timestamp. Each frame's wait budget is the smaller of its per-frame cap and the remaining request-wide time. The budget is never reset for the next frame. Zero timeouts and timestamp overflow are rejected.

`deadline-tests.c` has 20 deterministic assertions. The overlapped named-pipe test also performs two pending reads under one 500 ms request deadline and 200 ms per-frame caps: the first read times out and is canceled, the second receives only the remaining budget, and both OVERLAPPED/event/buffer lifetimes remain valid until cancellation completion is observed.

The deadline arithmetic is not yet wired into the synchronous message-mode frame transport or a production asynchronous reaper. A future transport must apply the budget to each ACK read and to the request-wide operation without releasing resources at timeout.

## Implemented test coverage

- `read-frame-tests.c`: 57 pure C assertions for byte layout, malformed frames, size boundaries, the 16-frame ceiling, ACK validation, sequencing and END.
- `request-error-tests.c`: 44 assertions for REQUEST and ERROR schemas, rootId syntax, UTF-16/path validation, reserved fields, malformed payloads, error-code bounds, and terminal ERROR ACK.
- `message-pipe-frame-transport-tests.c`: 28 assertions using real Windows message-mode named pipes. Full transfers cover zero bytes, chunk-plus-one, and exactly 256 KiB. REQUEST and terminal ERROR are sent over the pipe with an ACK. A deliberately short read returns `ERROR_MORE_DATA`; a truncated frame is rejected; disconnect between frames fails closed.
- `overlapped-pipe-lifetime-tests.c`: 23 assertions for pending cancellation, normal completion, cancel/completion race, late cancellation, and request-wide/per-frame deadline interaction with actual overlapped pipe reads.
- `io-lifecycle-model-tests.c` and `overlapped-pipe-reaper-tests.c`: 34 and 42 assertions respectively for state transitions and heap-owned completion reaping.

## Required remaining transport work

1. Wire the request/per-frame deadline budget into actual asynchronous message-frame reads and ACK waits, using the tested cancellation/reaper ownership model.
2. Test disconnect before header ACK, between chunks, during final ACK, and after END.
3. Validate asynchronous transport ownership against supported Windows versions.
4. Map Windows failures to the defined ERROR categories without leaking raw paths or sensitive details.
5. Bind rootId to configured ROOTS and enforce request-scoped HANDLE lifetime before filesystem dispatch.

## Configured root binding and request-scoped handles

`root-registry.c/.h` adds a test-only binding layer. Service configuration supplies 1..8 absolute `rootId -> path` entries. The registry validates the rootId grammar and uniqueness, opens each configured root once with `FILE_FLAG_OPEN_REPARSE_POINT`, checks the returned HANDLE for disk-directory/non-reparse/non-delete-pending state, and retains it until registry shutdown.

A decoded READ_FILE request resolves rootId, opens the parent directory chain relative to the retained root HANDLE, then opens the final regular file relative to that pinned parent. LIST_DIRECTORY resolves the rootId and pins the requested directory; an empty relative path means the configured root. Every request holds an active registry reference. Registry shutdown returns `ERROR_BUSY` until all request contexts close. A context closes its owned file and directory-chain HANDLEs before releasing the reference.

The 44-assertion test covers two distinct configured roots, rootId selection, root-level and nested reads, root and nested directory listing, unknown roots, invalid paths, file size caps, directory-as-file rejection, context cleanup, active-context shutdown refusal, root namespace pinning, and invalid/duplicate configuration. Configured-root symbolic-link creation is skipped locally when privilege is unavailable; CI runs the reparse-root rejection case.

This is not a production root registry. It does not validate root owner/DACL, authenticate the client, load the actual application ROOTS configuration, or integrate with Node handlers. The configured paths are trusted test configuration, not request input.
## Relationship to filesystem security

Framing does not authorize filesystem access or repair path races. The receiver must validate all fields before exposing payload bytes, and the filesystem layer must independently enforce the pinned-root/relative-HANDLE policy. The hard-link mutation issue remains a separate unresolved security acceptance question.

## Non-goals

No production service installation, persistent service, ACL migration, workspace deployment, or integration into Raya Local MCP production handlers.
