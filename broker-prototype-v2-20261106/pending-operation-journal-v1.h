#ifndef RAYA_PENDING_OPERATION_JOURNAL_V1_H
#define RAYA_PENDING_OPERATION_JOURNAL_V1_H
#include <stdint.h>
#define RAYA_PENDING_RECORD_SIZE 128u
typedef enum {
 RAYA_PENDING_EMPTY=0, RAYA_PENDING_PREPARED=1,
 RAYA_PENDING_WITNESS_COMMITTED=2, RAYA_PENDING_LOCAL_COMMITTED=3,
 RAYA_PENDING_ABORTED=4, RAYA_PENDING_DEGRADED=5,
 RAYA_PENDING_RECOVERY_REQUIRED=6, RAYA_PENDING_TOMBSTONED=7
} raya_pending_state;
typedef enum {
 RAYA_RECOVERY_READY=0, RAYA_RECOVERY_RETRY_EXACT=1,
 RAYA_RECOVERY_COMMIT_LOCAL=2, RAYA_RECOVERY_FINALIZE=3,
 RAYA_RECOVERY_ROLLBACK_SUSPECTED=4, RAYA_RECOVERY_REQUIRED=5,
 RAYA_RECOVERY_SECURITY_FAILURE=6, RAYA_RECOVERY_MARK_LOCAL_COMMITTED=7,
 RAYA_RECOVERY_MARK_WITNESS_COMMITTED=8
} raya_pending_recovery;
typedef enum {
 RAYA_WITNESS_COMMITTED=1, RAYA_WITNESS_NOT_FOUND=2,
 RAYA_WITNESS_UNKNOWN=3, RAYA_WITNESS_CONFLICT=4
} raya_witness_status;
typedef struct {
 raya_pending_state state;
 uint8_t record[RAYA_PENDING_RECORD_SIZE];
} raya_pending_journal;
typedef struct {
 int (*write_record)(void *context, const uint8_t record[RAYA_PENDING_RECORD_SIZE]);
 int (*flush)(void *context);
 int (*readback)(void *context, uint8_t record[RAYA_PENDING_RECORD_SIZE]);
 void *context;
} raya_pending_store_ops;
int raya_pending_record_encode(const uint8_t namespace_id[16], const uint8_t operation_id[16],
 uint64_t expected_sequence, const uint8_t request_digest[32],
 raya_pending_state state, uint64_t witness_sequence, uint8_t out[RAYA_PENDING_RECORD_SIZE]);
int raya_pending_record_decode(const uint8_t in[RAYA_PENDING_RECORD_SIZE],
 raya_pending_state *state, uint64_t *expected_sequence, uint64_t *next_sequence,
 uint64_t *witness_sequence);
void raya_pending_init(raya_pending_journal *journal);
int raya_pending_load(raya_pending_journal *journal, const uint8_t record[RAYA_PENDING_RECORD_SIZE]);
int raya_pending_prepare(raya_pending_journal *journal, const uint8_t namespace_id[16],
 const uint8_t operation_id[16], uint64_t expected_sequence, const uint8_t request_digest[32]);
int raya_pending_mark_witness_committed(raya_pending_journal *journal,
 const uint8_t namespace_id[16], const uint8_t operation_id[16],
 const uint8_t request_digest[32], uint64_t committed_sequence);
int raya_pending_mark_local_committed(raya_pending_journal *journal,
 const uint8_t operation_id[16], const uint8_t request_digest[32], uint64_t local_sequence);
/* In-memory compatibility helper only; never use as durable finalization. */
int raya_pending_finalize(raya_pending_journal *journal, const uint8_t operation_id[16]);
int raya_pending_store_prepare(raya_pending_journal *journal, const raya_pending_store_ops *store,
 const uint8_t namespace_id[16], const uint8_t operation_id[16],
 uint64_t expected_sequence, const uint8_t request_digest[32]);
int raya_pending_store_mark_witness_committed(raya_pending_journal *journal, const raya_pending_store_ops *store,
 const uint8_t namespace_id[16], const uint8_t operation_id[16],
 const uint8_t request_digest[32], uint64_t committed_sequence);
int raya_pending_store_mark_local_committed(raya_pending_journal *journal, const raya_pending_store_ops *store,
 const uint8_t operation_id[16], const uint8_t request_digest[32], uint64_t local_sequence);
int raya_pending_store_finalize(raya_pending_journal *journal, const raya_pending_store_ops *store,
 const uint8_t operation_id[16]);
raya_pending_recovery raya_pending_classify(const raya_pending_journal *journal,
 raya_witness_status witness_status, uint64_t witness_sequence, uint64_t local_epoch,
 int exact_operation_match);
#endif
