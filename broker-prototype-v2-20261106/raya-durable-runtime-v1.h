#ifndef RAYA_DURABLE_RUNTIME_V1_H
#define RAYA_DURABLE_RUNTIME_V1_H
#include "raya-durable-owner-v1.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RAYA_DURABLE_RUNTIME_MAGIC_V1 0x44525631u
typedef struct {
 DWORD magic;
 DWORD struct_size;
 DWORD initialized;
 DWORD reserved[3];
 CRITICAL_SECTION state_lock;
 CRITICAL_SECTION callback_lock;
 raya_durable_owner_v1 owner;
 const raya_durable_callbacks_v1 *callbacks;
} raya_durable_runtime_v1;
/* Initialize only fresh caller-owned storage; do not initialize an already-live
 * runtime. The caller must keep runtime storage, callback table, and callback
 * context alive until destroy succeeds. Do not call teardown from a callback.
 * External API callers must be quiesced before destroy. Callbacks are serialized
 * globally. No timeout, cancellation, durable journal, or exception isolation
 * exists; this wrapper does not itself persist EFFECT_BEGIN or EFFECT_RESULT. */
raya_durable_owner_status_v1 raya_durable_runtime_init_v1(
 raya_durable_runtime_v1 *, const raya_durable_config_v1 *,
 const raya_durable_callbacks_v1 *);
raya_durable_owner_status_v1 raya_durable_runtime_invoke_issue_v1(
 raya_durable_runtime_v1 *, const raya_durable_operation_id_v1 *,
 raya_durable_issue_result_v1 *, ULONGLONG *);
raya_durable_owner_status_v1 raya_durable_runtime_invoke_handoff_v1(
 raya_durable_runtime_v1 *, const raya_durable_operation_id_v1 *,
 raya_durable_handoff_result_v1 *, ULONGLONG *);
raya_durable_owner_status_v1 raya_durable_runtime_begin_close_v1(
 raya_durable_runtime_v1 *);
raya_durable_owner_status_v1 raya_durable_runtime_reconcile_v1(
 raya_durable_runtime_v1 *, ULONGLONG, int);
raya_durable_owner_status_v1 raya_durable_runtime_finish_close_v1(
 raya_durable_runtime_v1 *);
/* Requires CLOSED and caller-proven external quiescence. */
raya_durable_owner_status_v1 raya_durable_runtime_destroy_v1(
 raya_durable_runtime_v1 *);
#ifdef __cplusplus
}
#endif
#endif
