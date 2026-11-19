#include "generation-ledger-v2.h"
#include <string.h>
static void put64(uint8_t*p,uint64_t v){unsigned i;for(i=0;i<8;i++)p[i]=(uint8_t)(v>>(56u-8u*i));}
static uint64_t get64(const uint8_t*p){uint64_t v=0;unsigned i;for(i=0;i<8;i++)v=(v<<8)|p[i];return v;}
static uint32_t crc32(const uint8_t*p,unsigned n){uint32_t c=0xFFFFFFFFu;unsigned i,j;for(i=0;i<n;i++){c^=p[i];for(j=0;j<8;j++)c=(c>>1)^((0u-(c&1u))&0xEDB88320u);}return ~c;}
static void put32(uint8_t*p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static uint32_t get32(const uint8_t*p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
int raya_ledger_record_encode(uint64_t e,uint8_t out[32]){if(!e||!out)return 0;memset(out,0,32);memcpy(out,"RYGL",4);put32(out+4,2);put64(out+8,e);put64(out+16,e);put32(out+24,crc32(out,24));put32(out+28,~get32(out+24));return 1;}
int raya_ledger_record_decode(const uint8_t in[32],uint64_t*out){uint64_t s,e;uint32_t c;if(!in||!out||memcmp(in,"RYGL",4)||get32(in+4)!=2)return 0;s=get64(in+8);e=get64(in+16);c=get32(in+24);if(!s||s!=e||c!=crc32(in,24)||get32(in+28)!=~c)return 0;*out=e;return 1;}
static int storage_ok(const raya_ledger_storage*s){return s&&s->read_slot&&s->write_slot&&s->flush_slot;}
int raya_ledger_init_empty(raya_generation_ledger*l,const raya_ledger_storage*s){uint8_t b[32],v[32];if(!l||!storage_ok(s))return 0;memset(l,0,sizeof(*l));l->storage=*s;{int a=s->read_slot(s->context,0,b),d=s->read_slot(s->context,1,v);if(a!=0||d!=0){l->state=RAYA_LEDGER_DEGRADED;return 0;}}if(!raya_ledger_record_encode(1,b)||s->write_slot(s->context,0,b)!=1||s->flush_slot(s->context,0)!=1||s->read_slot(s->context,0,v)!=1||memcmp(b,v,32)){l->state=RAYA_LEDGER_DEGRADED;return 0;}l->epoch=1;l->active_slot=0;l->state=RAYA_LEDGER_READY;return 1;}
int raya_ledger_load(raya_generation_ledger*l,const raya_ledger_storage*s){uint8_t b[2][32];uint64_t e[2]={0,0};int r[2],v[2];unsigned i;if(!l||!storage_ok(s))return 0;memset(l,0,sizeof(*l));l->storage=*s;for(i=0;i<2;i++){r[i]=s->read_slot(s->context,i,b[i]);if(r[i]<0||r[i]>1){l->state=RAYA_LEDGER_DEGRADED;return 0;}v[i]=(r[i]==1&&raya_ledger_record_decode(b[i],&e[i]));if(r[i]==1&&!v[i]){l->state=RAYA_LEDGER_DEGRADED;return 0;}}if(!v[0]&&!v[1]){l->state=RAYA_LEDGER_DEGRADED;return 0;}if(v[0]&&v[1]&&e[0]==e[1]&&memcmp(b[0],b[1],32)){l->state=RAYA_LEDGER_DEGRADED;return 0;}l->active_slot=(v[1]&&(!v[0]||e[1]>e[0]))?1u:0u;l->epoch=e[l->active_slot];l->state=RAYA_LEDGER_READY;return 1;}
int raya_ledger_reserve_next(raya_generation_ledger*l,uint64_t*out){uint8_t b[32],v[32];unsigned slot;uint64_t next,check;if(!l||!out||l->state!=RAYA_LEDGER_READY)return 0;if(l->epoch==UINT64_MAX){l->state=RAYA_LEDGER_DEGRADED;return 0;}next=l->epoch+1;slot=1u-l->active_slot;if(!raya_ledger_record_encode(next,b)||l->storage.write_slot(l->storage.context,slot,b)!=1||l->storage.flush_slot(l->storage.context,slot)!=1||l->storage.read_slot(l->storage.context,slot,v)!=1||memcmp(b,v,32)||!raya_ledger_record_decode(v,&check)||check!=next){l->state=RAYA_LEDGER_DEGRADED;return 0;}l->epoch=next;l->active_slot=slot;*out=next;return 1;}


int raya_ledger_create_generation(raya_generation_ledger*l,raya_generation_random_fn random_fn,void*context,uint8_t out[16]){uint64_t epoch;if(!out)return 0;memset(out,0,16);if(!l||!random_fn)return 0;if(!raya_ledger_reserve_next(l,&epoch))return 0;put64(out,epoch);if(random_fn(context,out+8,8)!=1){memset(out,0,16);return 0;}return 1;}


