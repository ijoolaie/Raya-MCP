# Completion Result Classification Probe — 2026-11-07

Status: superseded by GETOVERLAPPEDRESULT-LIVE-PROBES-20261107.md. The earlier version of this note incorrectly treated selected Win32 error values as generic terminal I/O results. That allowlist was withdrawn after reviewing Microsoft's GetOverlappedResult contract.

Current classifier contract:
- API success -> TERMINAL_SUCCESS.
- FALSE + ERROR_IO_INCOMPLETE -> INCOMPLETE.
- Every other failure -> UNCERTAIN.

No failed result other than ERROR_IO_INCOMPLETE is promoted to a generic terminal category by this helper. A successful GetOverlappedResult is the positive collection result in the current contract. A specific failed result may be treated as terminal only under a separately reviewed, operation-specific contract with evidence; do not infer cleanup safety from an error name alone.

The generic helper remains deliberately conservative and is not integrated into pipe-io-v2.c. A separate context-aware Named Pipe helper was added as `pipe-completion-context-v2.h/.c`: it accepts `ERROR_OPERATION_ABORTED` as terminal only when the caller asserts that the dedicated event for this operation was observed signaled and that result collection uses the exact issuing pipe handle and OVERLAPPED pointer. The helper cannot verify those facts itself; its caller must establish them from the operation record and Win32 calls. It continues to classify `ERROR_IO_INCOMPLETE` as incomplete and all other failures as uncertain. Its 10 assertions passed under MSVC `/W4 /WX` and 10/10 repeated process runs. This helper is not integrated into pipe-io-v2.c and does not prove runtime ownership safety. The live Named Pipe tests, actual observations, and limitations are recorded in GETOVERLAPPEDRESULT-LIVE-PROBES-20261107.md.

Production remains NO-GO.
