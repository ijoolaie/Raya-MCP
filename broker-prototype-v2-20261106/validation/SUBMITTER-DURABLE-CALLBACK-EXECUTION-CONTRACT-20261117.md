# Durable Contract v1 — callback execution contract clarification — 2026-11-17

## Scope
Working Copy only. Clarified the caller-side execution/lifetime contract for the versioned callback table and added NULL/non-NULL context validation coverage. No callback invocation helper or runtime execution path was added. No legacy Submitter/Adapter/Worker integration, persistence, WAL, replay, or production change.

## Contract clarification
The public header now explicitly states:
- The owner must keep the callback table and opaque context alive throughout every invocation.
- Calls must be serialized unless the callback provider explicitly guarantees thread safety.
- Callback reentrancy into teardown of the owning object is prohibited.
- Every callback must initialize every output field, including exact structure size/schema, disposition, reserved fields, and an exact echo of the input operation identity.
- The consumer must normalize returned evidence; malformed or missing evidence is UNKNOWN.
- The pure validator permits NULL context; a concrete runtime may require non-NULL context.
- No timeout, cancellation, or exception isolation is implied.

## Tests
- Added explicit acceptance tests for NULL and non-NULL opaque context.
- MSVC x64 /W4 /WX /TC: successful.
- 65 checks/run × 25 runs = **1,625 checks passed, 0 failures**.
- Executable SHA-256: 9D7EF7A80909371019B0A613D19ADE86F124EEC5658965DDEC182DF94D551179.
- Implementation object SHA-256: 1F3A0F5740C8E4B0D34353F7EE47F15AE22C84ED0EF64D89AC4D0FA430E3D98E.
- Test object SHA-256: 5DD0B8A3288A9AB60B75AB8765C0DA0DED8599E54245795577FA352363191B22.
- Three temporary build artifacts were inspected and removed; zero matching artifacts remain.

## Source hashes
- raya-durable-contract-v1.h: 2FA73C0B77968D48909982379EC851DD5ECE1B0FD7DEEC7FD960AA04B7984B31
- raya-durable-contract-v1.c: 0CBEE0D35A98768A322D37B1BBA065BDBE44DBB1A513E366E1BDC42FA1CC637F
- raya-durable-contract-v1-test.c: 4CDD7E53C32987529B25736190CC427C13993BF7539BD765CAAEECFC66BC211F

## Open risks / next work
- The type layer cannot enforce callback table/context lifetime or provider thread-safety; these are caller obligations until a runtime owner exists.
- There is no timeout/cancellation/exception boundary, and no safe generic way to forcibly interrupt a callback.
- Next safe design slice: define an explicit runtime owner state machine and admission/drain protocol before any callback invocation code. That owner must serialize teardown, retain the table/context, prevent callback-initiated self-destruction, and represent uncertain callback outcomes without retrying blindly.
- Namespace ownership, durable sequence reservation, recipient receipts, WAL, and crash recovery remain unimplemented. No exactly-once claim.
