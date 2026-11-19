# Pipe I/O resource finalizer primitive — 2026-11-08

## Status and scope

Added a small, isolated finalization gate for the two operation-owned HANDLEs (duplicated issuing pipe and dedicated completion event). It is not connected to the Reaper worker and is not wired into `pipe-io-v2.c`. It deliberately does not free the operation record, OVERLAPPED, or private buffer.

Files:
- `pipe-io-finalizer-v2.h/.c`
- `pipe-io-finalizer-v2-test.c`
- `build-pipe-io-finalizer-v2-test.cmd`

## Contract

- Initialize only after the operation has been positively collected as terminal using the exact event and issuing HANDLE/OVERLAPPED pair.
- Close the duplicated issuing pipe first, then the completion event. The caller must serialize the record and invoke the close callback outside the Reaper lock.
- Each successful close is committed independently by nulling only that handle and incrementing the success count.
- Any CloseHandle failure moves the finalizer to sticky CLOSE_UNCERTAIN. The failed handle value is retained, later close attempts are rejected, and no release commit is allowed. The caller must record `RAYA_RECOVERY_CAUSE_HANDLE_CLOSE` in the lifecycle core and must not report clean shutdown.
- Memory release is eligible only after both handles were positively closed. The primitive records exactly one release commit; the integrating owner remains responsible for freeing the record, OVERLAPPED, and private buffer exactly once.
- The close callback exists for deterministic fault injection. Production integration must use a narrowly wrapped CloseHandle and must not treat callback success as proof beyond the Win32 API's successful return.

## Verification

MSVC x64 `/W4 /WX`: 30 assertions per process, 25/25 independent runs passed (750 assertions). Tests cover normal ordered closure, first-handle failure, second-handle failure after partial success, sticky failure/no retry, retained failed handle, no premature release, double-release rejection, duplicate/invalid handles, and null callback.

This is a resource-finalization primitive only. It does not prove terminal I/O collection, provide thread safety, manage a worker/queue/registry, or implement lifecycle shutdown. No actual handles were closed by the synthetic-handle tests. The lifecycle-managed Reaper and 28-group native acceptance matrix remain mandatory. Production remains NO-GO.
