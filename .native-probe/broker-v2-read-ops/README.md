# Broker v2 read-operation foundation

Status: research-only, isolated branch. No production handler wiring.

## Delivered in this slice

- A standalone UTF-16 relative-path lexical validator.
- Rejects rooted and drive-qualified paths, alternate separators, empty components, dot/dot-dot, ADS colons, control characters, Win32 wildcard characters, trailing dot/space, and reserved device names.
- A standalone test harness with positive nested-path cases and negative traversal/device-name cases.
- A Windows CI step that builds the helper with MSVC `/W4 /WX` and runs the test harness.

## Explicit limitation

This is only the lexical validation gate. It does not open or authorize filesystem objects and is not a confinement boundary. A caller must still traverse one component at a time using `NtCreateFile` relative to the already-pinned parent directory HANDLE, reject reparses, validate each opened object using information queried from that same HANDLE, and keep parent handles alive for the traversal.

## Next implementation gates

1. Implement and test HANDLE-relative directory traversal with an explicit stack of open directory handles.
2. Add adversarial directory replacement and reparse-point tests.
3. Define bounded multi-frame named-pipe response framing; current one-response protocol is insufficient for the existing 256 KiB read contract.
4. Resolve pending-I/O cancellation lifetime without freeing stack-owned OVERLAPPED state before completion.
5. Only then implement the four read operations and consider a Node adapter.

## Current product decision

Do not connect this research component to `server.mjs` or `advanced-tools.mjs`. Do not install a persistent or production service. PR #19 remains a separate test-only Draft.