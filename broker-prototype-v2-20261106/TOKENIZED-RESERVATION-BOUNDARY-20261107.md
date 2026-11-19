# Tokenized Reservation Boundary — 2026-11-07

## Change
The integrated Reaper no longer exposes anonymous `reserve(state)` and `unreserve(state)` counter operations.

Each `raya_integrated_op` now carries a reservation state:
- `NONE`: no capacity has been reserved for this operation.
- `HELD`: one registry slot is reserved for this exact operation before handoff.
- `HANDED_OFF`: the operation has transferred to the queue/worker lifecycle.
- `RELEASED`: terminal collection removed the registry entry and released the slot.

The reserve API now requires the operation pointer. Duplicate reservation is rejected. Handoff requires that same operation to be in HELD state and changes it to HANDED_OFF only after the Core handoff succeeds. Promotion requires HANDED_OFF. Terminal collection alone releases the reservation and marks it RELEASED. An uncertain result retains the reservation, registry entry, outstanding count, operation and event.

There is deliberately no anonymous unreserve API. This avoids allowing a failed reservation attempt to decrement another operation's capacity. It also means a pre-handoff caller that has reserved capacity must continue through handoff; an abandoned held reservation is a conservative capacity leak and requires a future explicit cancellation/lease design before broader integration.

## Shutdown race
A focused test covers an admitted and reserved caller racing shutdown: after STOPPING, new admission and reservation fail, while the pre-admitted caller may hand off; the worker drains and terminally collects the operation before exit.

## Validation
MSVC `/W4 /WX`, boundary build command exit 0:
- Integrated baseline: 25 assertions, 0 failures.
- Capacity: 3,085 assertions, 0 failures; all 256 slots filled and drained; 257th reservation rejected.
- Uncertain retention: 16 assertions, 0 failures.
- Shutdown race: 16 assertions, 0 failures.

Executables and object files are deleted by the build script after each test.

## Limitations / gate
These are single-threaded, externally-lock-held state-machine tests. They do not establish safe pointer ownership against malicious/mistaken callers, do not exercise real multi-producer/worker concurrency, do not connect the WaitPlan/Worker, and do not test actual OVERLAPPED completion or the 28-group named-pipe acceptance matrix.

No changes to `pipe-io-v2.c`, Production transport, tunnel, or IranPriceScout. No commit/reset. Production remains **NO-GO**.
