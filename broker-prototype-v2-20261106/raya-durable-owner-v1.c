#include "raya-durable-owner-v1.h"
#include <string.h>
static int valid(const raya_durable_owner_v1 *o) {
 size_t i,j; DWORD active=0,unknown=0;
 if(!o||o->initialized!=1||o->struct_size!=sizeof(*o)||o->schema_version!=RAYA_DURABLE_CONTRACT_SCHEMA_V1||!o->next_lease_id||o->state<RAYA_DURABLE_OWNER_CREATED_V1||o->state>RAYA_DURABLE_OWNER_CLOSED_V1||o->close_requested>1)return 0;
 for(i=0;i<4;++i)if(o->reserved[i])return 0;
 if((o->state==RAYA_DURABLE_OWNER_CLOSED_V1)!=(o->callbacks==NULL))return 0;
 for(i=0;i<RAYA_DURABLE_OWNER_LEASE_CAPACITY_V1;++i){
  const raya_durable_owner_lease_record_v1 *r=&o->leases[i];
  if(r->state==RAYA_DURABLE_LEASE_FREE_V1){if(r->id!=0)return 0;}
  else if(r->state==RAYA_DURABLE_LEASE_ACTIVE_V1||r->state==RAYA_DURABLE_LEASE_UNKNOWN_V1){
   if(r->id==0||r->id>=o->next_lease_id)return 0;
   if(r->state==RAYA_DURABLE_LEASE_ACTIVE_V1)++active;else ++unknown;
   for(j=i+1;j<RAYA_DURABLE_OWNER_LEASE_CAPACITY_V1;++j)
    if(o->leases[j].state!=RAYA_DURABLE_LEASE_FREE_V1&&o->leases[j].id==r->id)return 0;
  } else return 0;
 }
 if(active!=o->in_flight||unknown!=o->unresolved_unknown)return 0;
 if((o->state==RAYA_DURABLE_OWNER_QUARANTINED_V1)!=(unknown!=0))return 0;
 if((o->state==RAYA_DURABLE_OWNER_CREATED_V1||o->state==RAYA_DURABLE_OWNER_CLOSED_V1)&&
    (active!=0||unknown!=0))return 0;
 if((o->state==RAYA_DURABLE_OWNER_CREATED_V1||o->state==RAYA_DURABLE_OWNER_ACTIVE_V1) &&
    o->close_requested!=0)return 0;
 if((o->state==RAYA_DURABLE_OWNER_CLOSING_V1||o->state==RAYA_DURABLE_OWNER_CLOSED_V1) &&
    o->close_requested!=1)return 0;
 return 1;
}
raya_durable_owner_status_v1 raya_durable_owner_init_v1(raya_durable_owner_v1 *o,const raya_durable_callbacks_v1 *c){if(!o||!c)return RAYA_DURABLE_OWNER_INVALID_V1;memset(o,0,sizeof(*o));o->struct_size=(DWORD)sizeof(*o);o->schema_version=RAYA_DURABLE_CONTRACT_SCHEMA_V1;o->state=RAYA_DURABLE_OWNER_CREATED_V1;o->next_lease_id=1;o->callbacks=c;o->initialized=1;return RAYA_DURABLE_OWNER_OK_V1;}
raya_durable_owner_status_v1 raya_durable_owner_activate_v1(raya_durable_owner_v1 *o){if(!valid(o)||o->state!=RAYA_DURABLE_OWNER_CREATED_V1)return RAYA_DURABLE_OWNER_INVALID_V1;o->state=RAYA_DURABLE_OWNER_ACTIVE_V1;return RAYA_DURABLE_OWNER_OK_V1;}
raya_durable_owner_status_v1 raya_durable_owner_admit_v1(raya_durable_owner_v1 *o,ULONGLONG *out){size_t i;if(out)*out=0;if(!valid(o)||!out)return RAYA_DURABLE_OWNER_INVALID_V1;if(o->state==RAYA_DURABLE_OWNER_QUARANTINED_V1)return RAYA_DURABLE_OWNER_QUARANTINED_RESULT_V1;if(o->state!=RAYA_DURABLE_OWNER_ACTIVE_V1)return RAYA_DURABLE_OWNER_BUSY_V1;if(o->next_lease_id==0||o->next_lease_id==~(ULONGLONG)0)return RAYA_DURABLE_OWNER_CAPACITY_V1;for(i=0;i<RAYA_DURABLE_OWNER_LEASE_CAPACITY_V1;++i)if(o->leases[i].state==RAYA_DURABLE_LEASE_FREE_V1)break;if(i==RAYA_DURABLE_OWNER_LEASE_CAPACITY_V1)return RAYA_DURABLE_OWNER_CAPACITY_V1;o->leases[i].id=o->next_lease_id++;o->leases[i].state=RAYA_DURABLE_LEASE_ACTIVE_V1;++o->in_flight;*out=o->leases[i].id;return RAYA_DURABLE_OWNER_OK_V1;}
raya_durable_owner_status_v1 raya_durable_owner_complete_v1(raya_durable_owner_v1 *o,ULONGLONG id,raya_durable_callback_outcome_v1 outcome){size_t i;if(!valid(o)||!id||(outcome!=RAYA_DURABLE_CALLBACK_KNOWN_V1&&outcome!=RAYA_DURABLE_CALLBACK_UNKNOWN_V1))return RAYA_DURABLE_OWNER_INVALID_V1;for(i=0;i<RAYA_DURABLE_OWNER_LEASE_CAPACITY_V1;++i)if(o->leases[i].id==id&&o->leases[i].state==RAYA_DURABLE_LEASE_ACTIVE_V1)break;if(i==RAYA_DURABLE_OWNER_LEASE_CAPACITY_V1||!o->in_flight)return RAYA_DURABLE_OWNER_INVALID_V1;--o->in_flight;if(outcome==RAYA_DURABLE_CALLBACK_UNKNOWN_V1){o->leases[i].state=RAYA_DURABLE_LEASE_UNKNOWN_V1;++o->unresolved_unknown;o->state=RAYA_DURABLE_OWNER_QUARANTINED_V1;return RAYA_DURABLE_OWNER_QUARANTINED_RESULT_V1;}o->leases[i].state=RAYA_DURABLE_LEASE_FREE_V1;o->leases[i].id=0;return RAYA_DURABLE_OWNER_OK_V1;}
raya_durable_owner_status_v1 raya_durable_owner_begin_close_v1(raya_durable_owner_v1 *o){if(!valid(o)||o->state==RAYA_DURABLE_OWNER_CLOSED_V1)return RAYA_DURABLE_OWNER_INVALID_V1;if(o->state==RAYA_DURABLE_OWNER_CREATED_V1||o->state==RAYA_DURABLE_OWNER_ACTIVE_V1){o->close_requested=1;o->state=RAYA_DURABLE_OWNER_CLOSING_V1;}else if(o->state==RAYA_DURABLE_OWNER_QUARANTINED_V1)o->close_requested=1;else if(o->state!=RAYA_DURABLE_OWNER_CLOSING_V1)return RAYA_DURABLE_OWNER_INVALID_V1;return(!o->in_flight&&!o->unresolved_unknown)?RAYA_DURABLE_OWNER_OK_V1:RAYA_DURABLE_OWNER_BUSY_V1;}
raya_durable_owner_status_v1 raya_durable_owner_reconcile_v1(raya_durable_owner_v1 *o,ULONGLONG id,int determinate){size_t i;if(!valid(o)||!id||determinate!=1)return RAYA_DURABLE_OWNER_INVALID_V1;for(i=0;i<RAYA_DURABLE_OWNER_LEASE_CAPACITY_V1;++i)if(o->leases[i].id==id&&o->leases[i].state==RAYA_DURABLE_LEASE_UNKNOWN_V1)break;if(i==RAYA_DURABLE_OWNER_LEASE_CAPACITY_V1||!o->unresolved_unknown)return RAYA_DURABLE_OWNER_INVALID_V1;o->leases[i].state=RAYA_DURABLE_LEASE_FREE_V1;o->leases[i].id=0;--o->unresolved_unknown;if(!o->unresolved_unknown&&o->state==RAYA_DURABLE_OWNER_QUARANTINED_V1)o->state=o->close_requested?RAYA_DURABLE_OWNER_CLOSING_V1:RAYA_DURABLE_OWNER_ACTIVE_V1;return RAYA_DURABLE_OWNER_OK_V1;}
raya_durable_owner_status_v1 raya_durable_owner_finish_close_v1(raya_durable_owner_v1 *o){if(!valid(o)||!o->close_requested)return RAYA_DURABLE_OWNER_INVALID_V1;if(o->state==RAYA_DURABLE_OWNER_QUARANTINED_V1||o->unresolved_unknown)return RAYA_DURABLE_OWNER_QUARANTINED_RESULT_V1;if(o->state!=RAYA_DURABLE_OWNER_CLOSING_V1||o->in_flight)return RAYA_DURABLE_OWNER_BUSY_V1;o->state=RAYA_DURABLE_OWNER_CLOSED_V1;o->callbacks=NULL;return RAYA_DURABLE_OWNER_OK_V1;}
