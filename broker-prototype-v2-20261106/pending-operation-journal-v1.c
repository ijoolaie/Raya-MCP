#include "pending-operation-journal-v1.h"
#include <string.h>
static void put32(uint8_t*p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static uint32_t get32(const uint8_t*p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void put64(uint8_t*p,uint64_t v){unsigned i;for(i=0;i<8;i++)p[i]=(uint8_t)(v>>(56u-8u*i));}
static uint64_t get64(const uint8_t*p){uint64_t v=0;unsigned i;for(i=0;i<8;i++)v=(v<<8)|p[i];return v;}
static uint32_t crc32(const uint8_t*p,unsigned n){uint32_t c=0xFFFFFFFFu;unsigned i,j;for(i=0;i<n;i++){c^=p[i];for(j=0;j<8;j++)c=(c>>1)^((0u-(c&1u))&0xEDB88320u);}return ~c;}
static int nonzero(const uint8_t*p,unsigned n){unsigned i;uint8_t x=0;for(i=0;i<n;i++)x|=p[i];return x!=0;}
static int valid_state(raya_pending_state s){return s==RAYA_PENDING_PREPARED||s==RAYA_PENDING_WITNESS_COMMITTED||s==RAYA_PENDING_LOCAL_COMMITTED||s==RAYA_PENDING_ABORTED||s==RAYA_PENDING_TOMBSTONED;}
int raya_pending_record_encode(const uint8_t ns[16],const uint8_t op[16],uint64_t expected,const uint8_t digest[32],raya_pending_state state,uint64_t witness,uint8_t out[128]){
 uint64_t next;if(!ns||!op||!digest||!out||!nonzero(ns,16)||!nonzero(op,16)||!nonzero(digest,32)||!valid_state(state)||expected==UINT64_MAX)return 0;
 next=expected+1;if((state==RAYA_PENDING_PREPARED&&witness!=0)||(state!=RAYA_PENDING_PREPARED&&witness!=next))return 0;
 memset(out,0,128);memcpy(out,"RYPI",4);put32(out+4,1);put32(out+8,(uint32_t)state);memcpy(out+16,ns,16);memcpy(out+32,op,16);put64(out+48,expected);put64(out+56,next);memcpy(out+64,digest,32);put64(out+96,witness);put32(out+112,crc32(out,112));put32(out+116,~get32(out+112));return 1;
}
int raya_pending_record_decode(const uint8_t in[128],raya_pending_state*state,uint64_t*expected,uint64_t*next,uint64_t*witness){
 uint32_t s,c;uint64_t e,n,w;if(!in||!state||!expected||!next||!witness)return 0;
 if(memcmp(in,"RYPI",4)||get32(in+4)!=1||get32(in+12)!=0||!nonzero(in+16,16)||!nonzero(in+32,16)||!nonzero(in+64,32))return 0;
 s=get32(in+8);if(s<1||s>7)return 0;if(!valid_state((raya_pending_state)s))return 0;
 e=get64(in+48);n=get64(in+56);w=get64(in+96);if(e==UINT64_MAX||n!=e+1)return 0;
 if((s==RAYA_PENDING_PREPARED&&w!=0)||(s!=RAYA_PENDING_PREPARED&&w!=n))return 0;
 {unsigned i;for(i=104;i<112;i++)if(in[i])return 0;for(i=120;i<128;i++)if(in[i])return 0;}
 c=get32(in+112);if(c!=crc32(in,112)||get32(in+116)!=~c)return 0;
 *state=(raya_pending_state)s;*expected=e;*next=n;*witness=w;return 1;
}
void raya_pending_init(raya_pending_journal*j){if(j){memset(j,0,sizeof(*j));j->state=RAYA_PENDING_EMPTY;}}
int raya_pending_load(raya_pending_journal*j,const uint8_t rec[128]){raya_pending_state s;uint64_t e,n,w;if(!j||!rec)return 0;if(!raya_pending_record_decode(rec,&s,&e,&n,&w)){memcpy(j->record,rec,128);j->state=RAYA_PENDING_RECOVERY_REQUIRED;return 0;}memcpy(j->record,rec,128);j->state=(s==RAYA_PENDING_TOMBSTONED)?RAYA_PENDING_EMPTY:s;return 1;}
static int matches(const raya_pending_journal*j,const uint8_t ns[16],const uint8_t op[16],const uint8_t digest[32]){return j&&ns&&op&&digest&&memcmp(j->record+16,ns,16)==0&&memcmp(j->record+32,op,16)==0&&memcmp(j->record+64,digest,32)==0;}
int raya_pending_prepare(raya_pending_journal*j,const uint8_t ns[16],const uint8_t op[16],uint64_t expected,const uint8_t digest[32]){
 uint8_t b[128];if(!j||j->state!=RAYA_PENDING_EMPTY)return 0;if(!raya_pending_record_encode(ns,op,expected,digest,RAYA_PENDING_PREPARED,0,b))return 0;memcpy(j->record,b,128);j->state=RAYA_PENDING_PREPARED;return 1;
}
int raya_pending_mark_witness_committed(raya_pending_journal*j,const uint8_t ns[16],const uint8_t op[16],const uint8_t digest[32],uint64_t seq){
 uint64_t expected;if(!j||j->state!=RAYA_PENDING_PREPARED||!matches(j,ns,op,digest))return 0;expected=get64(j->record+48);if(expected==UINT64_MAX||seq!=expected+1)return 0;
 if(!raya_pending_record_encode(ns,op,expected,digest,RAYA_PENDING_WITNESS_COMMITTED,seq,j->record))return 0;j->state=RAYA_PENDING_WITNESS_COMMITTED;return 1;
}
int raya_pending_mark_local_committed(raya_pending_journal*j,const uint8_t op[16],const uint8_t digest[32],uint64_t seq){
 uint64_t expected;uint8_t ns[16];if(!j||j->state!=RAYA_PENDING_WITNESS_COMMITTED||!op||!digest||memcmp(j->record+32,op,16)||memcmp(j->record+64,digest,32))return 0;
 expected=get64(j->record+48);if(expected==UINT64_MAX||seq!=expected+1)return 0;memcpy(ns,j->record+16,16);
 if(!raya_pending_record_encode(ns,op,expected,digest,RAYA_PENDING_LOCAL_COMMITTED,seq,j->record))return 0;j->state=RAYA_PENDING_LOCAL_COMMITTED;return 1;
}
int raya_pending_finalize(raya_pending_journal*j,const uint8_t op[16]){if(!j||j->state!=RAYA_PENDING_LOCAL_COMMITTED||!op||memcmp(j->record+32,op,16))return 0;raya_pending_init(j);return 1;}
static int persist_candidate(raya_pending_journal*j,const raya_pending_store_ops*store,const raya_pending_journal*candidate){
 uint8_t verify[128];
 if(!j||!store||!store->write_record||!store->flush||!store->readback||!candidate)return 0;
 if(!store->write_record(store->context,candidate->record)||!store->flush(store->context)||
    !store->readback(store->context,verify)||memcmp(verify,candidate->record,128)!=0){
  memcpy(j->record,candidate->record,128);j->state=RAYA_PENDING_DEGRADED;return 0;
 }
 memcpy(j->record,candidate->record,128);j->state=candidate->state;return 1;
}
int raya_pending_store_prepare(raya_pending_journal*j,const raya_pending_store_ops*s,const uint8_t ns[16],const uint8_t op[16],uint64_t expected,const uint8_t digest[32]){
 raya_pending_journal candidate;if(!j||!s||j->state!=RAYA_PENDING_EMPTY)return 0;candidate=*j;
 if(!raya_pending_prepare(&candidate,ns,op,expected,digest))return 0;return persist_candidate(j,s,&candidate);
}
int raya_pending_store_mark_witness_committed(raya_pending_journal*j,const raya_pending_store_ops*s,const uint8_t ns[16],const uint8_t op[16],const uint8_t digest[32],uint64_t seq){
 raya_pending_journal candidate;if(!j||!s||j->state!=RAYA_PENDING_PREPARED)return 0;candidate=*j;
 if(!raya_pending_mark_witness_committed(&candidate,ns,op,digest,seq))return 0;return persist_candidate(j,s,&candidate);
}
int raya_pending_store_mark_local_committed(raya_pending_journal*j,const raya_pending_store_ops*s,const uint8_t op[16],const uint8_t digest[32],uint64_t seq){
 raya_pending_journal candidate;if(!j||!s||j->state!=RAYA_PENDING_WITNESS_COMMITTED)return 0;candidate=*j;
 if(!raya_pending_mark_local_committed(&candidate,op,digest,seq))return 0;return persist_candidate(j,s,&candidate);
}
int raya_pending_store_finalize(raya_pending_journal*j,const raya_pending_store_ops*s,const uint8_t op[16]){
 raya_pending_journal candidate;raya_pending_state decoded;uint64_t expected,next,witness;
 if(!j||!s||j->state!=RAYA_PENDING_LOCAL_COMMITTED||!op||memcmp(j->record+32,op,16))return 0;
 if(!raya_pending_record_decode(j->record,&decoded,&expected,&next,&witness)||decoded!=RAYA_PENDING_LOCAL_COMMITTED)return 0;
 candidate=*j;
 if(!raya_pending_record_encode(j->record+16,j->record+32,expected,j->record+64,RAYA_PENDING_TOMBSTONED,witness,candidate.record))return 0;
 candidate.state=RAYA_PENDING_TOMBSTONED;
 if(!persist_candidate(j,s,&candidate))return 0;
 j->state=RAYA_PENDING_EMPTY;
 return 1;
}
raya_pending_recovery raya_pending_classify(const raya_pending_journal*j,raya_witness_status ws,uint64_t witness,uint64_t local,int exact){
 raya_pending_state decoded;uint64_t e,n,w;
 if(!j)return RAYA_RECOVERY_REQUIRED;
 if(j->state==RAYA_PENDING_EMPTY){
  if(ws!=RAYA_WITNESS_COMMITTED)return RAYA_RECOVERY_REQUIRED;
  return local==witness?RAYA_RECOVERY_READY:(local<witness?RAYA_RECOVERY_ROLLBACK_SUSPECTED:RAYA_RECOVERY_REQUIRED);
 }
 if(j->state<RAYA_PENDING_PREPARED||j->state>RAYA_PENDING_LOCAL_COMMITTED)return RAYA_RECOVERY_REQUIRED;
 if(!raya_pending_record_decode(j->record,&decoded,&e,&n,&w)||decoded!=j->state)return RAYA_RECOVERY_REQUIRED;
 if(!exact)return RAYA_RECOVERY_SECURITY_FAILURE;
 if(j->state==RAYA_PENDING_PREPARED){
  if(ws==RAYA_WITNESS_COMMITTED&&witness==n&&(local==e||local==n))return RAYA_RECOVERY_MARK_WITNESS_COMMITTED;
  if(ws==RAYA_WITNESS_NOT_FOUND&&local==e)return RAYA_RECOVERY_RETRY_EXACT;
  return ws==RAYA_WITNESS_CONFLICT?RAYA_RECOVERY_SECURITY_FAILURE:RAYA_RECOVERY_REQUIRED;
 }
 if(ws==RAYA_WITNESS_UNKNOWN)return RAYA_RECOVERY_REQUIRED;
 if(ws!=RAYA_WITNESS_COMMITTED||witness!=n)return RAYA_RECOVERY_SECURITY_FAILURE;
 if(j->state==RAYA_PENDING_WITNESS_COMMITTED){if(local==e)return RAYA_RECOVERY_COMMIT_LOCAL;if(local==n)return RAYA_RECOVERY_MARK_LOCAL_COMMITTED;return RAYA_RECOVERY_REQUIRED;}
 return local==n?RAYA_RECOVERY_FINALIZE:RAYA_RECOVERY_REQUIRED;
}
