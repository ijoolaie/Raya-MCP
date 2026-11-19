#ifndef RAYA_DURABLE_CONTRACT_V1_H
#define RAYA_DURABLE_CONTRACT_V1_H
#include <windows.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RAYA_DURABLE_CONTRACT_SCHEMA_V1 1u
#define RAYA_DURABLE_NAMESPACE_BYTES 16u
#define RAYA_DURABLE_DIGEST_BYTES 32u

typedef enum {
 RAYA_DURABLE_OPERATION_GENERIC=1,
 RAYA_DURABLE_OPERATION_READ=2,
 RAYA_DURABLE_OPERATION_WRITE=3,
 RAYA_DURABLE_OPERATION_MESSAGE=4
} raya_durable_operation_kind_v1;

typedef struct {
 DWORD struct_size;
 DWORD schema_version;
 BYTE name_space[RAYA_DURABLE_NAMESPACE_BYTES];
 ULONGLONG sequence;
 DWORD operation_kind;
 DWORD reserved;
 BYTE payload_digest[RAYA_DURABLE_DIGEST_BYTES];
} raya_durable_operation_id_v1;

typedef enum {
 RAYA_DURABLE_ISSUE_PROVEN_NOT_SUBMITTED=1,
 RAYA_DURABLE_ISSUE_OS_ACCEPTED=2,
 RAYA_DURABLE_ISSUE_UNKNOWN=3
} raya_durable_issue_disposition_v1;

typedef struct {
 DWORD struct_size;
 DWORD schema_version;
 raya_durable_issue_disposition_v1 disposition;
 DWORD native_status;
 DWORD transferred_bytes;
 DWORD reserved;
 raya_durable_operation_id_v1 operation_id_echo;
} raya_durable_issue_result_v1;

typedef enum {
 RAYA_DURABLE_HANDOFF_TRANSFERRED=1,
 RAYA_DURABLE_HANDOFF_NOT_TRANSFERRED=2,
 RAYA_DURABLE_HANDOFF_UNKNOWN=3
} raya_durable_handoff_disposition_v1;

typedef struct {
 DWORD struct_size;
 DWORD schema_version;
 raya_durable_handoff_disposition_v1 disposition;
 DWORD reserved;
 BYTE worker_instance[RAYA_DURABLE_NAMESPACE_BYTES];
 ULONGLONG worker_operation_token;
 raya_durable_operation_id_v1 operation_id_echo;
} raya_durable_handoff_result_v1;

typedef enum {
 RAYA_DURABLE_CAP_IDENTITY=0x00000001u,
 RAYA_DURABLE_CAP_LOCAL_JOURNAL=0x00000002u,
 RAYA_DURABLE_CAP_TYPED_ISSUE=0x00000004u,
 RAYA_DURABLE_CAP_TYPED_HANDOFF=0x00000008u,
 RAYA_DURABLE_CAP_RECIPIENT_IDEMPOTENCY=0x00000010u,
 RAYA_DURABLE_CAP_RECIPIENT_RECEIPT=0x00000020u,
 RAYA_DURABLE_CAP_RECIPIENT_STATUS_QUERY=0x00000040u
} raya_durable_capability_v1;

typedef enum {
 RAYA_DURABLE_GUARANTEE_QUARANTINE_ONLY=1,
 RAYA_DURABLE_GUARANTEE_RECIPIENT_COMMIT=2
} raya_durable_guarantee_v1;

typedef struct {
 DWORD struct_size;
 DWORD schema_version;
 DWORD capabilities;
 DWORD guarantee;
 BYTE name_space[RAYA_DURABLE_NAMESPACE_BYTES];
 DWORD reserved[4];
} raya_durable_config_v1;

/* Versioned callback ABI: callbacks report evidence only; they are never
 * invoked by the pure validators in this module.
 * The owner of this table must keep it and context alive for every invocation,
 * serialize calls unless the provider explicitly guarantees thread safety,
 * and prohibit callback reentrancy into teardown of the owning object.
 * A callback must initialize every result field, including size, schema,
 * disposition, reserved fields, and the exact operation_id_echo. The caller
 * must normalize the result and treat malformed or missing evidence as UNKNOWN.
 * The validator permits NULL context; a concrete runtime may impose stricter
 * policy. No timeout, cancellation, or exception isolation is implied. */
typedef void (WINAPI *raya_durable_issue_callback_v1)(
 void *context, const raya_durable_operation_id_v1 *operation_id,
 raya_durable_issue_result_v1 *result);
typedef void (WINAPI *raya_durable_handoff_callback_v1)(
 void *context, const raya_durable_operation_id_v1 *operation_id,
 raya_durable_handoff_result_v1 *result);

typedef struct {
 DWORD struct_size;
 DWORD schema_version;
 DWORD capabilities;
 DWORD reserved[4];
 void *context;
 raya_durable_issue_callback_v1 issue;
 raya_durable_handoff_callback_v1 handoff;
} raya_durable_callbacks_v1;

typedef enum {
 RAYA_DURABLE_VALIDATION_OK=0,
 RAYA_DURABLE_VALIDATION_NULL=1,
 RAYA_DURABLE_VALIDATION_SIZE=2,
 RAYA_DURABLE_VALIDATION_VERSION=3,
 RAYA_DURABLE_VALIDATION_RESERVED=4,
 RAYA_DURABLE_VALIDATION_IDENTITY=5,
 RAYA_DURABLE_VALIDATION_CAPABILITY=6,
 RAYA_DURABLE_VALIDATION_GUARANTEE=7,
 RAYA_DURABLE_VALIDATION_EVIDENCE=8
} raya_durable_validation_v1;

int raya_durable_operation_id_valid_v1(const raya_durable_operation_id_v1 *id);
int raya_durable_operation_id_equal_v1(
 const raya_durable_operation_id_v1 *left,
 const raya_durable_operation_id_v1 *right);
raya_durable_validation_v1 raya_durable_config_validate_v1(
 const raya_durable_config_v1 *config);
raya_durable_validation_v1 raya_durable_callbacks_validate_v1(
 const raya_durable_config_v1 *config,
 const raya_durable_callbacks_v1 *callbacks);
raya_durable_issue_disposition_v1 raya_durable_issue_normalize_v1(
 const raya_durable_issue_result_v1 *result,
 const raya_durable_operation_id_v1 *expected);
raya_durable_handoff_disposition_v1 raya_durable_handoff_normalize_v1(
 const raya_durable_handoff_result_v1 *result,
 const raya_durable_operation_id_v1 *expected);

#ifdef __cplusplus
}
#endif
#endif
