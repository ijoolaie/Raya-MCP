# PRODUCT ENTRYPOINT AND INTEGRATION-BOUNDARY REVIEW
Date: 2026-11-17
Scope: Working Copy root structure, README/docs inventory, Adapter public contract and startup implementation
Status: READ-ONLY SOURCE/DOCUMENT REVIEW

## 1. Executive result

The Working Copy currently presents as a collection of C implementation units, API headers, focused test/probe programs, validation reports, and archived TLC evidence—not as a discoverable application/product build.

Observed facts:
- Root has no CMakeLists.txt, Visual Studio solution/project, Makefile, package manifest, Cargo manifest, or other conventional product build manifest.
- The only root README is 727 bytes and describes one isolated pipe-I/O resource-finalizer primitive. It explicitly says it is not wired into the Worker or pipe-io-v2.c and that Production remains NO-GO.
- docs/ contains only TLC archive inventory/manifests.
- Root-level C/C++ files matching the non-test source filter were not found; implementation units are present but are assembled as a set of separately named source files and focused tests.
- The recursive call-site audit found no non-test caller for Adapter submit, managed-pipe submit, or legacy pipe read/write wrappers.

Conclusion: there is no source-backed basis in this Working Copy to nominate an application/business entrypoint or claim that the Adapter is integrated into a product. This is an integration-surface gap, not proof that no external consumer exists outside this repository.

## 2. Repository evidence

### Root/document inventory
- README.md: 727 bytes; one entry describing the 2026-11-08 isolated resource-finalizer primitive.
- docs/: three TLC archive inventory/manifest files only.
- validation/: accumulated implementation, lifecycle, WAL, and API review reports.
- Numerous root-level evidence/state directories are TLC metadata archives, not application modules.

### Build discovery
No root-level conventional build manifest was found for:
- CMake
- Visual Studio solution/project
- Make
- package.json
- Cargo

This review did not inspect every possible external build script or user-local IDE configuration. The finding is limited to the root manifest patterns searched.

## 3. Adapter startup contract (source-backed)

Header: integrated-submit-adapter-v2.h

- Worker is borrowed and must outlive Adapter.
- Submitter storage is caller-supplied but must be uninitialized; Adapter initializes it during start.
- Adapter Destroy does not stop/join/destroy Submitter.
- After Adapter destruction, caller must quiesce and tear Submitter down before reusing storage.
- Submit is asynchronous; no timeout/cancellation API in this version.
- WRITE input is copied; READ buffer is valid only during completion callback.
- Destroy closes admission; caller must serialize Destroy calls, keep Adapter storage alive, quiesce all callers, and prevent new calls after successful destruction.

Implementation: integrated-submit-adapter-v2.c
- Start rejects null/uninitialized prerequisites, requires Worker initialized and started, requires Submitter not already initialized, and validates queue capacity.
- Adapter initializes its own lock and stores borrowed Worker and caller-supplied Submitter.
- Adapter then initializes/starts Submitter with Adapter-owned Issue/Publish/not-issued callbacks.
- Adapter startup failure destroys only the Adapter lock/storage initialization; the Worker remains caller-owned.

Implication:
The caller must already have a running Worker and must own its lifetime. It must allocate and preserve Submitter and Adapter storage. It must also own the external pipe handles, callback context, and eventual teardown coordination. These are substantial product integration responsibilities, not a self-contained public service boundary.

## 4. Missing integration contract

No production caller was found to answer these questions:
1. Who creates/configures the Worker and pipe endpoints?
2. Which thread or component owns the issuing thread and keeps it alive through overlapped completion?
3. Who owns the Adapter, Submitter, Worker, pipe handles, and callback contexts?
4. Where are READ/WRITE requests sourced from, and what higher-level framing/protocol applies?
5. What does a successful local callback mean to the application?
6. Who observes and reconciles Adapter RETAINED envelopes?
7. What is the shutdown order when Adapter has retained work or Worker is degraded?
8. Where does an operation ID originate and how does it reach callbacks and any durable record?
9. Is there a process/service entrypoint or external host not present in this Working Copy?

The current test programs answer selected component-level questions, but not these application ownership and business semantics.

## 5. Recommended integration decision

Do not implement a speculative product caller or invent a protocol in this slice.

Before wiring the Adapter into a product, the owner should identify:
- the actual host/service executable and build system;
- the real pipe producer/consumer and protocol/framing contract;
- the component that owns Worker startup and lifetime;
- the single supported public submission surface (Adapter versus direct managed-pipe API);
- callback-context lifetime and reentrancy rules;
- retained-operation observability and reconciliation owner;
- terminal semantics required by the application (local transport versus recipient commit).

Once those are specified, add an integration harness that resembles the actual host and tests the real lifecycle. Do not use a loopback test as evidence of recipient application commit.

## 6. Scope and safety

- Read-only review of root directory inventory, README, docs inventory, Adapter header, and Adapter startup implementation.
- No source/header/test/config/README modifications.
- No build or test run.
- No process termination, install, download, or temp artifacts.
- Production unchanged.
- The conclusion is limited to the Working Copy; external consumers/build systems may exist elsewhere.

---
End of review.
