#include "raya-durable-contract-v1.h"
#include <string.h>

static int bytes_nonzero(const BYTE *bytes, size_t count) {
 size_t i;
 if (bytes==NULL) return 0;
 for (i=0;i<count;++i) if (bytes[i]!=0) return 1;
 return 0;
}

int raya_durable_operation_id_valid_v1(const raya_durable_operation_id_v1 *id) {
 if (id==NULL || id->struct_size!=sizeof(*id) ||
     id->schema_version!=RAYA_DURABLE_CONTRACT_SCHEMA_V1 ||
     id->sequence==0 || id->reserved!=0 ||
     id->operation_kind<RAYA_DURABLE_OPERATION_GENERIC ||
     id->operation_kind>RAYA_DURABLE_OPERATION_MESSAGE)
  return 0;
 if (!bytes_nonzero(id->name_space,sizeof(id->name_space)) ||
     !bytes_nonzero(id->payload_digest,sizeof(id->payload_digest)))
  return 0;
 return 1;
}

int raya_durable_operation_id_equal_v1(
 const raya_durable_operation_id_v1 *left,
 const raya_durable_operation_id_v1 *right) {
 if (!raya_durable_operation_id_valid_v1(left) ||
     !raya_durable_operation_id_valid_v1(right))
  return 0;
 return left->schema_version==right->schema_version &&
  left->sequence==right->sequence &&
  left->operation_kind==right->operation_kind &&
  memcmp(left->name_space,right->name_space,sizeof(left->name_space))==0 &&
  memcmp(left->payload_digest,right->payload_digest,
         sizeof(left->payload_digest))==0;
}

raya_durable_validation_v1 raya_durable_config_validate_v1(
 const raya_durable_config_v1 *config) {
 const DWORD known=RAYA_DURABLE_CAP_IDENTITY |
  RAYA_DURABLE_CAP_LOCAL_JOURNAL | RAYA_DURABLE_CAP_TYPED_ISSUE |
  RAYA_DURABLE_CAP_TYPED_HANDOFF | RAYA_DURABLE_CAP_RECIPIENT_IDEMPOTENCY |
  RAYA_DURABLE_CAP_RECIPIENT_RECEIPT | RAYA_DURABLE_CAP_RECIPIENT_STATUS_QUERY;
 const DWORD local_required=RAYA_DURABLE_CAP_IDENTITY |
  RAYA_DURABLE_CAP_LOCAL_JOURNAL | RAYA_DURABLE_CAP_TYPED_ISSUE |
  RAYA_DURABLE_CAP_TYPED_HANDOFF;
 const DWORD recipient_required=RAYA_DURABLE_CAP_RECIPIENT_IDEMPOTENCY |
  RAYA_DURABLE_CAP_RECIPIENT_RECEIPT | RAYA_DURABLE_CAP_RECIPIENT_STATUS_QUERY;
 size_t i;
 if (config==NULL) return RAYA_DURABLE_VALIDATION_NULL;
 if (config->struct_size!=sizeof(*config)) return RAYA_DURABLE_VALIDATION_SIZE;
 if (config->schema_version!=RAYA_DURABLE_CONTRACT_SCHEMA_V1)
  return RAYA_DURABLE_VALIDATION_VERSION;
 for (i=0;i<sizeof(config->reserved)/sizeof(config->reserved[0]);++i)
  if (config->reserved[i]!=0) return RAYA_DURABLE_VALIDATION_RESERVED;
 if ((config->capabilities & ~known)!=0)
  return RAYA_DURABLE_VALIDATION_CAPABILITY;
 if ((config->capabilities & RAYA_DURABLE_CAP_LOCAL_JOURNAL)!=0) {
  if ((config->capabilities & local_required)!=local_required ||
      !bytes_nonzero(config->name_space,sizeof(config->name_space)))
   return RAYA_DURABLE_VALIDATION_CAPABILITY;
 }
 if ((config->capabilities & recipient_required)!=0 &&
     (config->capabilities & recipient_required)!=recipient_required)
  return RAYA_DURABLE_VALIDATION_CAPABILITY;
 if (config->guarantee==RAYA_DURABLE_GUARANTEE_QUARANTINE_ONLY) {
  if ((config->capabilities & local_required)!=local_required)
   return RAYA_DURABLE_VALIDATION_GUARANTEE;
 } else if (config->guarantee==RAYA_DURABLE_GUARANTEE_RECIPIENT_COMMIT) {
  if ((config->capabilities & local_required)!=local_required ||
      (config->capabilities & recipient_required)!=recipient_required)
   return RAYA_DURABLE_VALIDATION_GUARANTEE;
 } else {
  return RAYA_DURABLE_VALIDATION_GUARANTEE;
 }
 return RAYA_DURABLE_VALIDATION_OK;
}

raya_durable_validation_v1 raya_durable_callbacks_validate_v1(
 const raya_durable_config_v1 *config,
 const raya_durable_callbacks_v1 *callbacks) {
 size_t i;
 raya_durable_validation_v1 config_result=raya_durable_config_validate_v1(config);
 if (config_result!=RAYA_DURABLE_VALIDATION_OK) return config_result;
 if (callbacks==NULL) return RAYA_DURABLE_VALIDATION_NULL;
 if (callbacks->struct_size!=sizeof(*callbacks)) return RAYA_DURABLE_VALIDATION_SIZE;
 if (callbacks->schema_version!=RAYA_DURABLE_CONTRACT_SCHEMA_V1)
  return RAYA_DURABLE_VALIDATION_VERSION;
 for (i=0;i<sizeof(callbacks->reserved)/sizeof(callbacks->reserved[0]);++i)
  if (callbacks->reserved[i]!=0) return RAYA_DURABLE_VALIDATION_RESERVED;
 if (callbacks->capabilities!=config->capabilities)
  return RAYA_DURABLE_VALIDATION_CAPABILITY;
 if (((config->capabilities & RAYA_DURABLE_CAP_TYPED_ISSUE)!=0) !=
     (callbacks->issue!=NULL)) return RAYA_DURABLE_VALIDATION_CAPABILITY;
 if (((config->capabilities & RAYA_DURABLE_CAP_TYPED_HANDOFF)!=0) !=
     (callbacks->handoff!=NULL)) return RAYA_DURABLE_VALIDATION_CAPABILITY;
 return RAYA_DURABLE_VALIDATION_OK;
}

raya_durable_issue_disposition_v1 raya_durable_issue_normalize_v1(
 const raya_durable_issue_result_v1 *result,
 const raya_durable_operation_id_v1 *expected) {
 if (result==NULL || expected==NULL ||
     result->struct_size!=sizeof(*result) ||
     result->schema_version!=RAYA_DURABLE_CONTRACT_SCHEMA_V1 ||
     result->reserved!=0 ||
     !raya_durable_operation_id_valid_v1(expected) ||
     !raya_durable_operation_id_equal_v1(&result->operation_id_echo,expected))
  return RAYA_DURABLE_ISSUE_UNKNOWN;
 if (result->disposition==RAYA_DURABLE_ISSUE_PROVEN_NOT_SUBMITTED)
  return result->native_status!=ERROR_SUCCESS ?
   RAYA_DURABLE_ISSUE_PROVEN_NOT_SUBMITTED : RAYA_DURABLE_ISSUE_UNKNOWN;
 if (result->disposition==RAYA_DURABLE_ISSUE_OS_ACCEPTED)
  return (result->native_status==ERROR_SUCCESS ||
          result->native_status==ERROR_IO_PENDING) ?
   RAYA_DURABLE_ISSUE_OS_ACCEPTED : RAYA_DURABLE_ISSUE_UNKNOWN;
 return RAYA_DURABLE_ISSUE_UNKNOWN;
}

raya_durable_handoff_disposition_v1 raya_durable_handoff_normalize_v1(
 const raya_durable_handoff_result_v1 *result,
 const raya_durable_operation_id_v1 *expected) {
 if (result==NULL || expected==NULL ||
     result->struct_size!=sizeof(*result) ||
     result->schema_version!=RAYA_DURABLE_CONTRACT_SCHEMA_V1 ||
     result->reserved!=0 ||
     !raya_durable_operation_id_valid_v1(expected) ||
     !raya_durable_operation_id_equal_v1(&result->operation_id_echo,expected))
  return RAYA_DURABLE_HANDOFF_UNKNOWN;
 if (result->disposition==RAYA_DURABLE_HANDOFF_TRANSFERRED)
  return (bytes_nonzero(result->worker_instance,sizeof(result->worker_instance)) &&
          result->worker_operation_token!=0) ?
   RAYA_DURABLE_HANDOFF_TRANSFERRED : RAYA_DURABLE_HANDOFF_UNKNOWN;
 if (result->disposition==RAYA_DURABLE_HANDOFF_NOT_TRANSFERRED)
  return (result->worker_operation_token==0 &&
          !bytes_nonzero(result->worker_instance,sizeof(result->worker_instance))) ?
   RAYA_DURABLE_HANDOFF_NOT_TRANSFERRED : RAYA_DURABLE_HANDOFF_UNKNOWN;
 return RAYA_DURABLE_HANDOFF_UNKNOWN;
}
