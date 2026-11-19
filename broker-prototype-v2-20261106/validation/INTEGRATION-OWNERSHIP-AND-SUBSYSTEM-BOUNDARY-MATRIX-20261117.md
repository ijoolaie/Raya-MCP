# Integration Ownership and Subsystem Boundary Matrix — 2026-11-17

Status: source-backed architecture review; no runtime integration or acceptance claim.
Scope: Working Copy only — `D:\\Work\\project\\Raya-MCP\\broker-prototype-v2-20261106`.
Production: NO-GO. Phase 1: NOT ACCEPTED.

## Executive result

The repository contains several substantial but separate subsystems. The source tree does not establish one integrated application lifecycle connecting the pipe Adapter/Submitter/Worker chain, the process Supervisor, the generation and pending-operation journals, the durable WAL/runtime, and the Evidence Service designs.

A non-test C/H call-site scan found definitions/declarations for the reviewed APIs, but no production caller outside each component's own implementation for:
- `raya_integrated_adapter_start` / `raya_integrated_adapter_submit`;
- `raya_managed_pipe_submit`;
- `raya_orchestrator_context_register` and the owner cleanup entrypoints;
- `raya_supervisor_start`;
- `raya_pending_store_prepare`;
- `raya_ledger_load` / `raya_ledger_reserve_next`;
- `raya_wal_scan_v1` and `raya_durable_runtime_init_v1`.

Tests and build scripts are deliberately excluded from that production-callsite conclusion. It means the Working Copy does not demonstrate runtime wiring; it does not prove that an external consumer or another repository does not exist.

## Correction to the previous entrypoint review

The earlier statement that no build scripts were present was too broad. This review found 76 top-level `build-*.cmd` scripts for individual modules and tests. The root does not show a conventional CMakeLists.txt, Visual Studio solution/project, Makefile, or application manifest in the examined candidate set. Therefore:
- module-specific build/test entrypoints exist;
- a single top-level product build or application entrypoint was not identified;
- the absence of a conventional project manifest must not be confused with the absence of build scripts.

## Ownership and integration matrix

| Component | Creation / initialization | Runtime owner and borrowed dependencies | Completion / recovery | Destruction / current gap |
|---|---|---|---|---|
| Orchestrator Context + Registry | Caller initializes Context and registers it in Registry | Context storage remains caller-owned, cannot move/copy after registration; stores Adapter, Submitter, Worker pointers and Worker Owned/Borrowed metadata | Registry leases protect Context access; cleanup-owner token/cursor serializes the implemented teardown steps | Context/Registry reclamation is a separate caller action. Context metadata does not itself own/free component storage |
| Integrated Adapter | Caller supplies uninitialized Submitter storage; Adapter initializes Submitter during start | Adapter borrows Worker; Submitter storage remains caller-provided; Adapter owns its envelopes and internal lock | Submit path returns REJECTED/QUEUED/RETAINED; retained list is internal and there is no general public per-operation retained-recovery API | Adapter Destroy closes admission and drains Adapter activity, but intentionally does not stop/destroy Submitter or Worker |
| Submitter | Initialized by Adapter | Operational API admission gate protects its five public operational calls; direct callers must be quiesced before lifecycle teardown | Structured Shutdown/Join/Destroy results; Orchestrator cursor records lifecycle outcomes | Orchestrator implements admission close, Shutdown, Join, Destroy. Concurrent lifecycle calls are not generally safe; external serialization remains required |
| Managed Worker | Started externally before Adapter start | Borrowed by Adapter; Orchestrator records Owned/Borrowed as teardown policy metadata | Worker owns handed-off pending I/O and retained work; terminal completion/finalizer paths release operation resources | Orchestrator's current cleanup step list has no Worker Shutdown/Join/Destroy step. No owner-only Worker teardown API is present in the reviewed Orchestrator header/source. Owned Worker teardown is therefore an unresolved external step |
| Managed Pipe I/O / Pipe Owner / Finalizer | Invoked by Adapter/Worker internals or separate callers | Duplicated pipe/OVERLAPPED/buffer resources move through Worker ownership; Pipe Owner and Finalizer guard release | Exact terminal completion and finalizer/owner release are local resource-lifecycle evidence | No production caller above Adapter was found. These primitives do not create a durable operation identity or recipient receipt |
| Process Supervisor | Caller supplies application, mutable command line and working directory; Supervisor creates Job and suspended child | Supervisor owns Job/process/primary-thread handles; Job is configured kill-on-close | Wait/terminate/exit-code state is local process lifecycle | Close only after signaled process; failure can leave DEGRADED with handles/live child retained. No integration with Adapter, Worker, Orchestrator, IPC, or service startup was found |
| Generation Ledger + CNG | Caller supplies storage callbacks; CNG helper supplies system RNG | Ledger stores callback table and epoch in caller-owned object | Two-slot callback model reserves epoch then read/flush/readback; CNG adds random suffix | No concrete filesystem adapter, process lock, anti-rollback anchor or production caller. Not connected to Supervisor generation lifecycle |
| Pending Operation Journal | Caller supplies journal object and write/flush/readback callbacks | In-memory state machine; caller owns store callbacks and transaction coordination | Pure record codec/state machine and classifier; tombstone callback path exists in model | No concrete Windows file adapter, load/open/lock contract or production caller. Expected sequence and Witness proof remain caller responsibilities |
| Windows Pending Slot Codec / Store Model | Test/model caller supplies slot bytes or simulated volatile/durable images | Codec is pure byte validation; Store Model is in-memory fault simulation | Two-slot selection and fault outcomes are modeled | Not a real file adapter; no FlushFileBuffers, cross-process mutex, ACL, service identity or restart recovery |
| Durable Contract / Owner / Runtime / WAL | Caller initializes in-memory objects and passes callback table / byte buffers | Owner/runtime use caller-owned state and locks; WAL scanner reads caller-owned bytes | Typed callback invocation, lease/quarantine state, structural WAL scanning | No WAL writer, file persistence, startup replay, namespace allocator or integration into Adapter/Submitter/Worker. Recipient verifier/status callback is absent |
| Evidence Service | Design documents only | Provider, DB, object store, witness, key and service ownership not selected | Commit/recovery/trust/receipt protocols are design candidates | No deployed service, persistent adapter, credentials, endpoint, runtime caller or production integration |

## Specific boundary findings

### 1. Orchestrator teardown is incomplete for an Owned Worker

The current cleanup step constants cover:
0. Adapter Destroy
1. Submitter admission close
2. Submitter Shutdown
3. Submitter Join
4. Submitter Destroy

The Context also stores Worker pointer and Owned/Borrowed metadata, but the reviewed API has no Worker cleanup step or owner-only Worker lifecycle function. Adapter explicitly borrows Worker and never tears it down. Consequently, the Owned designation currently describes intended policy but does not implement or prove Worker teardown. Do not add a Worker destroy call until the exact order is specified against retained Worker work, pipe finalizers, and issuer-thread constraints.

### 2. Adapter/Worker lifecycle is not equivalent to application lifecycle

Adapter Destroy can succeed only after its own entrants, active work and retained envelopes are drained. It does not imply Worker shutdown. Submitter teardown is explicitly separate. Worker can still own handed-off I/O, and issuer-thread lifetime requirements remain relevant. A product-level shutdown coordinator must first establish a quiescence/drain policy across all three components; the existing Adapter/Submitter cursor covers only part of that graph.

### 3. Local journals are separate transaction models

The Pending Operation Journal, Generation Ledger and Durable WAL each have different record formats, state machines and evidence semantics:
- Pending Journal models PREPARED → WITNESS_COMMITTED → LOCAL_COMMITTED → TOMBSTONED with caller-supplied storage callbacks.
- Generation Ledger reserves a monotonic local epoch through two slots and caller-supplied storage callbacks.
- Durable WAL v1 is a caller-buffer-only structural scanner with ISSUE and HANDOFF effect planes; it has no file writer or replay.

No production call path joins these three into one atomic transaction. Their operation identifiers, namespace authority, ordering and cross-store recovery rules are not selected by runtime code.

### 4. Supervisor is a separate process primitive

The Supervisor starts a child suspended, assigns/verifies Job membership, resumes it and tracks process handles. Its design explicitly states that it is not integrated with Adapter or Managed Worker. No IPC handshake, service identity binding, authenticated pipe, restart orchestration or Broker caller is established by the current source.

### 5. Evidence Service is not an implemented dependency

Evidence receipt, commit crash recovery and trust/anti-rollback are design documents. They do not provide a runtime evidence verifier to the Durable Contract callbacks. In particular, recipient-commit capability must not be advertised based only on declared capability bits; the runtime callback surface currently has ISSUE/HANDOFF callbacks, not a recipient receipt/status verifier.

## Risks and required decisions before implementation

1. Define the actual product process and its entrypoint/build composition; identify whether it is a service, child process, library consumer or external host.
2. Define one lifecycle authority and an explicit dependency DAG for Supervisor, Worker, Adapter, Submitter, Pipe Owner/Finalizer, Orchestrator Context and Registry.
3. Extend the teardown design to cover Worker ownership, worker-held retained operations, issuer thread completion and pipe finalization. Define the exact safe ordering before code changes.
4. Define how RETAINED is surfaced, inventoried, reconciled and eventually released without treating suppression of the completion callback as success/failure.
5. Choose whether Pending Journal + Generation Ledger or Durable WAL is the authoritative transaction record for a given operation class. Do not create overlapping sources of truth.
6. Define operation namespace/ID assignment and propagation from caller through Adapter, Submitter, Worker and completion/reconciliation records.
7. Select and approve storage identity, ACL, cross-process locking, anti-rollback authority and restart recovery before implementing persistent adapters.
8. Define Supervisor-to-child authenticated IPC, process identity binding, restart policy and shutdown coordination.
9. Define recipient receipt/status verification before any RECIPIENT_COMMIT guarantee is enabled.
10. Only after the above, build one narrow integration harness around the real chosen caller; keep it separate from Production until explicit acceptance.

## Verification and scope

- Read-only source/header/document inspection and non-test C/H call-site search.
- Confirmed 76 top-level `build-*.cmd` scripts.
- No build or tests run.
- No source, header, test, build script, README, configuration or Production file changed.
- No process terminated, install/download performed, or temporary artifact created/deleted.
- Working Copy only; no Git metadata/status claim.

## Source fingerprints

SHA-256 fingerprints captured during this review:

| File | SHA-256 |
|---|---|
| raya-orchestrator-v1.h | A73F9961F7C02EE3F64488DC7920BDD092BA4F5763FF05A74A2B4FBF6B342EC7 |
| raya-orchestrator-v1.c | 80073CBFFDCBBF96B01879562506616F0ED2E969081D0F50C32BE56D8E534F25 |
| integrated-submit-adapter-v2.h | DB52B1D6723DCA1159AD8737B83E64EFE26FF4D5F4053A32F9D1C8CD6C0AA1DC |
| integrated-submit-adapter-v2.c | A9B39D0D4094E8418F820E53B3E28FEE64FC7B0A9EC417450F25EC422EE8D87F |
| reaper-managed-worker-v2.h | 5D0230BC5295AF2DFE8B8BC4B64E0E7D1938F977A446F0C785CEA0C2E0918F96 |
| reaper-managed-worker-v2.c | 5B0B43BA60303A0EC3EA07A9E26A641E32E40812702C0F87EC37C1ADD11DD8DD |
| pending-operation-journal-v1.h | AEA2D9515D3772C86C920661C7CEE7DFA223045D831E19B2FDB5707A71C52CD2 |
| pending-operation-journal-v1.c | 6A0C49ACE1CF91AD3C2F36D66079A8F7F08CF3EC1055907D6584A472FC18437C |
| generation-ledger-v2.h | 73B2E3FAD97891DE6F5AA1A6CAF86B872F39CB65BC08E8D9287C7A07D1323CBD |
| generation-ledger-v2.c | 377F37011403902DA41BB0969CAB02695F21A06B0076E77C4126E5D44F866945 |
| process-supervisor-v2.h | 9076074A777E8574884CE32F965F144B754033386BA26A4CBE519A362C08EA70 |
| process-supervisor-v2.c | 3C2AA35BDEE49CD499D9073FF8253A07CD925F8FA0347536D31611F5C07177C7 |
| raya-durable-runtime-v1.h | 6799F895EB70E74D3A830B044B6EB8BE4FD1BBFC9CDFEADB92B9C59445CD8C4E |
| raya-durable-runtime-v1.c | 9BB9571D5D24601A0E3CE63C155C7228966F1E5125ABE29068AD99B82DCB64FF |
| raya-durable-wal-v1.h | CFA50B740F67013171B3199F769F331A5AFECBD95A86A66D77185AABEA2B6DF9 |
| raya-durable-wal-v1.c | E1BD46915B859751DAB20B1E62352A39C72C105B75979DB0DE9CCC3B007159E5 |
| windows-pending-slot-v1.c | 0D45C3EC6E4E2DE4E9E47203D19BC9E09C3C44D4EE84652A5892232E65D74249 |
| pending-slot-store-model-v1.c | 65AC3F3AA5D23B8D83D1CD2668A476D1104C59AC98614BA0BF55F79FFE0ECD4D |
