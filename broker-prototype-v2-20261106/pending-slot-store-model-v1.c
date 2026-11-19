#include "pending-slot-store-model-v1.h"
#include <string.h>
void raya_pending_model_init(raya_pending_store_model*m){if(m)memset(m,0,sizeof(*m));}
int raya_pending_model_seed(raya_pending_store_model*m,unsigned s,const uint8_t b[256]){if(!m||!b||s>=2)return 0;memcpy(m->volatile_slots[s],b,256);memcpy(m->durable_slots[s],b,256);return 1;}
int raya_pending_model_write(void*ctx,unsigned s,const uint8_t b[256]){raya_pending_store_model*m=(raya_pending_store_model*)ctx;if(!m||!b||s>=2)return 0;m->writes++;if(m->fault==RAYA_MODEL_FAULT_WRITE_FAIL)return 0;if(m->fault==RAYA_MODEL_FAULT_WRITE_PARTIAL||m->fault==RAYA_MODEL_FAULT_WRITE_PARTIAL_DURABLE){unsigned n=m->partial_bytes;if(n>256)n=256;memcpy(m->volatile_slots[s],b,n);if(m->fault==RAYA_MODEL_FAULT_WRITE_PARTIAL_DURABLE)memcpy(m->durable_slots[s],b,n);return 0;}memcpy(m->volatile_slots[s],b,256);return 1;}
int raya_pending_model_flush(void*ctx){raya_pending_store_model*m=(raya_pending_store_model*)ctx;if(!m)return 0;m->flushes++;if(m->fault==RAYA_MODEL_FAULT_FLUSH_FAIL_NOT_DURABLE)return 0;if(m->fault==RAYA_MODEL_FAULT_FLUSH_FAIL_DURABLE){memcpy(m->durable_slots,m->volatile_slots,sizeof(m->durable_slots));return 0;}memcpy(m->durable_slots,m->volatile_slots,sizeof(m->durable_slots));return 1;}
int raya_pending_model_readback(void*ctx,unsigned s,uint8_t b[256]){raya_pending_store_model*m=(raya_pending_store_model*)ctx;if(!m||!b||s>=2)return 0;m->reads++;if(m->fault==RAYA_MODEL_FAULT_READBACK_FAIL)return 0;memcpy(b,m->volatile_slots[s],256);if(m->fault==RAYA_MODEL_FAULT_READBACK_MISMATCH)b[100]^=0x80;return 1;}
void raya_pending_model_power_loss(raya_pending_store_model*m){if(m)memcpy(m->volatile_slots,m->durable_slots,sizeof(m->volatile_slots));}
raya_slot_select_result raya_pending_model_recover(const raya_pending_store_model*m,unsigned*s,uint64_t*r,raya_pending_state*st){if(!m)return RAYA_SLOT_SELECT_ERROR;return raya_pending_slots_select((const uint8_t (*)[256])m->durable_slots,s,r,st);}
raya_model_commit_result raya_pending_model_commit(raya_pending_store_model*m,const uint8_t rec[128],unsigned*published,uint64_t*published_revision){
 unsigned current,inactive;uint64_t revision,old_revision,new_revision;raya_pending_state state;uint8_t next[256],verify[256];raya_slot_select_result sr;
 if(!m||!rec||!published||!published_revision)return RAYA_MODEL_COMMIT_INVALID;
 sr=raya_pending_slots_select((const uint8_t (*)[256])m->volatile_slots,&current,&old_revision,&state);
 if(sr==RAYA_SLOT_SELECT_UNPROVISIONED){current=1;old_revision=0;}
 else if(sr!=RAYA_SLOT_SELECT_VALID)return RAYA_MODEL_COMMIT_INVALID;
 if(old_revision==UINT64_MAX-1)return RAYA_MODEL_COMMIT_INVALID;
 new_revision=old_revision+1;inactive=1u-current;
 if(!raya_pending_slot_encode(rec,new_revision,next))return RAYA_MODEL_COMMIT_INVALID;
 if(!raya_pending_model_write(m,inactive,next))return RAYA_MODEL_COMMIT_UNCERTAIN;
 if(!raya_pending_model_flush(m))return RAYA_MODEL_COMMIT_UNCERTAIN;
 if(!raya_pending_model_readback(m,inactive,verify))return RAYA_MODEL_COMMIT_UNCERTAIN;
 if(memcmp(verify,next,256)!=0)return RAYA_MODEL_COMMIT_UNCERTAIN;
 if(!raya_pending_slot_decode(verify,&revision,&state)||revision!=new_revision)return RAYA_MODEL_COMMIT_UNCERTAIN;

 *published=inactive;*published_revision=new_revision;return RAYA_MODEL_COMMIT_OK;
}
