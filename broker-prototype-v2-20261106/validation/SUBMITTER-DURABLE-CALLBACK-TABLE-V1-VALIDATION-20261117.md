# Durable Contract v1 — versioned callback table validation — 2026-11-17

## Scope
Working Copy only. Added a versioned callback-table type and a pure validator. No callback is invoked. No legacy Submitter API, Adapter, Worker, persistence, WAL, replay, or production behavior was changed.

## API additions
- `raya_durable_issue_callback_v1`: WINAPI function pointer receiving opaque context, immutable operation identity, and an Issue result output.
- `raya_durable_handoff_callback_v1`: equivalent typed Handoff callback.
- `raya_durable_callbacks_v1`: exact-size/schema-versioned table with capability declaration, reserved fields, opaque context, and callback pointers.
- `raya_durable_callbacks_validate_v1`: validates the associated config first, then table size/version/reserved fields, exact capability equality, and required callback-pointer presence.

## Validation behavior
- Config capability declaration must exactly match callback table declaration.
- A declared TYPED_ISSUE capability requires a non-NULL Issue callback; absence is rejected.
- A declared TYPED_HANDOFF capability requires a non-NULL Handoff callback; absence is rejected.
- A callback pointer without its declared capability is also rejected.
- Invalid config is returned as-is; NULL table, wrong size/version, nonzero reserved fields, or capability mismatch fail closed.
- The validator does not inspect callback implementation behavior and never calls either callback.

## Test and build results
- MSVC x64, `/W4 /WX /TC`: successful.
- 63 checks per run × 25 runs = **1,575 checks passed, 0 failures**.
- Executable SHA-256: `5E7A1A4540459239E54005770F4C4CDF06DD984C4AED3143A4B3291A7197FD33`.
- Implementation object SHA-256: `7222FED8373268FAE980075DEAECA6F7ED3E4098272802AA8B0659E7B6BC3BA5`.
- Test object SHA-256: `2DB0CD4DAD32F4D0CF77E180B3913973098ABD2C28CA313BA0601C93D1B6983A`.
- All three temporary build artifacts were inspected and removed. Matching temporary artifacts remaining: **0**.

## Source hashes after this slice
- `raya-durable-contract-v1.h`: `92560184BC87574A17F27C6A328F7B8920D5D292F3C3AFEDC7B915DBE183ED16`
- `raya-durable-contract-v1.c`: `0CBEE0D35A98768A322D37B1BBA065BDBE44DBB1A513E366E1BDC42FA1CC637F`
- `raya-durable-contract-v1-test.c`: `9EA78CD20AB46A3E41E7E1D4F26E35366A21DCCB66C334C7763E249C4FD2BAB9`

## Remaining contract decisions / limitations
- Callback invocation serialization, concurrency, reentrancy, cancellation, timeout, and context lifetime are not yet specified.
- Callback must initialize a complete result including the echoed operation identity; consumer-side normalization remains mandatory.
- The opaque context may be NULL at this type-validation layer; whether a concrete runtime requires non-NULL context remains a start-API policy decision.
- No durable namespace ownership, durable sequence reservation, recipient idempotency/receipt verification, or crash recovery is implemented.
- This is not an exactly-once guarantee. Legacy API remains isolated and unchanged.
