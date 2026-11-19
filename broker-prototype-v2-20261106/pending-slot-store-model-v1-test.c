#include "pending-slot-store-model-v1.h"
#include <stdio.h>
#include <string.h>
#define C(x) do { tests++; if (!(x)) { printf("FAIL %d: %s\\n", __LINE__, #x); return 1; } } while (0)
static unsigned tests;
static int make_record(raya_pending_state state,uint8_t out[128]) { uint8_t ns[16]={1},op[16]={2},dg[32]={3}; return raya_pending_record_encode(ns,op,5,dg,state,state==RAYA_PENDING_PREPARED?0:6,out); }
static int setup(raya_pending_store_model*m,uint8_t rec[128],uint8_t old[256]) { raya_pending_model_init(m); if(!make_record(RAYA_PENDING_PREPARED,rec))return 0; if(!raya_pending_slot_encode(rec,1,old))return 0; return raya_pending_model_seed(m,0,old); }
int main(void) {
 raya_pending_store_model m; uint8_t rec[128],old[256],newrec[128],next[256],copy[256];
 unsigned pub,sel,fault; uint64_t rev; raya_pending_state st; raya_slot_select_result sr; raya_model_commit_result cr;
 C(setup(&m,rec,old)); C(make_record(RAYA_PENDING_WITNESS_COMMITTED,newrec)); pub=99; rev=99;
 cr=raya_pending_model_commit(&m,newrec,&pub,&rev); C(cr==RAYA_MODEL_COMMIT_OK&&pub==1&&rev==2);
 C(m.writes==1&&m.flushes==1&&m.reads==1); sr=raya_pending_model_recover(&m,&sel,&rev,&st); C(sr==RAYA_SLOT_SELECT_VALID&&sel==1&&rev==2&&st==RAYA_PENDING_WITNESS_COMMITTED);
 C(setup(&m,rec,old)); memcpy(copy,m.durable_slots[0],256); m.fault=RAYA_MODEL_FAULT_WRITE_PARTIAL; m.partial_bytes=73;
 cr=raya_pending_model_commit(&m,newrec,&pub,&rev); C(cr==RAYA_MODEL_COMMIT_UNCERTAIN); C(!memcmp(copy,m.durable_slots[0],256)); C(memcmp(copy,m.volatile_slots[0],256)==0);
 sr=raya_pending_slots_select((const uint8_t (*)[256])m.volatile_slots,&sel,&rev,&st); C(sr==RAYA_SLOT_SELECT_ERROR); raya_pending_model_power_loss(&m); C(!memcmp(copy,m.volatile_slots[0],256)); sr=raya_pending_model_recover(&m,&sel,&rev,&st); C(sr==RAYA_SLOT_SELECT_VALID&&sel==0&&rev==1);
 C(setup(&m,rec,old)); m.fault=RAYA_MODEL_FAULT_WRITE_PARTIAL_DURABLE; m.partial_bytes=73; pub=0xFFFFFFFFu; rev=0xFFFFFFFFFFFFFFFFull; cr=raya_pending_model_commit(&m,newrec,&pub,&rev); C(cr==RAYA_MODEL_COMMIT_UNCERTAIN); C(pub==0xFFFFFFFFu&&rev==0xFFFFFFFFFFFFFFFFull); C(!memcmp(old,m.durable_slots[0],256)); raya_pending_model_power_loss(&m); sr=raya_pending_model_recover(&m,&sel,&rev,&st); C(sr==RAYA_SLOT_SELECT_ERROR);
 C(setup(&m,rec,old)); m.fault=RAYA_MODEL_FAULT_WRITE_FAIL; cr=raya_pending_model_commit(&m,newrec,&pub,&rev); C(cr==RAYA_MODEL_COMMIT_UNCERTAIN&&m.flushes==0); sr=raya_pending_model_recover(&m,&sel,&rev,&st); C(sr==RAYA_SLOT_SELECT_VALID&&sel==0&&rev==1);
 C(setup(&m,rec,old)); m.fault=RAYA_MODEL_FAULT_FLUSH_FAIL_NOT_DURABLE; cr=raya_pending_model_commit(&m,newrec,&pub,&rev); C(cr==RAYA_MODEL_COMMIT_UNCERTAIN); raya_pending_model_power_loss(&m); sr=raya_pending_model_recover(&m,&sel,&rev,&st); C(sr==RAYA_SLOT_SELECT_VALID&&sel==0&&rev==1);
 C(setup(&m,rec,old)); m.fault=RAYA_MODEL_FAULT_FLUSH_FAIL_DURABLE; cr=raya_pending_model_commit(&m,newrec,&pub,&rev); C(cr==RAYA_MODEL_COMMIT_UNCERTAIN); raya_pending_model_power_loss(&m); sr=raya_pending_model_recover(&m,&sel,&rev,&st); C(sr==RAYA_SLOT_SELECT_VALID&&sel==1&&rev==2);
 C(setup(&m,rec,old)); m.fault=RAYA_MODEL_FAULT_READBACK_FAIL; cr=raya_pending_model_commit(&m,newrec,&pub,&rev); C(cr==RAYA_MODEL_COMMIT_UNCERTAIN); raya_pending_model_power_loss(&m); sr=raya_pending_model_recover(&m,&sel,&rev,&st); C(sr==RAYA_SLOT_SELECT_VALID&&sel==1&&rev==2);
 C(setup(&m,rec,old)); m.fault=RAYA_MODEL_FAULT_READBACK_MISMATCH; cr=raya_pending_model_commit(&m,newrec,&pub,&rev); C(cr==RAYA_MODEL_COMMIT_UNCERTAIN); raya_pending_model_power_loss(&m); sr=raya_pending_model_recover(&m,&sel,&rev,&st); C(sr==RAYA_SLOT_SELECT_VALID&&sel==1&&rev==2);
 C(setup(&m,rec,old));
 for(fault=1;fault<=6;fault++) { m.fault=(raya_model_fault)fault; m.partial_bytes=127; pub=0xFFFFFFFFu; rev=0xFFFFFFFFFFFFFFFFull; cr=raya_pending_model_commit(&m,newrec,&pub,&rev); C(cr==RAYA_MODEL_COMMIT_UNCERTAIN); C(pub==0xFFFFFFFFu&&rev==0xFFFFFFFFFFFFFFFFull); raya_pending_model_power_loss(&m); sr=raya_pending_model_recover(&m,&sel,&rev,&st); C(sr==RAYA_SLOT_SELECT_VALID||sr==RAYA_SLOT_SELECT_ERROR); m.fault=RAYA_MODEL_FAULT_NONE; }
 C(setup(&m,rec,old)); memcpy(m.durable_slots[1],old,256); memcpy(m.volatile_slots[1],old,256); C(raya_pending_model_recover(&m,&sel,&rev,&st)==RAYA_SLOT_SELECT_VALID); C(raya_pending_slot_encode(newrec,2,next)); memcpy(m.durable_slots[0],next,256); memcpy(m.volatile_slots[0],next,256); C(raya_pending_model_recover(&m,&sel,&rev,&st)==RAYA_SLOT_SELECT_VALID&&sel==0&&rev==2);
 C(setup(&m,rec,old)); memcpy(m.durable_slots[1],old,256); memcpy(m.volatile_slots[1],old,256); m.durable_slots[1][25]^=1; m.volatile_slots[1][25]^=1; C(raya_pending_model_recover(&m,&sel,&rev,&st)==RAYA_SLOT_SELECT_ERROR);
 C(setup(&m,rec,old)); memset(m.durable_slots[1],0,256); memset(m.volatile_slots[1],0,256); C(raya_pending_model_commit(&m,newrec,&pub,&rev)==RAYA_MODEL_COMMIT_OK); C(raya_pending_model_recover(&m,&sel,&rev,&st)==RAYA_SLOT_SELECT_VALID&&rev==2);
 C(setup(&m,rec,old)); C(raya_pending_model_seed(&m,1,old)); C(raya_pending_model_commit(&m,newrec,&pub,&rev)==RAYA_MODEL_COMMIT_OK); C(raya_pending_model_recover(&m,&sel,&rev,&st)==RAYA_SLOT_SELECT_VALID&&rev==2);
 C(raya_pending_model_commit(NULL,newrec,&pub,&rev)==RAYA_MODEL_COMMIT_INVALID);
 printf("PASS pending-slot-store-model-v1 assertions: %u\\n",tests); return 0;
}
