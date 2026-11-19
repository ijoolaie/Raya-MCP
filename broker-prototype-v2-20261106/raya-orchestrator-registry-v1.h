#ifndef RAYA_ORCHESTRATOR_REGISTRY_V1_H
#define RAYA_ORCHESTRATOR_REGISTRY_V1_H
#include <windows.h>
#include <stdint.h>

#define RAYA_ORCH_REGISTRY_CAPACITY 64u
#define RAYA_ORCH_LEASE_CAPACITY 256u
#define RAYA_ORCH_CLEANUP_STEPS 8u

typedef uint64_t raya_orch_handle;
typedef uint64_t raya_orch_owner_token;

typedef enum {
    RAYA_ORCH_SLOT_EMPTY=0, RAYA_ORCH_SLOT_OPEN=1,
    RAYA_ORCH_SLOT_CLOSING=2, RAYA_ORCH_SLOT_RETIRED=3
} raya_orch_slot_state;

typedef enum {
    RAYA_ORCH_CLOSE_POLICY_GENERIC=0,
    RAYA_ORCH_CLOSE_POLICY_REQUIRED_STEPS=1
} raya_orch_close_policy;
/* PARTIAL is a known, component-tracked effect that permits same-operation
   retry only when outcome is RETRYABLE_FAILURE; it blocks owner release. */
typedef enum {
    RAYA_ORCH_EFFECT_NOT_ATTEMPTED=0, RAYA_ORCH_EFFECT_IN_PROGRESS=1,
    RAYA_ORCH_EFFECT_COMMITTED=2, RAYA_ORCH_EFFECT_UNKNOWN=3,
    RAYA_ORCH_EFFECT_BLOCKED=4, RAYA_ORCH_EFFECT_PARTIAL=5
} raya_orch_effect;
typedef enum {
    RAYA_ORCH_OUTCOME_NOT_EVALUATED=0, RAYA_ORCH_OUTCOME_SUCCESS=1,
    RAYA_ORCH_OUTCOME_ABNORMAL_EXIT=2, RAYA_ORCH_OUTCOME_RETRYABLE_FAILURE=3,
    RAYA_ORCH_OUTCOME_TERMINAL_FAILURE=4, RAYA_ORCH_OUTCOME_UNCERTAIN=5
} raya_orch_outcome;
typedef enum {
    RAYA_ORCH_OK=1, RAYA_ORCH_INVALID=0, RAYA_ORCH_BUSY=-1,
    RAYA_ORCH_TIMEOUT=-2, RAYA_ORCH_CAPACITY=-3
} raya_orch_status;

typedef struct {
    raya_orch_effect effect;
    raya_orch_outcome outcome;
    uint32_t attempts;
    DWORD last_error;
    DWORD exit_code;
} raya_orch_cleanup_step;

typedef struct {
    raya_orch_handle handle;
    void *object;
    void *registry_private;
    uint64_t lease_id;
    BOOL active;
} raya_orch_lease;

typedef struct {
    void *object;
    uint32_t generation;
    uint32_t lease_count;
    uint32_t object_kind;
    raya_orch_slot_state state;
    raya_orch_close_policy close_policy;
    uint32_t required_success_mask;
    raya_orch_owner_token owner;
    raya_orch_cleanup_step steps[RAYA_ORCH_CLEANUP_STEPS];
} raya_orch_registry_slot;

typedef struct {
    uint64_t id;
    raya_orch_handle handle;
    void *object;
    BOOL active;
} raya_orch_lease_record;

typedef struct {
    SRWLOCK lock;
    CONDITION_VARIABLE drained;
    raya_orch_registry_slot slots[RAYA_ORCH_REGISTRY_CAPACITY];
    raya_orch_owner_token next_owner;
    uint64_t next_lease_id;
    raya_orch_lease_record leases[RAYA_ORCH_LEASE_CAPACITY];
    BOOL initialized;
} raya_orch_registry;

/* Registry storage must outlive every handle, lease, and cleanup operation.
   Initialize once; do not copy or relocate an initialized registry.
   Lease records are bounded globally per registry; lease values are move-only:
   copying one does not create another releasable lease identity. */
int raya_orch_registry_init(raya_orch_registry *registry);
raya_orch_status raya_orch_register(raya_orch_registry *registry,
    void *object, uint32_t object_kind, raya_orch_handle *handle_out);
/* Strict close policy and immutable required-success mask are published
   atomically with the object. GENERIC requires mask zero. */
raya_orch_status raya_orch_register_with_policy(
    raya_orch_registry *registry, void *object, uint32_t object_kind,
    raya_orch_close_policy policy, uint32_t required_success_mask,
    raya_orch_handle *handle_out);
raya_orch_status raya_orch_acquire(raya_orch_registry *registry,
    raya_orch_handle handle, uint32_t expected_kind, raya_orch_lease *lease_out);
raya_orch_status raya_orch_release(raya_orch_lease *lease);
/* Verifies the unique lease identity under the Registry lock; copied stale
   lease values cannot continue to expose their former object after release. */
int raya_orch_lease_is_active(const raya_orch_lease *lease);
/* Exactly one caller wins close ownership. New leases are denied immediately.
   If a previous owner explicitly yielded, a retry may claim the CLOSING slot. */
raya_orch_status raya_orch_begin_close(raya_orch_registry *registry,
    raya_orch_handle handle, raya_orch_owner_token *owner_out);
/* Owner may yield only when no step is in progress or awaiting reconciliation. */
raya_orch_status raya_orch_release_cleanup_owner(raya_orch_registry *registry,
    raya_orch_handle handle, raya_orch_owner_token owner);
/* Explicit evidence-based resolution of an uncertain/blocked step. */
raya_orch_status raya_orch_cursor_reconcile(raya_orch_registry *registry,
    raya_orch_handle handle, raya_orch_owner_token owner, uint32_t step,
    raya_orch_effect resolved_effect, raya_orch_outcome resolved_outcome,
    DWORD last_error, DWORD exit_code);
/* Returns the registered object only to the exact Cleanup Owner after admission
   is closed and every API lease has drained. Pointer is used after unlocking;
   caller must retain exclusive owner token until it finishes using the object. */
raya_orch_status raya_orch_owner_get_object(raya_orch_registry *registry,
    raya_orch_handle handle, raya_orch_owner_token owner,
    uint32_t expected_kind, void **object_out);
raya_orch_status raya_orch_wait_drained(raya_orch_registry *registry,
    raya_orch_handle handle, raya_orch_owner_token owner, DWORD timeout_ms);
raya_orch_status raya_orch_cursor_update(raya_orch_registry *registry,
    raya_orch_handle handle, raya_orch_owner_token owner, uint32_t step,
    raya_orch_effect effect, raya_orch_outcome outcome,
    DWORD last_error, DWORD exit_code);
raya_orch_status raya_orch_cursor_read(raya_orch_registry *registry,
    raya_orch_handle handle, uint32_t step, raya_orch_cleanup_step *out);
/* Reclaims the slot only after all leases drain; caller frees object separately. */
raya_orch_status raya_orch_complete_close(raya_orch_registry *registry,
    raya_orch_handle handle, raya_orch_owner_token owner);
/* Reclaims only a REQUIRED_STEPS slot whose immutable registered mask is
   satisfied exactly. expected_kind is checked atomically with reclamation. */
raya_orch_status raya_orch_complete_close_required(
    raya_orch_registry *registry, raya_orch_handle handle,
    raya_orch_owner_token owner, uint32_t expected_kind);

#endif
