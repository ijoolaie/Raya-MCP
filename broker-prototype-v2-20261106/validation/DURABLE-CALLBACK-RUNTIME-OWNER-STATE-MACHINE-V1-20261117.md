# Durable Callback Runtime Owner v1 — State Machine Slice
Date: 2026-11-17

## Scope and location
Working Copy only: `D:\Work\project\Raya-MCP\broker-prototype-v2-20261106`.
Added a standalone deterministic owner-state module. It is not wired into Submitter, Adapter, Worker, Orchestrator, or any production path. No callback is invoked, and no persistent storage or replay is implemented.

## State and transitions
- States: CREATED -> ACTIVE -> CLOSING -> CLOSED; an UNKNOWN callback result moves the owner to QUARANTINED.
- Admission is allowed only in ACTIVE. Each admitted callback gets a monotonically increasing nonzero lease ID.
- Completion with determinate/known outcome releases the active lease exactly once.
- Completion with UNKNOWN removes it from in-flight count, retains its lease record as unresolved, and quarantines the owner. New admission is denied.
- BeginClose permanently closes admission. It returns BUSY while callbacks remain in flight or unknown effects remain unresolved.
- Reconciliation requires an explicit determinate external decision for the exact unknown lease ID. It cannot resolve by passing an indeterminate result. Once all unknowns are reconciled, state returns to CLOSING if close was requested, otherwise ACTIVE.
- FinishClose succeeds only after close was requested, in-flight is zero, and no UNKNOWN remains; it then clears the callback-table pointer in the state object.
- Lease IDs are not reused. The bounded table holds at most 32 simultaneous active plus unresolved records; capacity exhaustion and sequence exhaustion fail closed.

## Concurrency and ownership contract
Every operation requires the caller to hold one external exclusive lock and serialize all owner-state operations. This module contains no lock, atomic primitive, callback invocation, allocator, timeout, cancellation, or callback exception boundary. It is not thread-safe by itself.
The caller must validate the callback table against its configuration before owner initialization, keep the table and opaque context alive until CLOSED and all external users have stopped, and prevent callback reentrancy into owner teardown. The module stores a non-owning table pointer only; it does not retain/free the context.

## Defensive invariants
Every public transition validates structure/schema, reserved fields, callback pointer/state relationship, lease ID uniqueness, lease-state validity, and agreement between lease records and in-flight/unknown counters. It also enforces valid close_requested/lifecycle combinations. Duplicate, stale, malformed, corrupted, and invalid-state operations fail closed.

## Validation
- MSVC x64 C compilation: `/W4 /WX /TC`, successful.
- Test executable: 80 checks/run, 0 failures.
- Repeated 25 times: **2,000 assertions passed, 0 failures**.
- Covered pre-activation admission rejection, invalid repeated activation, admission IDs, close admission gate, drain pending, duplicate/stale completion, successful close, UNKNOWN quarantine, admission denial while quarantined, refusal to close with UNKNOWN, indeterminate reconciliation rejection, exact-ID reconciliation, quarantine recovery with/without close requested, bounded lease capacity, counter corruption, reserved-field corruption, invalid state/close-flag combinations, duplicate lease-record IDs, and sequence exhaustion.
- Executable SHA-256: `982FC5ABA507F4801C0FD149AB213D61BF975D7BC0E4DDF522FB5A9B8BDD47E6`.

## Final source hashes
- `raya-durable-owner-v1.h`: `0CEC413CB1917A547E09CE719B617C040C0D55DACB1FD765BAE93544E71D0011`
- `raya-durable-owner-v1.c`: `08279D27728F641C5F6608633BFF7423D05EE76067F46B036A6EA345E4E31DBA`
- `raya-durable-owner-v1-test.c`: `A1E8401D8FDA45E4756FF0723CA9E191DA2298655A69F2F5259DAEB08F2E7DA4`

## Limits and next gate
This proves only deterministic state transitions under the stated external-serialization contract. It does not prove thread safety, callback lifetime enforcement, recipient commit, durable identity reservation, WAL ordering, crash recovery, replay safety, or exactly-once effects. A later runtime slice must add a real synchronization/lifetime boundary and callback invocation wrapper, with durable EFFECT_BEGIN/result semantics, before any integration. UNKNOWN must remain quarantined unless external evidence explicitly resolves it. No production readiness or Phase 1 acceptance is implied.
