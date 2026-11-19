#include "raya-durable-runtime-v1.h"
#include <string.h>
static int runtime_valid(const raya_durable_runtime_v1 *r) {
 return r && r->magic==RAYA_DURABLE_RUNTIME_MAGIC_V1 &&
  r->struct_size==sizeof(*r) && r->initialized==1 &&
  r->reserved[0]==0 && r->reserved[1]==0 && r->reserved[2]==0 &&
  r->callbacks!=NULL;
}
static void init_issue_result(raya_durable_issue_result_v1 *r,
 const raya_durable_operation_id_v1 *id) {
 memset(r,0,sizeof(*r)); r->struct_size=(DWORD)sizeof(*r);
 r->schema_version=RAYA_DURABLE_CONTRACT_SCHEMA_V1;
 r->disposition=RAYA_DURABLE_ISSUE_UNKNOWN; r->operation_id_echo=*id;
}
static void init_handoff_result(raya_durable_handoff_result_v1 *r,
 const raya_durable_operation_id_v1 *id) {
 memset(r,0,sizeof(*r)); r->struct_size=(DWORD)sizeof(*r);
 r->schema_version=RAYA_DURABLE_CONTRACT_SCHEMA_V1;
 r->disposition=RAYA_DURABLE_HANDOFF_UNKNOWN; r->operation_id_echo=*id;
}
raya_durable_owner_status_v1 raya_durable_runtime_init_v1(
 raya_durable_runtime_v1 *r,const raya_durable_config_v1 *cfg,
 const raya_durable_callbacks_v1 *cb) {
 if(!r || raya_durable_callbacks_validate_v1(cfg,cb)!=RAYA_DURABLE_VALIDATION_OK)
  return RAYA_DURABLE_OWNER_INVALID_V1;
 memset(r,0,sizeof(*r));
 if(!InitializeCriticalSectionEx(&r->state_lock,4000,0))
  return RAYA_DURABLE_OWNER_INVALID_V1;
 if(!InitializeCriticalSectionEx(&r->callback_lock,4000,0)) {
  DeleteCriticalSection(&r->state_lock); memset(r,0,sizeof(*r));
  return RAYA_DURABLE_OWNER_INVALID_V1;
 }
 r->magic=RAYA_DURABLE_RUNTIME_MAGIC_V1;
 r->struct_size=(DWORD)sizeof(*r); r->callbacks=cb;
 if(raya_durable_owner_init_v1(&r->owner,cb)!=RAYA_DURABLE_OWNER_OK_V1 ||
    raya_durable_owner_activate_v1(&r->owner)!=RAYA_DURABLE_OWNER_OK_V1) {
  DeleteCriticalSection(&r->callback_lock);
  DeleteCriticalSection(&r->state_lock); memset(r,0,sizeof(*r));
  return RAYA_DURABLE_OWNER_INVALID_V1;
 }
 r->initialized=1;
 return RAYA_DURABLE_OWNER_OK_V1;
}
static int lease_is_active(const raya_durable_owner_v1 *o,ULONGLONG id) {
 DWORD i;
 for(i=0;i<RAYA_DURABLE_OWNER_LEASE_CAPACITY_V1;++i)
  if(o->leases[i].id==id && o->leases[i].state==RAYA_DURABLE_LEASE_ACTIVE_V1)
   return 1;
 return 0;
}
raya_durable_owner_status_v1 raya_durable_runtime_invoke_issue_v1(
 raya_durable_runtime_v1 *r,const raya_durable_operation_id_v1 *id,
 raya_durable_issue_result_v1 *out,ULONGLONG *lease_out) {
 raya_durable_issue_result_v1 result;
 raya_durable_issue_disposition_v1 normalized;
 raya_durable_owner_status_v1 status;
 ULONGLONG lease=0;
 int execute=0;
 if(out) { memset(out,0,sizeof(*out)); out->struct_size=(DWORD)sizeof(*out); out->schema_version=RAYA_DURABLE_CONTRACT_SCHEMA_V1; out->disposition=RAYA_DURABLE_ISSUE_UNKNOWN; if(id && raya_durable_operation_id_valid_v1(id)) out->operation_id_echo=*id; }
 if(lease_out) *lease_out=0;
 if(!runtime_valid(r)||!id||!out||!lease_out||
    !raya_durable_operation_id_valid_v1(id)||!r->callbacks->issue)
  return RAYA_DURABLE_OWNER_INVALID_V1;
 EnterCriticalSection(&r->state_lock);
 status=raya_durable_owner_admit_v1(&r->owner,&lease);
 LeaveCriticalSection(&r->state_lock);
 if(status!=RAYA_DURABLE_OWNER_OK_V1) return status;
 *lease_out=lease;
 init_issue_result(&result,id);
 EnterCriticalSection(&r->callback_lock);
 EnterCriticalSection(&r->state_lock);
 if(r->owner.state==RAYA_DURABLE_OWNER_ACTIVE_V1 &&
    lease_is_active(&r->owner,lease)) execute=1;
 else {
  status=raya_durable_owner_complete_v1(&r->owner,lease,RAYA_DURABLE_CALLBACK_KNOWN_V1);
  if(r->owner.state==RAYA_DURABLE_OWNER_QUARANTINED_V1)
   status=RAYA_DURABLE_OWNER_QUARANTINED_RESULT_V1;
  else if(status==RAYA_DURABLE_OWNER_OK_V1) status=RAYA_DURABLE_OWNER_BUSY_V1;
 }
 LeaveCriticalSection(&r->state_lock);
 if(!execute) { LeaveCriticalSection(&r->callback_lock); return status; }
 r->callbacks->issue(r->callbacks->context,id,&result);
 normalized=raya_durable_issue_normalize_v1(&result,id);
 result.disposition=normalized;
 *out=result;
 EnterCriticalSection(&r->state_lock);
 status=raya_durable_owner_complete_v1(&r->owner,lease,
  normalized==RAYA_DURABLE_ISSUE_UNKNOWN ?
  RAYA_DURABLE_CALLBACK_UNKNOWN_V1 : RAYA_DURABLE_CALLBACK_KNOWN_V1);
 LeaveCriticalSection(&r->state_lock);
 LeaveCriticalSection(&r->callback_lock);
 return status;
}
raya_durable_owner_status_v1 raya_durable_runtime_invoke_handoff_v1(
 raya_durable_runtime_v1 *r,const raya_durable_operation_id_v1 *id,
 raya_durable_handoff_result_v1 *out,ULONGLONG *lease_out) {
 raya_durable_handoff_result_v1 result;
 raya_durable_handoff_disposition_v1 normalized;
 raya_durable_owner_status_v1 status;
 ULONGLONG lease=0;
 int execute=0;
 if(out) { memset(out,0,sizeof(*out)); out->struct_size=(DWORD)sizeof(*out); out->schema_version=RAYA_DURABLE_CONTRACT_SCHEMA_V1; out->disposition=RAYA_DURABLE_HANDOFF_UNKNOWN; if(id && raya_durable_operation_id_valid_v1(id)) out->operation_id_echo=*id; }
 if(lease_out) *lease_out=0;
 if(!runtime_valid(r)||!id||!out||!lease_out||
    !raya_durable_operation_id_valid_v1(id)||!r->callbacks->handoff)
  return RAYA_DURABLE_OWNER_INVALID_V1;
 EnterCriticalSection(&r->state_lock);
 status=raya_durable_owner_admit_v1(&r->owner,&lease);
 LeaveCriticalSection(&r->state_lock);
 if(status!=RAYA_DURABLE_OWNER_OK_V1) return status;
 *lease_out=lease;
 init_handoff_result(&result,id);
 EnterCriticalSection(&r->callback_lock);
 EnterCriticalSection(&r->state_lock);
 if(r->owner.state==RAYA_DURABLE_OWNER_ACTIVE_V1 &&
    lease_is_active(&r->owner,lease)) execute=1;
 else {
  status=raya_durable_owner_complete_v1(&r->owner,lease,RAYA_DURABLE_CALLBACK_KNOWN_V1);
  if(r->owner.state==RAYA_DURABLE_OWNER_QUARANTINED_V1)
   status=RAYA_DURABLE_OWNER_QUARANTINED_RESULT_V1;
  else if(status==RAYA_DURABLE_OWNER_OK_V1) status=RAYA_DURABLE_OWNER_BUSY_V1;
 }
 LeaveCriticalSection(&r->state_lock);
 if(!execute) { LeaveCriticalSection(&r->callback_lock); return status; }
 r->callbacks->handoff(r->callbacks->context,id,&result);
 normalized=raya_durable_handoff_normalize_v1(&result,id);
 result.disposition=normalized;
 *out=result;
 EnterCriticalSection(&r->state_lock);
 status=raya_durable_owner_complete_v1(&r->owner,lease,
  normalized==RAYA_DURABLE_HANDOFF_UNKNOWN ?
  RAYA_DURABLE_CALLBACK_UNKNOWN_V1 : RAYA_DURABLE_CALLBACK_KNOWN_V1);
 LeaveCriticalSection(&r->state_lock);
 LeaveCriticalSection(&r->callback_lock);
 return status;
}
raya_durable_owner_status_v1 raya_durable_runtime_begin_close_v1(
 raya_durable_runtime_v1 *r) {
 raya_durable_owner_status_v1 s;
 if(!runtime_valid(r)) return RAYA_DURABLE_OWNER_INVALID_V1;
 EnterCriticalSection(&r->state_lock);
 s=raya_durable_owner_begin_close_v1(&r->owner);
 LeaveCriticalSection(&r->state_lock); return s;
}
raya_durable_owner_status_v1 raya_durable_runtime_reconcile_v1(
 raya_durable_runtime_v1 *r,ULONGLONG lease,int determinate) {
 raya_durable_owner_status_v1 s;
 if(!runtime_valid(r)) return RAYA_DURABLE_OWNER_INVALID_V1;
 EnterCriticalSection(&r->state_lock);
 s=raya_durable_owner_reconcile_v1(&r->owner,lease,determinate);
 LeaveCriticalSection(&r->state_lock); return s;
}
raya_durable_owner_status_v1 raya_durable_runtime_finish_close_v1(
 raya_durable_runtime_v1 *r) {
 raya_durable_owner_status_v1 s;
 if(!runtime_valid(r)) return RAYA_DURABLE_OWNER_INVALID_V1;
 EnterCriticalSection(&r->state_lock);
 s=raya_durable_owner_finish_close_v1(&r->owner);
 LeaveCriticalSection(&r->state_lock); return s;
}
raya_durable_owner_status_v1 raya_durable_runtime_destroy_v1(
 raya_durable_runtime_v1 *r) {
 if(!runtime_valid(r)) return RAYA_DURABLE_OWNER_INVALID_V1;
 EnterCriticalSection(&r->state_lock);
 if(r->owner.state!=RAYA_DURABLE_OWNER_CLOSED_V1 ||
    r->owner.in_flight || r->owner.unresolved_unknown) {
  LeaveCriticalSection(&r->state_lock); return RAYA_DURABLE_OWNER_BUSY_V1;
 }
 LeaveCriticalSection(&r->state_lock);
 /* Caller must have externally quiesced every API entrant before this point. */
 DeleteCriticalSection(&r->callback_lock);
 DeleteCriticalSection(&r->state_lock);
 memset(r,0,sizeof(*r));
 return RAYA_DURABLE_OWNER_OK_V1;
}
