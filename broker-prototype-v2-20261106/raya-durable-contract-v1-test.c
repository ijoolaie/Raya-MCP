#include "raya-durable-contract-v1.h"
#include <stdio.h>
#include <string.h>

static unsigned g_checks, g_failures;
#define CHECK(x) do { ++g_checks; if (!(x)) { ++g_failures; \
 printf("FAIL line %u: %s\n",(unsigned)__LINE__,#x); } } while (0)

static void WINAPI test_issue_callback(void *context,
 const raya_durable_operation_id_v1 *operation_id,
 raya_durable_issue_result_v1 *result) {
 (void)context; (void)operation_id; (void)result;
}
static void WINAPI test_handoff_callback(void *context,
 const raya_durable_operation_id_v1 *operation_id,
 raya_durable_handoff_result_v1 *result) {
 (void)context; (void)operation_id; (void)result;
}
static void make_id(raya_durable_operation_id_v1 *id) {
 size_t i;
 memset(id,0,sizeof(*id));
 id->struct_size=(DWORD)sizeof(*id);
 id->schema_version=RAYA_DURABLE_CONTRACT_SCHEMA_V1;
 id->sequence=17;
 id->operation_kind=RAYA_DURABLE_OPERATION_WRITE;
 for (i=0;i<sizeof(id->name_space);++i) id->name_space[i]=(BYTE)(i+1);
 for (i=0;i<sizeof(id->payload_digest);++i) id->payload_digest[i]=(BYTE)(0xA0u+i);
}
static void make_config(raya_durable_config_v1 *c) {
 memset(c,0,sizeof(*c));
 c->struct_size=(DWORD)sizeof(*c);
 c->schema_version=RAYA_DURABLE_CONTRACT_SCHEMA_V1;
 c->capabilities=RAYA_DURABLE_CAP_IDENTITY |
  RAYA_DURABLE_CAP_LOCAL_JOURNAL | RAYA_DURABLE_CAP_TYPED_ISSUE |
  RAYA_DURABLE_CAP_TYPED_HANDOFF;
 c->guarantee=RAYA_DURABLE_GUARANTEE_QUARANTINE_ONLY;
 memcpy(c->name_space,"RAYA-TEST-NS-001",16);
}
int main(void) {
 raya_durable_operation_id_v1 id, other;
 raya_durable_config_v1 cfg;
 raya_durable_callbacks_v1 callbacks;
 raya_durable_issue_result_v1 issue;
 raya_durable_handoff_result_v1 handoff;
 make_id(&id); other=id;
 CHECK(raya_durable_operation_id_valid_v1(&id));
 CHECK(raya_durable_operation_id_equal_v1(&id,&other));
 other.sequence++;
 CHECK(!raya_durable_operation_id_equal_v1(&id,&other));
 other=id; other.sequence=0;
 CHECK(!raya_durable_operation_id_valid_v1(&other));
 other=id; other.name_space[0]=0;
 memset(other.name_space,0,sizeof(other.name_space));
 CHECK(!raya_durable_operation_id_valid_v1(&other));
 other=id; other.payload_digest[0]=0;
 memset(other.payload_digest,0,sizeof(other.payload_digest));
 CHECK(!raya_durable_operation_id_valid_v1(&other));
 other=id; other.reserved=1;
 CHECK(!raya_durable_operation_id_valid_v1(&other));

 make_config(&cfg);
 CHECK(raya_durable_config_validate_v1(&cfg)==RAYA_DURABLE_VALIDATION_OK);
 cfg.capabilities|=0x80000000u;
 CHECK(raya_durable_config_validate_v1(&cfg)==RAYA_DURABLE_VALIDATION_CAPABILITY);
 make_config(&cfg); cfg.reserved[2]=1;
 CHECK(raya_durable_config_validate_v1(&cfg)==RAYA_DURABLE_VALIDATION_RESERVED);
 make_config(&cfg); cfg.struct_size--;
 CHECK(raya_durable_config_validate_v1(&cfg)==RAYA_DURABLE_VALIDATION_SIZE);
 make_config(&cfg); cfg.schema_version++;
 CHECK(raya_durable_config_validate_v1(&cfg)==RAYA_DURABLE_VALIDATION_VERSION);
 make_config(&cfg); cfg.name_space[0]=0; memset(cfg.name_space,0,16);
 CHECK(raya_durable_config_validate_v1(&cfg)==RAYA_DURABLE_VALIDATION_CAPABILITY);
 make_config(&cfg); cfg.capabilities|=RAYA_DURABLE_CAP_RECIPIENT_RECEIPT;
 CHECK(raya_durable_config_validate_v1(&cfg)==RAYA_DURABLE_VALIDATION_CAPABILITY);
 make_config(&cfg); cfg.guarantee=RAYA_DURABLE_GUARANTEE_RECIPIENT_COMMIT;
 CHECK(raya_durable_config_validate_v1(&cfg)==RAYA_DURABLE_VALIDATION_GUARANTEE);
 make_config(&cfg); cfg.capabilities|=RAYA_DURABLE_CAP_RECIPIENT_IDEMPOTENCY |
  RAYA_DURABLE_CAP_RECIPIENT_RECEIPT | RAYA_DURABLE_CAP_RECIPIENT_STATUS_QUERY;
 cfg.guarantee=RAYA_DURABLE_GUARANTEE_RECIPIENT_COMMIT;
 CHECK(raya_durable_config_validate_v1(&cfg)==RAYA_DURABLE_VALIDATION_OK);
 CHECK(raya_durable_config_validate_v1(NULL)==RAYA_DURABLE_VALIDATION_NULL);

 memset(&callbacks,0,sizeof(callbacks));
 callbacks.struct_size=(DWORD)sizeof(callbacks);
 callbacks.schema_version=RAYA_DURABLE_CONTRACT_SCHEMA_V1;
 callbacks.capabilities=cfg.capabilities;
 callbacks.issue=test_issue_callback;
 callbacks.handoff=test_handoff_callback;
 CHECK(raya_durable_callbacks_validate_v1(&cfg,&callbacks)==RAYA_DURABLE_VALIDATION_OK);
 CHECK(raya_durable_callbacks_validate_v1(&cfg,NULL)==RAYA_DURABLE_VALIDATION_NULL);
 callbacks.struct_size--; CHECK(raya_durable_callbacks_validate_v1(&cfg,&callbacks)==RAYA_DURABLE_VALIDATION_SIZE);
 callbacks.struct_size=(DWORD)sizeof(callbacks); callbacks.schema_version++;
 CHECK(raya_durable_callbacks_validate_v1(&cfg,&callbacks)==RAYA_DURABLE_VALIDATION_VERSION);
 callbacks.schema_version=RAYA_DURABLE_CONTRACT_SCHEMA_V1; callbacks.reserved[1]=1;
 CHECK(raya_durable_callbacks_validate_v1(&cfg,&callbacks)==RAYA_DURABLE_VALIDATION_RESERVED);
 callbacks.reserved[1]=0; callbacks.capabilities^=RAYA_DURABLE_CAP_TYPED_ISSUE;
 CHECK(raya_durable_callbacks_validate_v1(&cfg,&callbacks)==RAYA_DURABLE_VALIDATION_CAPABILITY);
 callbacks.capabilities=cfg.capabilities; callbacks.issue=NULL;
 CHECK(raya_durable_callbacks_validate_v1(&cfg,&callbacks)==RAYA_DURABLE_VALIDATION_CAPABILITY);
 callbacks.issue=test_issue_callback; callbacks.handoff=NULL;
 CHECK(raya_durable_callbacks_validate_v1(&cfg,&callbacks)==RAYA_DURABLE_VALIDATION_CAPABILITY);
 callbacks.handoff=test_handoff_callback;
 /* NULL context is permitted by the type contract; ownership is external. */
 callbacks.context=NULL;
 CHECK(raya_durable_callbacks_validate_v1(&cfg,&callbacks)==RAYA_DURABLE_VALIDATION_OK);
 { int context_marker=7; callbacks.context=&context_marker;
  CHECK(raya_durable_callbacks_validate_v1(&cfg,&callbacks)==RAYA_DURABLE_VALIDATION_OK); }
 cfg.guarantee=0;
 CHECK(raya_durable_callbacks_validate_v1(&cfg,&callbacks)==RAYA_DURABLE_VALIDATION_GUARANTEE);
 cfg.guarantee=RAYA_DURABLE_GUARANTEE_QUARANTINE_ONLY;

 memset(&issue,0,sizeof(issue));
 issue.struct_size=(DWORD)sizeof(issue);
 issue.schema_version=RAYA_DURABLE_CONTRACT_SCHEMA_V1;
 issue.disposition=RAYA_DURABLE_ISSUE_OS_ACCEPTED;
 issue.native_status=ERROR_IO_PENDING;
 issue.operation_id_echo=id;
 CHECK(raya_durable_issue_normalize_v1(&issue,&id)==RAYA_DURABLE_ISSUE_OS_ACCEPTED);
 issue.native_status=ERROR_ACCESS_DENIED;
 CHECK(raya_durable_issue_normalize_v1(&issue,&id)==RAYA_DURABLE_ISSUE_UNKNOWN);
 issue.disposition=RAYA_DURABLE_ISSUE_PROVEN_NOT_SUBMITTED;
 issue.native_status=ERROR_ACCESS_DENIED;
 CHECK(raya_durable_issue_normalize_v1(&issue,&id)==RAYA_DURABLE_ISSUE_PROVEN_NOT_SUBMITTED);
 issue.native_status=ERROR_SUCCESS;
 CHECK(raya_durable_issue_normalize_v1(&issue,&id)==RAYA_DURABLE_ISSUE_UNKNOWN);
 issue.native_status=ERROR_ACCESS_DENIED; issue.operation_id_echo.sequence++;
 CHECK(raya_durable_issue_normalize_v1(&issue,&id)==RAYA_DURABLE_ISSUE_UNKNOWN);
 issue.operation_id_echo=id; issue.reserved=1;
 CHECK(raya_durable_issue_normalize_v1(&issue,&id)==RAYA_DURABLE_ISSUE_UNKNOWN);
 CHECK(raya_durable_issue_normalize_v1(NULL,&id)==RAYA_DURABLE_ISSUE_UNKNOWN);

 memset(&handoff,0,sizeof(handoff));
 handoff.struct_size=(DWORD)sizeof(handoff);
 handoff.schema_version=RAYA_DURABLE_CONTRACT_SCHEMA_V1;
 handoff.disposition=RAYA_DURABLE_HANDOFF_TRANSFERRED;
 handoff.worker_instance[0]=1;
 handoff.worker_operation_token=99;
 handoff.operation_id_echo=id;
 CHECK(raya_durable_handoff_normalize_v1(&handoff,&id)==RAYA_DURABLE_HANDOFF_TRANSFERRED);
 handoff.worker_operation_token=0;
 CHECK(raya_durable_handoff_normalize_v1(&handoff,&id)==RAYA_DURABLE_HANDOFF_UNKNOWN);
 handoff.disposition=RAYA_DURABLE_HANDOFF_NOT_TRANSFERRED;
 memset(handoff.worker_instance,0,sizeof(handoff.worker_instance));
 CHECK(raya_durable_handoff_normalize_v1(&handoff,&id)==RAYA_DURABLE_HANDOFF_NOT_TRANSFERRED);
 handoff.worker_operation_token=1;
 CHECK(raya_durable_handoff_normalize_v1(&handoff,&id)==RAYA_DURABLE_HANDOFF_UNKNOWN);
 handoff.worker_operation_token=0; handoff.operation_id_echo.payload_digest[0]^=1;
 CHECK(raya_durable_handoff_normalize_v1(&handoff,&id)==RAYA_DURABLE_HANDOFF_UNKNOWN);
 CHECK(raya_durable_handoff_normalize_v1(NULL,&id)==RAYA_DURABLE_HANDOFF_UNKNOWN);

 /* Malformed identity and result fields must fail closed. */
 other=id; other.struct_size--; CHECK(!raya_durable_operation_id_valid_v1(&other));
 other=id; other.schema_version++; CHECK(!raya_durable_operation_id_valid_v1(&other));
 other=id; other.operation_kind=0; CHECK(!raya_durable_operation_id_valid_v1(&other));
 other=id; other.operation_kind=99; CHECK(!raya_durable_operation_id_valid_v1(&other));
 other=id; other.name_space[7]^=1; CHECK(!raya_durable_operation_id_equal_v1(&id,&other));
 other=id; other.operation_kind=RAYA_DURABLE_OPERATION_READ; CHECK(!raya_durable_operation_id_equal_v1(&id,&other));
 CHECK(!raya_durable_operation_id_equal_v1(NULL,&id));

 make_config(&cfg); cfg.capabilities=RAYA_DURABLE_CAP_IDENTITY;
 CHECK(raya_durable_config_validate_v1(&cfg)==RAYA_DURABLE_VALIDATION_GUARANTEE);
 make_config(&cfg); cfg.capabilities &= ~RAYA_DURABLE_CAP_TYPED_ISSUE;
 CHECK(raya_durable_config_validate_v1(&cfg)==RAYA_DURABLE_VALIDATION_CAPABILITY);
 make_config(&cfg); cfg.capabilities &= ~RAYA_DURABLE_CAP_TYPED_HANDOFF;
 CHECK(raya_durable_config_validate_v1(&cfg)==RAYA_DURABLE_VALIDATION_CAPABILITY);
 make_config(&cfg); cfg.guarantee=0;
 CHECK(raya_durable_config_validate_v1(&cfg)==RAYA_DURABLE_VALIDATION_GUARANTEE);
 make_config(&cfg); cfg.capabilities |= RAYA_DURABLE_CAP_RECIPIENT_IDEMPOTENCY;
 CHECK(raya_durable_config_validate_v1(&cfg)==RAYA_DURABLE_VALIDATION_CAPABILITY);
 make_config(&cfg); cfg.capabilities |= RAYA_DURABLE_CAP_RECIPIENT_IDEMPOTENCY |
  RAYA_DURABLE_CAP_RECIPIENT_RECEIPT | RAYA_DURABLE_CAP_RECIPIENT_STATUS_QUERY;
 CHECK(raya_durable_config_validate_v1(&cfg)==RAYA_DURABLE_VALIDATION_OK);

 memset(&issue,0,sizeof(issue)); issue.struct_size=(DWORD)sizeof(issue);
 issue.schema_version=RAYA_DURABLE_CONTRACT_SCHEMA_V1; issue.operation_id_echo=id;
 issue.disposition=(raya_durable_issue_disposition_v1)99;
 CHECK(raya_durable_issue_normalize_v1(&issue,&id)==RAYA_DURABLE_ISSUE_UNKNOWN);
 issue.disposition=RAYA_DURABLE_ISSUE_OS_ACCEPTED; issue.native_status=ERROR_SUCCESS;
 issue.struct_size--; CHECK(raya_durable_issue_normalize_v1(&issue,&id)==RAYA_DURABLE_ISSUE_UNKNOWN);
 issue.struct_size=(DWORD)sizeof(issue); issue.schema_version++;
 CHECK(raya_durable_issue_normalize_v1(&issue,&id)==RAYA_DURABLE_ISSUE_UNKNOWN);
 issue.schema_version=RAYA_DURABLE_CONTRACT_SCHEMA_V1; issue.reserved=1;
 CHECK(raya_durable_issue_normalize_v1(&issue,&id)==RAYA_DURABLE_ISSUE_UNKNOWN);
 issue.reserved=0; issue.disposition=RAYA_DURABLE_ISSUE_OS_ACCEPTED;
 issue.native_status=ERROR_SUCCESS; issue.transferred_bytes=0xFFFFFFFFu;
 CHECK(raya_durable_issue_normalize_v1(&issue,&id)==RAYA_DURABLE_ISSUE_OS_ACCEPTED);
 issue.disposition=RAYA_DURABLE_ISSUE_UNKNOWN;
 CHECK(raya_durable_issue_normalize_v1(&issue,&id)==RAYA_DURABLE_ISSUE_UNKNOWN);

 memset(&handoff,0,sizeof(handoff)); handoff.struct_size=(DWORD)sizeof(handoff);
 handoff.schema_version=RAYA_DURABLE_CONTRACT_SCHEMA_V1; handoff.operation_id_echo=id;
 handoff.disposition=(raya_durable_handoff_disposition_v1)99;
 CHECK(raya_durable_handoff_normalize_v1(&handoff,&id)==RAYA_DURABLE_HANDOFF_UNKNOWN);
 handoff.disposition=RAYA_DURABLE_HANDOFF_TRANSFERRED; handoff.worker_instance[0]=1;
 handoff.worker_operation_token=1; handoff.struct_size--;
 CHECK(raya_durable_handoff_normalize_v1(&handoff,&id)==RAYA_DURABLE_HANDOFF_UNKNOWN);
 handoff.struct_size=(DWORD)sizeof(handoff); handoff.schema_version++;
 CHECK(raya_durable_handoff_normalize_v1(&handoff,&id)==RAYA_DURABLE_HANDOFF_UNKNOWN);
 handoff.schema_version=RAYA_DURABLE_CONTRACT_SCHEMA_V1; handoff.reserved=1;
 CHECK(raya_durable_handoff_normalize_v1(&handoff,&id)==RAYA_DURABLE_HANDOFF_UNKNOWN);
 handoff.reserved=0; handoff.disposition=RAYA_DURABLE_HANDOFF_NOT_TRANSFERRED;
 handoff.worker_instance[0]=1;
 CHECK(raya_durable_handoff_normalize_v1(&handoff,&id)==RAYA_DURABLE_HANDOFF_UNKNOWN);

 printf("Raya durable contract v1: %u checks, %u failures\n",g_checks,g_failures);
 return g_failures ? 1 : 0;
}
