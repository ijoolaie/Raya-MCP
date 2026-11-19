# Windows Pending Journal Slot Codec v1 — Implementation Report

Date: 2026-11-10
Status: pure codec and selector implemented; no filesystem adapter or runtime integration

## Files
- `windows-pending-slot-v1.h`
- `windows-pending-slot-v1.c`
- `windows-pending-slot-v1-test.c`
- `build-windows-pending-slot-v1-test.cmd`

## Implemented contract
- Fixed 256-byte slot; byte offsets match `WINDOWS-PENDING-JOURNAL-ADAPTER-V1-DESIGN-20261110.md`.
- Big-endian version, revision and bitwise complement.
- Embedded 128-byte RYPI record is validated using the existing journal decoder.
- SHA-256 over bytes 0..151 is computed through Windows CNG (`BCrypt*`).
- CRC32 and complement cover bytes 0..183.
- Bytes 192..255 must be zero.
- No native struct serialization, file I/O, mutex, ACL, service, network, or persistent state.

## Selection behavior
- Both all-zero slots: returns UNPROVISIONED. Runtime callers must reject this unless executing an explicitly authorized provisioning flow.
- Exactly one valid slot and one all-zero slot: select the valid slot.
- Two valid slots with different revisions: select the higher revision, unless a valid slot has UINT64_MAX revision; that exhausted state fails closed.
- Two byte-identical valid slots with equal revisions: select slot 0.
- Same revision with different bytes, any malformed nonzero slot, or invalid embedded record: fail closed.
- A TOMBSTONED embedded RYPI record remains valid terminal evidence; selector does not erase or reinterpret it.

## Verification
- MSVC x64 build with `/W4 /WX`: PASS.
- 300 assertions per fresh process.
- 25 fresh-process runs: 7,500 assertions PASS.
- Tests cover encode/decode, CNG SHA-256 execution, revision/complement, invalid version, malformed embedded record, digest/CRC/reserved corruption, every single-byte mutation of a valid slot, all-zero/unprovisioned state, one-valid/one-empty, ordered revisions, identical equal revisions, conflicting equal revisions, malformed second slot, tombstone preservation, and UINT64_MAX revision decode plus selector fail-closed behavior.

## Important limitations
- SHA-256 and CRC are consistency/corruption checks, not authentication, tamper resistance, or anti-rollback.
- A revision is not an external monotonic authority; a restored older pair of valid slots can still be selected.
- Selector's UNPROVISIONED result is not permission for runtime auto-provisioning.
- No real disk write, short-write loop, FlushFileBuffers, readback, process-kill, power-loss, filesystem, file-ID, reparse-point, ACL, service-token, mutex, concurrency, or VM-rollback behavior has been tested.
- This is not proof that two 256-byte writes are atomic or power-loss durable.
- No changes to Production, Windows services, protected roots, or IranPriceScout.

Production status: NO-GO.
