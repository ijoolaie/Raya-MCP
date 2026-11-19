#ifndef RAYA_ORCHESTRATOR_V1_H
#define RAYA_ORCHESTRATOR_V1_H
#include "raya-orchestrator-registry-v1.h"
#include "integrated-submit-adapter-v2.h"

#define RAYA_ORCHESTRATOR_CONTEXT_KIND 0x524F5231u
#define RAYA_ORCHESTRATOR_CONTEXT_MAGIC 0x524159414F524331ull
#define RAYA_ORCHESTRATOR_CLEANUP_STEP_ADAPTER_DESTROY 0u
#define RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_ADMISSION_CLOSE 1u
#define RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_SHUTDOWN 2u
#define RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_JOIN 3u
#define RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_DESTROY 4u
#define RAYA_ORCHESTRATOR_CLEANUP_STEP_WORKER_SHUTDOWN 5u
#define RAYA_ORCHESTRATOR_CLEANUP_STEP_WORKER_JOIN 6u
#define RAYA_ORCHESTRATOR_CLEANUP_STEP_WORKER_DESTROY 7u

typedef enum {
    RAYA_ORCH_WORKER_BORROWED=1,
    RAYA_ORCH_WORKER_OWNED=2
} raya_orch_worker_ownership;

typedef struct raya_orchestrator_context {
    uint64_t magic;
    raya_orch_registry *registry;
    raya_orch_handle handle;
    raya_integrated_submit_adapter *adapter;
    raya_dedicated_submitter *submitter;
    raya_managed_worker *worker;
    raya_orch_worker_ownership worker_ownership;
    BOOL registered;
} raya_orchestrator_context;

/* Call init on fresh storage before registration. Context storage is caller-owned, must not be copied/moved after registration,
   and must outlive registry reclamation. Components are borrowed by this layer:
   Adapter initializes Submitter; Adapter borrows Worker. Worker ownership here
   records teardown policy only and does not transfer/free storage. */
typedef struct {
    raya_orch_lease registry_lease;
    raya_orchestrator_context *context;
    BOOL active;
} raya_orchestrator_lease;

void raya_orchestrator_context_init(raya_orchestrator_context *context);
raya_orch_status raya_orchestrator_context_register(
    raya_orch_registry *registry,
    raya_orchestrator_context *context,
    raya_integrated_submit_adapter *adapter,
    raya_dedicated_submitter *submitter,
    raya_managed_worker *worker,
    raya_orch_worker_ownership worker_ownership,
    raya_orch_handle *handle_out);

/* The returned context/components are valid only while this lease is active.
   No component call is made while the Registry lock is held. */
raya_orch_status raya_orchestrator_acquire(
    raya_orch_registry *registry, raya_orch_handle handle,
    raya_orchestrator_lease *lease_out);
raya_orch_status raya_orchestrator_release(raya_orchestrator_lease *lease);
/* Cleanup-owner-only retrieval after BeginClose + lease drain. The caller must
   serialize cleanup-owner work and retain ownership until all Context use ends. */
raya_orch_status raya_orchestrator_owner_get_context(
    raya_orch_registry *registry, raya_orch_handle handle,
    raya_orch_owner_token owner, raya_orchestrator_context **context_out);
/* Context reclamation is policy-gated: steps 0-4 must succeed for all
   contexts; OWNED Worker additionally requires steps 5-7. */
raya_orch_status raya_orchestrator_complete_close_owned(
    raya_orch_registry *registry, raya_orch_handle handle,
    raya_orch_owner_token owner);
/* Executes only Adapter Destroy under the exclusive cleanup owner. BUSY/INVALID
   is recorded BLOCKED+UNCERTAIN: caller must reconcile the cursor explicitly
   from fresh drain evidence before another attempt. */
raya_orch_status raya_orchestrator_destroy_adapter_owned(
    raya_orch_registry *registry, raya_orch_handle handle,
    raya_orch_owner_token owner, raya_adapter_destroy_result *result_out);
/* Requires committed Adapter Destroy. Closes Submitter operational API admission;
   BUSY means the gate is closed but existing entrants still need to drain. */
raya_orch_status raya_orchestrator_close_submitter_admission_owned(
    raya_orch_registry *registry, raya_orch_handle handle,
    raya_orch_owner_token owner, LONG *active_count_out);
/* Lifecycle operations require one serialized cleanup owner and a committed
   predecessor step. result_out preserves component effect/status details.
   Join ABNORMAL_EXIT is a committed handle-reap effect, not thread success.
   Destroy PARTIAL is retryable only because Submitter tracks its close cursor. */
raya_orch_status raya_orchestrator_submitter_shutdown_owned(
    raya_orch_registry *registry, raya_orch_handle handle,
    raya_orch_owner_token owner, raya_submitter_op_result *result_out);
raya_orch_status raya_orchestrator_submitter_join_owned(
    raya_orch_registry *registry, raya_orch_handle handle,
    raya_orch_owner_token owner, DWORD timeout_ms,
    raya_submitter_op_result *result_out);
raya_orch_status raya_orchestrator_submitter_destroy_owned(
    raya_orch_registry *registry, raya_orch_handle handle,
    raya_orch_owner_token owner, raya_submitter_op_result *result_out);
/* Worker teardown is available only for OWNED Workers and follows successful
   Adapter/Submitter cleanup. Join must report normal thread exit before Destroy. */
raya_orch_status raya_orchestrator_worker_shutdown_owned(
    raya_orch_registry *registry, raya_orch_handle handle,
    raya_orch_owner_token owner, raya_worker_op_result *result_out);
raya_orch_status raya_orchestrator_worker_join_owned(
    raya_orch_registry *registry, raya_orch_handle handle,
    raya_orch_owner_token owner, DWORD timeout_ms,
    raya_worker_op_result *result_out);
raya_orch_status raya_orchestrator_worker_destroy_owned(
    raya_orch_registry *registry, raya_orch_handle handle,
    raya_orch_owner_token owner, raya_worker_op_result *result_out);

raya_integrated_submit_adapter *raya_orchestrator_lease_adapter(
    const raya_orchestrator_lease *lease);
raya_dedicated_submitter *raya_orchestrator_lease_submitter(
    const raya_orchestrator_lease *lease);
raya_managed_worker *raya_orchestrator_lease_worker(
    const raya_orchestrator_lease *lease);
raya_orch_worker_ownership raya_orchestrator_lease_worker_ownership(
    const raya_orchestrator_lease *lease);

#endif
