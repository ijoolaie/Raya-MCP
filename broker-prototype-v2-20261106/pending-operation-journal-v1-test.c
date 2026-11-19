#include "pending-operation-journal-v1.h"
#include <stdio.h>
#include <string.h>
#define C(x) do{tests++;if(!(x)){printf("FAIL line %d\n",__LINE__);return 1;}}while(0)
static unsigned tests;
typedef struct { unsigned calls,fail_at; int mismatch; uint8_t stored[128]; } mock_store;
static int ms_write(void*x,const uint8_t*r){mock_store*m=(mock_store*)x;m->calls++;if(m->calls==m->fail_at)return 0;memcpy(m->stored,r,128);return 1;}
static int ms_flush(void*x){mock_store*m=(mock_store*)x;m->calls++;return m->calls!=m->fail_at;}
static int ms_readback(void*x,uint8_t*r){mock_store*m=(mock_store*)x;m->calls++;if(m->calls==m->fail_at)return 0;memcpy(r,m->stored,128);if(m->mismatch)r[20]^=1;return 1;}
int main(void){
 uint8_t ns[16]={1},op[16]={2},dg[32]={3},bad[32]={4},op2[16]={5},dg2[32]={6},rec[128];raya_pending_journal j,k;raya_pending_state s;uint64_t e,n,w;unsigned i;
 raya_pending_init(&j);C(j.state==RAYA_PENDING_EMPTY);
 C(raya_pending_record_encode(ns,op,7,dg,RAYA_PENDING_PREPARED,0,rec));
 C(!memcmp(rec,"RYPI",4));C(rec[7]==1);C(rec[55]==7&&rec[63]==8);
 C(raya_pending_record_decode(rec,&s,&e,&n,&w));C(s==RAYA_PENDING_PREPARED&&e==7&&n==8&&w==0);
 {uint8_t corrupt[128];memcpy(corrupt,rec,128);corrupt[0]^=1;raya_pending_init(&k);C(!raya_pending_load(&k,corrupt));C(k.state==RAYA_PENDING_RECOVERY_REQUIRED);C(memcmp(k.record,corrupt,128)==0);}
 C(raya_pending_load(&k,rec));C(k.state==RAYA_PENDING_PREPARED);
 C(!raya_pending_prepare(&k,ns,op,7,dg));
 C(!raya_pending_mark_local_committed(&k,op,dg,8));
 C(!raya_pending_mark_witness_committed(&k,ns,op,bad,8));
 C(!raya_pending_mark_witness_committed(&k,ns,op,dg,9));
 C(raya_pending_mark_witness_committed(&k,ns,op,dg,8));C(k.state==RAYA_PENDING_WITNESS_COMMITTED);
 C(!raya_pending_mark_witness_committed(&k,ns,op,dg,8));
 C(!raya_pending_mark_local_committed(&k,op,bad,8));
 C(!raya_pending_mark_local_committed(&k,op,dg,7));
 C(raya_pending_mark_local_committed(&k,op,dg,8));C(k.state==RAYA_PENDING_LOCAL_COMMITTED);
 C(!raya_pending_mark_local_committed(&k,op,dg,8));
 C(!raya_pending_finalize(&k,bad));C(raya_pending_finalize(&k,op));C(k.state==RAYA_PENDING_EMPTY);
 C(raya_pending_prepare(&j,ns,op,7,dg));
 C(raya_pending_classify(&j,RAYA_WITNESS_NOT_FOUND,7,7,1)==RAYA_RECOVERY_RETRY_EXACT);
 C(raya_pending_classify(&j,RAYA_WITNESS_COMMITTED,8,7,1)==RAYA_RECOVERY_MARK_WITNESS_COMMITTED);
 C(raya_pending_classify(&j,RAYA_WITNESS_COMMITTED,8,8,1)==RAYA_RECOVERY_MARK_WITNESS_COMMITTED);
 C(raya_pending_mark_witness_committed(&j,ns,op,dg,8));
 C(raya_pending_classify(&j,RAYA_WITNESS_COMMITTED,8,7,1)==RAYA_RECOVERY_COMMIT_LOCAL);
 C(raya_pending_classify(&j,RAYA_WITNESS_COMMITTED,8,8,1)==RAYA_RECOVERY_MARK_LOCAL_COMMITTED);
 C(raya_pending_classify(&j,RAYA_WITNESS_UNKNOWN,0,7,1)==RAYA_RECOVERY_REQUIRED);
 C(raya_pending_classify(&j,RAYA_WITNESS_COMMITTED,8,7,0)==RAYA_RECOVERY_SECURITY_FAILURE);
 C(raya_pending_classify(&j,RAYA_WITNESS_CONFLICT,0,7,1)==RAYA_RECOVERY_SECURITY_FAILURE);
 C(raya_pending_classify(&j,RAYA_WITNESS_COMMITTED,9,7,1)==RAYA_RECOVERY_SECURITY_FAILURE);
 C(raya_pending_classify(&j,RAYA_WITNESS_COMMITTED,8,8,1)==RAYA_RECOVERY_MARK_LOCAL_COMMITTED);
 C(!raya_pending_record_encode(ns,op,UINT64_MAX,dg,RAYA_PENDING_PREPARED,0,rec));
 memset(ns,0,16);C(!raya_pending_record_encode(ns,op,7,dg,RAYA_PENDING_PREPARED,0,rec));ns[0]=1;
 memset(op,0,16);C(!raya_pending_record_encode(ns,op,7,dg,RAYA_PENDING_PREPARED,0,rec));op[0]=2;
 memset(dg,0,32);C(!raya_pending_record_encode(ns,op,7,dg,RAYA_PENDING_PREPARED,0,rec));dg[0]=3;
 C(raya_pending_record_encode(ns,op,7,dg,RAYA_PENDING_PREPARED,0,rec));
 for(i=0;i<128;i++){uint8_t t[128];memcpy(t,rec,128);t[i]^=1;C(!raya_pending_record_decode(t,&s,&e,&n,&w));}
 C(!raya_pending_record_encode(ns,op,7,dg,RAYA_PENDING_PREPARED,8,rec));
 C(!raya_pending_record_encode(ns,op,7,dg,RAYA_PENDING_LOCAL_COMMITTED,0,rec));
 C(raya_pending_record_encode(ns,op,7,dg,RAYA_PENDING_LOCAL_COMMITTED,8,rec));
 C(raya_pending_record_decode(rec,&s,&e,&n,&w)&&s==RAYA_PENDING_LOCAL_COMMITTED&&w==8);
 C(raya_pending_classify(&k,RAYA_WITNESS_UNKNOWN,8,8,1)==RAYA_RECOVERY_REQUIRED);
 C(raya_pending_classify(&k,RAYA_WITNESS_NOT_FOUND,8,8,1)==RAYA_RECOVERY_REQUIRED);
 C(raya_pending_classify(&k,RAYA_WITNESS_COMMITTED,8,8,1)==RAYA_RECOVERY_READY);
 C(raya_pending_classify(&k,RAYA_WITNESS_COMMITTED,8,7,1)==RAYA_RECOVERY_ROLLBACK_SUSPECTED);
 C(raya_pending_classify(&k,RAYA_WITNESS_COMMITTED,8,8,1)==RAYA_RECOVERY_READY);
 C(raya_pending_classify(&k,RAYA_WITNESS_COMMITTED,8,9,1)==RAYA_RECOVERY_REQUIRED);
 {
  unsigned f;
  for(f=1;f<=3;f++){mock_store m; raya_pending_store_ops so; raya_pending_journal sj;memset(&m,0,sizeof(m));m.fail_at=f;so.write_record=ms_write;so.flush=ms_flush;so.readback=ms_readback;so.context=&m;raya_pending_init(&sj);C(!raya_pending_store_prepare(&sj,&so,ns,op,7,dg));C(sj.state==RAYA_PENDING_DEGRADED);C(m.calls==f);C(raya_pending_record_decode(sj.record,&s,&e,&n,&w)&&s==RAYA_PENDING_PREPARED);}
  {mock_store m;raya_pending_store_ops so;raya_pending_journal sj;memset(&m,0,sizeof(m));m.mismatch=1;so.write_record=ms_write;so.flush=ms_flush;so.readback=ms_readback;so.context=&m;raya_pending_init(&sj);C(!raya_pending_store_prepare(&sj,&so,ns,op,7,dg));C(sj.state==RAYA_PENDING_DEGRADED);C(m.calls==3);}
  {mock_store m;raya_pending_store_ops so;raya_pending_journal sj;memset(&m,0,sizeof(m));so.write_record=ms_write;so.flush=ms_flush;so.readback=ms_readback;so.context=&m;raya_pending_init(&sj);C(raya_pending_store_prepare(&sj,&so,ns,op,7,dg));C(sj.state==RAYA_PENDING_PREPARED);C(!memcmp(m.stored,sj.record,128));C(raya_pending_store_mark_witness_committed(&sj,&so,ns,op,dg,8));C(sj.state==RAYA_PENDING_WITNESS_COMMITTED);C(raya_pending_store_mark_local_committed(&sj,&so,op,dg,8));C(sj.state==RAYA_PENDING_LOCAL_COMMITTED);C(!memcmp(m.stored,sj.record,128));C(m.calls==9);C(raya_pending_store_finalize(&sj,&so,op));C(sj.state==RAYA_PENDING_EMPTY);C(m.calls==12);C(raya_pending_record_decode(m.stored,&s,&e,&n,&w)&&s==RAYA_PENDING_TOMBSTONED&&w==8);raya_pending_init(&k);C(raya_pending_load(&k,m.stored));C(k.state==RAYA_PENDING_EMPTY);C(!memcmp(k.record,m.stored,128));C(raya_pending_store_prepare(&k,&so,ns,op2,8,dg2));C(k.state==RAYA_PENDING_PREPARED);C(m.calls==15);C(raya_pending_record_decode(m.stored,&s,&e,&n,&w)&&s==RAYA_PENDING_PREPARED&&e==8&&n==9);}
  {unsigned g;for(g=1;g<=3;g++){mock_store m;raya_pending_store_ops so;raya_pending_journal sj;memset(&m,0,sizeof(m));so.write_record=ms_write;so.flush=ms_flush;so.readback=ms_readback;so.context=&m;raya_pending_init(&sj);C(raya_pending_prepare(&sj,ns,op,7,dg));C(raya_pending_mark_witness_committed(&sj,ns,op,dg,8));C(raya_pending_mark_local_committed(&sj,op,dg,8));memcpy(m.stored,sj.record,128);m.fail_at=g;C(!raya_pending_store_finalize(&sj,&so,op));C(sj.state==RAYA_PENDING_DEGRADED);C(raya_pending_record_decode(sj.record,&s,&e,&n,&w)&&s==RAYA_PENDING_TOMBSTONED);C(m.calls==g);}}
 }
 printf("PASS pending-operation-journal-v1 assertions: %u\n",tests);return 0;
}
