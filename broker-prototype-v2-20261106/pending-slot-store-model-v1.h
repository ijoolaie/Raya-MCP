#ifndef RAYA_PENDING_SLOT_STORE_MODEL_V1_H
#define RAYA_PENDING_SLOT_STORE_MODEL_V1_H
#include <stdint.h>
#include "windows-pending-slot-v1.h"
typedef enum {
 RAYA_MODEL_FAULT_NONE=0,
 RAYA_MODEL_FAULT_WRITE_PARTIAL=1,
 RAYA_MODEL_FAULT_WRITE_FAIL=2,
 RAYA_MODEL_FAULT_FLUSH_FAIL_NOT_DURABLE=3,
 RAYA_MODEL_FAULT_FLUSH_FAIL_DURABLE=4,
 RAYA_MODEL_FAULT_READBACK_FAIL=5,
 RAYA_MODEL_FAULT_READBACK_MISMATCH=6,
 RAYA_MODEL_FAULT_WRITE_PARTIAL_DURABLE=7
} raya_model_fault;
typedef struct {
 uint8_t volatile_slots[2][RAYA_PENDING_SLOT_SIZE];
 uint8_t durable_slots[2][RAYA_PENDING_SLOT_SIZE];
 raya_model_fault fault;
 unsigned partial_bytes;
 unsigned writes,flushes,reads;
} raya_pending_store_model;
typedef enum { RAYA_MODEL_COMMIT_OK=1,RAYA_MODEL_COMMIT_UNCERTAIN=2,RAYA_MODEL_COMMIT_INVALID=3 } raya_model_commit_result;
void raya_pending_model_init(raya_pending_store_model *model);
int raya_pending_model_seed(raya_pending_store_model *model,unsigned slot,const uint8_t bytes[RAYA_PENDING_SLOT_SIZE]);
int raya_pending_model_write(void *context,unsigned slot,const uint8_t bytes[RAYA_PENDING_SLOT_SIZE]);
int raya_pending_model_flush(void *context);
int raya_pending_model_readback(void *context,unsigned slot,uint8_t bytes[RAYA_PENDING_SLOT_SIZE]);
void raya_pending_model_power_loss(raya_pending_store_model *model);
raya_model_commit_result raya_pending_model_commit(raya_pending_store_model *model,const uint8_t record[RAYA_PENDING_RECORD_SIZE],unsigned *published_slot,uint64_t *published_revision);
raya_slot_select_result raya_pending_model_recover(const raya_pending_store_model *model,unsigned *slot,uint64_t *revision,raya_pending_state *state);
#endif
