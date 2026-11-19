#ifndef RAYA_DURABLE_OWNER_V1_H
#define RAYA_DURABLE_OWNER_V1_H
#include "raya-durable-contract-v1.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RAYA_DURABLE_OWNER_LEASE_CAPACITY_V1 32u
typedef enum { RAYA_DURABLE_OWNER_CREATED_V1=1, RAYA_DURABLE_OWNER_ACTIVE_V1=2, RAYA_DURABLE_OWNER_CLOSING_V1=3, RAYA_DURABLE_OWNER_QUARANTINED_V1=4, RAYA_DURABLE_OWNER_CLOSED_V1=5 } raya_durable_owner_state_v1;
typedef enum { RAYA_DURABLE_OWNER_OK_V1=0, RAYA_DURABLE_OWNER_INVALID_V1=1, RAYA_DURABLE_OWNER_BUSY_V1=2, RAYA_DURABLE_OWNER_CAPACITY_V1=3, RAYA_DURABLE_OWNER_QUARANTINED_RESULT_V1=4 } raya_durable_owner_status_v1;
typedef enum { RAYA_DURABLE_CALLBACK_KNOWN_V1=1, RAYA_DURABLE_CALLBACK_UNKNOWN_V1=2 } raya_durable_callback_outcome_v1;
typedef enum { RAYA_DURABLE_LEASE_FREE_V1=0, RAYA_DURABLE_LEASE_ACTIVE_V1=1, RAYA_DURABLE_LEASE_UNKNOWN_V1=2 } raya_durable_lease_state_v1;
typedef struct { ULONGLONG id; DWORD state; } raya_durable_owner_lease_record_v1;
/* Caller must hold one external exclusive lock and serialize every operation. This is a deterministic state model, not a thread-safe runtime. It invokes no callbacks and owns no callback table/context. Caller retains both until CLOSED and all external users have stopped. */
typedef struct { DWORD struct_size; DWORD schema_version; raya_durable_owner_state_v1 state; DWORD close_requested; DWORD in_flight; DWORD unresolved_unknown; ULONGLONG next_lease_id; DWORD initialized; DWORD reserved[4]; const raya_durable_callbacks_v1 *callbacks; raya_durable_owner_lease_record_v1 leases[RAYA_DURABLE_OWNER_LEASE_CAPACITY_V1]; } raya_durable_owner_v1;
raya_durable_owner_status_v1 raya_durable_owner_init_v1(raya_durable_owner_v1 *, const raya_durable_callbacks_v1 *);
raya_durable_owner_status_v1 raya_durable_owner_activate_v1(raya_durable_owner_v1 *);
raya_durable_owner_status_v1 raya_durable_owner_admit_v1(raya_durable_owner_v1 *, ULONGLONG *);
raya_durable_owner_status_v1 raya_durable_owner_complete_v1(raya_durable_owner_v1 *, ULONGLONG, raya_durable_callback_outcome_v1);
raya_durable_owner_status_v1 raya_durable_owner_begin_close_v1(raya_durable_owner_v1 *);
raya_durable_owner_status_v1 raya_durable_owner_reconcile_v1(raya_durable_owner_v1 *, ULONGLONG, int);
raya_durable_owner_status_v1 raya_durable_owner_finish_close_v1(raya_durable_owner_v1 *);
#ifdef __cplusplus
}
#endif
#endif
