#include "generation-ledger-v2.h"
#include "generation-cng-v2.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{tests++;if(!(x)){printf("FAIL line %d\n",__LINE__);return 1;}}while(0)
typedef struct {uint8_t slot[2][32];int present[2];int fail_write,fail_flush,fail_read,corrupt_read,invalid_read_status;} memstore;
static int rd(void*c,unsigned s,uint8_t out[32]){memstore*m=(memstore*)c;if(m->fail_read)return -1;if(m->invalid_read_status)return 2;if(!m->present[s])return 0;memcpy(out,m->slot[s],32);if(m->corrupt_read)out[9]^=1;return 1;}
static int wr(void*c,unsigned s,const uint8_t in[32]){memstore*m=(memstore*)c;if(m->fail_write)return 0;memcpy(m->slot[s],in,32);m->present[s]=1;return 1;}
static int fl(void*c,unsigned s){memstore*m=(memstore*)c;(void)s;return !m->fail_flush;}
static unsigned tests;
static int rng_ok(void*c,uint8_t*out,unsigned n){unsigned i;(void)c;for(i=0;i<n;i++)out[i]=(uint8_t)(0xA0u+i);return 1;}
static int rng_fail(void*c,uint8_t*out,unsigned n){(void)c;(void)out;(void)n;return 0;}
int main(void){memstore m={0};raya_ledger_storage io={rd,wr,fl,&m};raya_generation_ledger l,r;uint64_t e=0;uint8_t rec[32];unsigned i;
 CHECK(raya_ledger_init_empty(&l,&io));CHECK(l.epoch==1);CHECK(raya_ledger_reserve_next(&l,&e)&&e==2);CHECK(raya_ledger_reserve_next(&l,&e)&&e==3);CHECK(raya_ledger_load(&r,&io)&&r.epoch==3);
 m.fail_write=1;CHECK(!raya_ledger_reserve_next(&r,&e));CHECK(r.state==RAYA_LEDGER_DEGRADED);m.fail_write=0;CHECK(raya_ledger_load(&r,&io)&&r.epoch==3);m.fail_read=1;CHECK(!raya_ledger_reserve_next(&r,&e));CHECK(r.state==RAYA_LEDGER_DEGRADED);m.fail_read=0;CHECK(raya_ledger_load(&r,&io)&&r.epoch==4);
 m.fail_flush=1;CHECK(!raya_ledger_reserve_next(&r,&e));CHECK(r.state==RAYA_LEDGER_DEGRADED);m.fail_flush=0;CHECK(raya_ledger_load(&r,&io)&&r.epoch==5);
 CHECK(raya_ledger_reserve_next(&r,&e)&&e==6);m.corrupt_read=1;CHECK(!raya_ledger_load(&l,&io));CHECK(l.state==RAYA_LEDGER_DEGRADED);m.corrupt_read=0;
 memset(&m,0,sizeof(m));io.context=&m;CHECK(!raya_ledger_load(&l,&io));
 CHECK(raya_ledger_init_empty(&l,&io));m.slot[1][0]='X';m.present[1]=1;CHECK(!raya_ledger_load(&r,&io));
 CHECK(raya_ledger_record_encode(99,rec));CHECK(raya_ledger_record_decode(rec,&e)&&e==99);rec[12]^=1;CHECK(!raya_ledger_record_decode(rec,&e));CHECK(!raya_ledger_record_encode(0,rec));
 memset(&m,0,sizeof(m));io.context=&m;CHECK(raya_ledger_init_empty(&l,&io));for(i=0;i<100;i++)CHECK(raya_ledger_reserve_next(&l,&e));CHECK(e==101);CHECK(raya_ledger_load(&r,&io)&&r.epoch==101);
 {uint8_t g[16];CHECK(raya_ledger_create_generation(&r,rng_ok,NULL,g));CHECK(g[7]==102&&g[8]==0xA0&&g[15]==0xA7);CHECK(r.epoch==102);memset(g,0xFF,sizeof(g));CHECK(!raya_ledger_create_generation(&r,rng_fail,NULL,g));CHECK(r.epoch==103);for(i=0;i<16;i++)CHECK(g[i]==0);CHECK(raya_ledger_create_generation(&r,rng_ok,NULL,g));CHECK(g[7]==104);CHECK(raya_ledger_create_generation(&r,raya_generation_cng_random,NULL,g));CHECK(g[7]==105);}

 memset(&m,0,sizeof(m));io.context=&m;CHECK(raya_ledger_record_encode(UINT64_MAX,m.slot[0]));m.present[0]=1;CHECK(raya_ledger_load(&r,&io));CHECK(!raya_ledger_reserve_next(&r,&e));CHECK(r.state==RAYA_LEDGER_DEGRADED);m.fail_read=1;CHECK(!raya_ledger_load(&l,&io));CHECK(l.state==RAYA_LEDGER_DEGRADED);m.fail_read=0;m.invalid_read_status=1;CHECK(!raya_ledger_load(&l,&io));CHECK(l.state==RAYA_LEDGER_DEGRADED);
 printf("PASS generation-ledger-v2 assertions: %u\n",tests);return 0;}








