#define WIN32_LEAN_AND_MEAN
#include "dedicated-io-submitter-v2.h"
#include <stdio.h>
#define NPROD 8
#define EACH 10
#define TOTAL (NPROD*EACH)
#define CHECK(x) do { ++checks; if (!(x)) { (void)fprintf(stderr,"FAIL line %d: %s err=%lu\n",__LINE__,#x,(unsigned long)GetLastError()); return 1; } } while(0)
static unsigned checks;
typedef struct { HANDLE sem; CRITICAL_SECTION lock; raya_submitter_request *done[TOTAL]; size_t head,tail,count; LONG issued,published,acked; } ctx;
typedef struct {raya_dedicated_submitter*s;ctx*c;raya_submitter_request*requests;HANDLE start;LONG errors;} prod_arg;
static raya_submitter_issue_result issue(raya_submitter_request*r,void*p){
 ctx*c=(ctx*)p;(void)r;InterlockedIncrement(&c->issued);return RAYA_SUBMITTER_ISSUE_ACCEPTED;
}
static int publish(raya_submitter_request*r,void*p){
 ctx*c=(ctx*)p;int ok=1;
 EnterCriticalSection(&c->lock);
 if(c->count>=TOTAL)ok=0;
 else {c->done[c->tail]=r;c->tail=(c->tail+1)%TOTAL;++c->count;InterlockedIncrement(&c->published);}
 LeaveCriticalSection(&c->lock);
 if(!ok)return 0;
 return ReleaseSemaphore(c->sem,1,NULL)!=0;
}
static void not_issued(raya_submitter_request*r,void*p){(void)r;(void)p;}
static DWORD WINAPI producer(void*p){
 prod_arg*a=(prod_arg*)p;
 if(WaitForSingleObject(a->start,5000)!=WAIT_OBJECT_0)return 10;
 for(int i=0;i<EACH;i++){
  raya_submitter_request*r=&a->requests[i];
  if(!raya_submitter_admit(a->s,r,(void*)(size_t)(i+1))){InterlockedIncrement(&a->errors);continue;}
  for(;;){
   raya_submitter_result z=raya_submitter_enqueue(a->s,r);
   if(z==RAYA_SUBMITTER_OK)break;
   if(z==RAYA_SUBMITTER_BACKPRESSURE){Sleep(1);continue;}
   InterlockedIncrement(&a->errors);(void)raya_submitter_abort_admission(a->s,r);break;
  }
 }
 return 0;
}
typedef struct {raya_dedicated_submitter*s;ctx*c;} ack_arg;
static DWORD WINAPI acknowledger(void*p){
 ack_arg*a=(ack_arg*)p;ctx*c=a->c;
 for(int i=0;i<TOTAL;i++){
  if(WaitForSingleObject(c->sem,10000)!=WAIT_OBJECT_0)return 20;
  raya_submitter_request*r=NULL;
  EnterCriticalSection(&c->lock);
  if(c->count){r=c->done[c->head];c->head=(c->head+1)%TOTAL;--c->count;}
  LeaveCriticalSection(&c->lock);
  if(r==NULL||!raya_submitter_acknowledge(a->s,r))return 21;
  InterlockedIncrement(&c->acked);
 }
 return 0;
}
int main(void){
 raya_dedicated_submitter s;ctx c;prod_arg pa[NPROD];ack_arg aa;
 raya_submitter_request req[NPROD][EACH];HANDLE start,prods[NPROD],ack;
 DWORD ec; raya_submitter_lifecycle life;size_t q,pd,ac,ls;
 ZeroMemory(&s,sizeof(s));ZeroMemory(&c,sizeof(c));ZeroMemory(req,sizeof(req));
 InitializeCriticalSection(&c.lock);c.sem=CreateSemaphoreW(NULL,0,TOTAL,NULL);
 start=CreateEventW(NULL,TRUE,FALSE,NULL);
 CHECK(c.sem&&start);
 CHECK(raya_submitter_start(&s,TOTAL,issue,publish,not_issued,&c));
 aa.s=&s;aa.c=&c;ack=CreateThread(NULL,0,acknowledger,&aa,0,NULL);CHECK(ack!=NULL);
 for(int i=0;i<NPROD;i++){
  pa[i].s=&s;pa[i].c=&c;pa[i].requests=req[i];pa[i].start=start;pa[i].errors=0;
  prods[i]=CreateThread(NULL,0,producer,&pa[i],0,NULL);CHECK(prods[i]!=NULL);
 }
 CHECK(SetEvent(start));
 for(int i=0;i<NPROD;i++){CHECK(WaitForSingleObject(prods[i],10000)==WAIT_OBJECT_0);CHECK(GetExitCodeThread(prods[i],&ec)&&ec==0);CHECK(CloseHandle(prods[i]));CHECK(pa[i].errors==0);}
 CHECK(raya_submitter_shutdown(&s));
 CHECK(WaitForSingleObject(ack,15000)==WAIT_OBJECT_0);CHECK(GetExitCodeThread(ack,&ec)&&ec==0);CHECK(CloseHandle(ack));
 CHECK(raya_submitter_join(&s,10000));
 CHECK(InterlockedCompareExchange(&c.issued,0,0)==TOTAL);
 CHECK(InterlockedCompareExchange(&c.published,0,0)==TOTAL);
 CHECK(InterlockedCompareExchange(&c.acked,0,0)==TOTAL);
 CHECK(raya_submitter_snapshot(&s,&life,&q,&pd,&ac,&ls));
 CHECK(life==RAYA_SUBMITTER_STOPPED&&q==0&&pd==0&&ac==0&&ls==0);
 for(int i=0;i<TOTAL;i++)CHECK(req[i/EACH][i%EACH].state==RAYA_SUBMITTER_REQ_ACKED);
 CHECK(raya_submitter_destroy(&s));
 CHECK(CloseHandle(start));CHECK(CloseHandle(c.sem));DeleteCriticalSection(&c.lock);
 (void)printf("Submitter multi-producer: %u assertions PASS\n",checks);return 0;
}
