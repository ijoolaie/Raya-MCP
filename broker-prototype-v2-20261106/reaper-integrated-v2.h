#ifndef RAYA_REAPER_INTEGRATED_V2_H
#define RAYA_REAPER_INTEGRATED_V2_H
#include "reaper-core-v2.h"
#include "reaper-registry-v2.h"
#define RAYA_REAPER_INTEGRATED_CAPACITY RAYA_REAPER_REGISTRY_CAPACITY
typedef enum {
 RAYA_INTEGRATED_RESERVATION_NONE=0,
 RAYA_INTEGRATED_RESERVATION_HELD=1,
 RAYA_INTEGRATED_RESERVATION_HANDED_OFF=2,
 RAYA_INTEGRATED_RESERVATION_RELEASED=3,
 RAYA_INTEGRATED_RESERVATION_TERMINAL_HELD=4
} raya_integrated_reservation_state;
typedef enum {
 RAYA_INTEGRATED_COLLECT_PENDING=0,
 RAYA_INTEGRATED_COLLECTING=1,
 RAYA_INTEGRATED_UNCERTAIN=2
} raya_integrated_collect_state;
typedef enum {
 RAYA_INTEGRATED_TERMINAL_SUCCESS=1,
 RAYA_INTEGRATED_TERMINAL_ABORTED=2,
 RAYA_INTEGRATED_INCOMPLETE=3,
 RAYA_INTEGRATED_UNCERTAIN_RESULT=4
} raya_integrated_collect_result;
typedef struct {
 raya_reaper_op base;
 HANDLE completion_event;
 raya_integrated_collect_state collect_state;
 raya_integrated_reservation_state reservation_state;
} raya_integrated_op;
typedef struct {
 raya_reaper_core core;
 raya_reaper_registry registry;
 size_t reserved_slots;
} raya_reaper_integrated;
typedef enum {
 RAYA_INTEGRATED_OK=1,
 RAYA_INTEGRATED_EMPTY=0,
 RAYA_INTEGRATED_INVALID=-1,
 RAYA_INTEGRATED_DEGRADED=-2,
 RAYA_INTEGRATED_FULL=-3
} raya_integrated_result;
/* Every function requires the one external lock shared by caller and worker. */
int raya_reaper_integrated_init(raya_reaper_integrated *state);
int raya_reaper_integrated_admit(raya_reaper_integrated *state);
/* Reservation is attached to one op; there is deliberately no anonymous unreserve. */
int raya_reaper_integrated_reserve(raya_reaper_integrated *state,
                                   raya_integrated_op *op);
/* Caller-local terminal path: terminalize, close resources, then unreserve. */
int raya_reaper_integrated_caller_terminal(raya_reaper_integrated *state,
                                   raya_integrated_op *op);
int raya_reaper_integrated_unreserve(raya_reaper_integrated *state,
                                   raya_integrated_op *op);
int raya_reaper_integrated_handoff(raya_reaper_integrated *state,
                                   raya_integrated_op *op);
raya_integrated_result raya_reaper_integrated_promote_one(
                                   raya_reaper_integrated *state,
                                   raya_integrated_op **out_op);
int raya_reaper_integrated_claim(raya_reaper_integrated *state,
                                 raya_integrated_op *op, HANDLE event_handle);
raya_integrated_result raya_reaper_integrated_collect(
                                 raya_reaper_integrated *state,
                                 raya_integrated_op *op,
                                 raya_integrated_collect_result result);
/* Staged terminal path: close operation resources outside lock, then commit release. */
raya_integrated_result raya_reaper_integrated_terminal_hold(
                                 raya_reaper_integrated *state,
                                 raya_integrated_op *op,
                                 raya_integrated_collect_result result);
int raya_reaper_integrated_release(
                                 raya_reaper_integrated *state,
                                 raya_integrated_op *op);
int raya_reaper_integrated_shutdown(raya_reaper_integrated *state);
int raya_reaper_integrated_can_exit(const raya_reaper_integrated *state);
#endif
