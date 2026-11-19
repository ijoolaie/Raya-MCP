#ifndef RAYA_WINDOWS_PENDING_SLOT_V1_H
#define RAYA_WINDOWS_PENDING_SLOT_V1_H
#include <stdint.h>
#include "pending-operation-journal-v1.h"
#define RAYA_PENDING_SLOT_SIZE 256u
#define RAYA_PENDING_SLOT_COUNT 2u
typedef enum {
 RAYA_SLOT_SELECT_ERROR=0,
 RAYA_SLOT_SELECT_UNPROVISIONED=1,
 RAYA_SLOT_SELECT_VALID=2
} raya_slot_select_result;
int raya_pending_slot_encode(const uint8_t record[RAYA_PENDING_RECORD_SIZE], uint64_t revision, uint8_t out[RAYA_PENDING_SLOT_SIZE]);
int raya_pending_slot_decode(const uint8_t slot[RAYA_PENDING_SLOT_SIZE], uint64_t *revision, raya_pending_state *state);
raya_slot_select_result raya_pending_slots_select(const uint8_t slots[2][RAYA_PENDING_SLOT_SIZE], unsigned *selected_index, uint64_t *revision, raya_pending_state *state);
#endif
