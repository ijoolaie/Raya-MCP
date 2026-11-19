#ifndef RAYA_DURABLE_WAL_V1_H
#define RAYA_DURABLE_WAL_V1_H
#include <windows.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RAYA_WAL_V1_RECORD_BYTES 192u
#define RAYA_WAL_V1_MAX_RECORDS 4096u
#define RAYA_WAL_V1_MAX_OPERATIONS 128u
#define RAYA_WAL_V1_FORMAT 1u
typedef enum { RAYA_WAL_HEADER=1, RAYA_WAL_ID_RESERVE=2, RAYA_WAL_REQUEST_ACCEPTED=3, RAYA_WAL_EFFECT_BEGIN=4, RAYA_WAL_EFFECT_RESULT=5, RAYA_WAL_OPERATION_TERMINAL=6, RAYA_WAL_RECONCILIATION=7 } raya_wal_record_type_v1;
typedef enum { RAYA_WAL_EFFECT_ISSUE=1, RAYA_WAL_EFFECT_HANDOFF=2 } raya_wal_effect_v1;
typedef enum { RAYA_WAL_RESULT_COMMITTED=1, RAYA_WAL_RESULT_PROVEN_NOT_COMMITTED=2, RAYA_WAL_RESULT_UNKNOWN=3 } raya_wal_disposition_v1;
typedef enum { RAYA_WAL_EMPTY=0, RAYA_WAL_VALID_COMPLETE=1, RAYA_WAL_INCOMPLETE_FINAL_RECORD=2, RAYA_WAL_CORRUPT=3, RAYA_WAL_LIMIT_EXCEEDED=4, RAYA_WAL_CRYPTO_ERROR=5, RAYA_WAL_INVALID_ARGUMENT=6 } raya_wal_scan_status_v1;
typedef struct {
 DWORD record_type;
 ULONGLONG journal_sequence;
 BYTE name_space[16];
 ULONGLONG operation_sequence;
 DWORD operation_kind;
 DWORD effect_kind;
 DWORD attempt;
 DWORD disposition;
 BYTE payload_digest[32];
 BYTE previous_record_digest[32];
 BYTE metadata_digest[32];
} raya_wal_record_fields_v1;
typedef struct {
 raya_wal_scan_status_v1 status;
 size_t valid_bytes;
 ULONGLONG last_journal_sequence;
 BYTE name_space[16];
 BYTE last_record_digest[32];
 DWORD records_scanned;
 DWORD operations_tracked;
 DWORD unresolved_effects;
} raya_wal_scan_result_v1;
/* Encodes one candidate v1 record into caller-owned bytes. Input fields are
 * validated structurally; chain linkage is checked by the scanner. */
int raya_wal_record_encode_v1(const raya_wal_record_fields_v1 *, BYTE out[RAYA_WAL_V1_RECORD_BYTES]);
/* Pure read-only scanner. Never mutates bytes, repairs, skips, truncates or replays.
 * State is bounded to 128 operations and 4096 records. */
raya_wal_scan_status_v1 raya_wal_scan_v1(const BYTE *bytes, size_t length,
 const BYTE expected_namespace[16], raya_wal_scan_result_v1 *result);
#ifdef __cplusplus
}
#endif
#endif
