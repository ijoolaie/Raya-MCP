#define WIN32_LEAN_AND_MEAN
#include "dedicated-io-submitter-v2.h"
#include <stdio.h>
#define CHECK(x) do { ++checks; if (!(x)) { (void)fprintf(stderr,"FAIL line %d: %s err=%lu\n",__LINE__,#x,(unsigned long)GetLastError()); return 1; } } while(0)
static unsigned checks;
typedef struct { HANDLE entered, release, published; LONG issues, rejected; } ctx;
static raya_submitter_issue_result issue(raya_submitter_request *r, void *p) {
 ctx *c=(ctx*)p; InterlockedIncrement(&c->issues);
 if (r->item==(void*)1) {
  if (!SetEvent(c->entered) || WaitForSingleObject(c->release,5000)!=WAIT_OBJECT_0)
   return RAYA_SUBMITTER_ISSUE_UNCERTAIN;
  return RAYA_SUBMITTER_ISSUE_NOT_ACCEPTED;
 }
 return RAYA_SUBMITTER_ISSUE_ACCEPTED;
}
static int publish(raya_submitter_request *r, void *p) {
 ctx *c=(ctx*)p; (void)r; return SetEvent(c->published)!=0;
}
static void not_issued(raya_submitter_request *r, void *p) {
 ctx *c=(ctx*)p; (void)r; InterlockedIncrement(&c->rejected);
}
typedef struct { raya_dedicated_submitter *s; raya_submitter_request *r; HANDLE published; int result; } ack_arg;
static DWORD WINAPI ack_thread(void *p) {
 ack_arg *a=(ack_arg*)p;
 if (WaitForSingleObject(a->published,5000)!=WAIT_OBJECT_0) return 20;
 a->result=raya_submitter_acknowledge(a->s,a->r);
 return a->result?0:21;
}
int main(void) {
 raya_dedicated_submitter s; raya_submitter_request r1,r2,r3; ctx c;
 ack_arg a; HANDLE ack; DWORD ec=99; raya_submitter_lifecycle life;
 size_t q,producers,active,leases;
 ZeroMemory(&s,sizeof(s)); ZeroMemory(&r1,sizeof(r1)); ZeroMemory(&r2,sizeof(r2));
 ZeroMemory(&r3,sizeof(r3)); ZeroMemory(&c,sizeof(c));
 c.entered=CreateEventW(NULL,TRUE,FALSE,NULL);
 c.release=CreateEventW(NULL,TRUE,FALSE,NULL);
 c.published=CreateEventW(NULL,TRUE,FALSE,NULL);
 CHECK(c.entered&&c.release&&c.published);
 CHECK(raya_submitter_start(&s,1,issue,publish,not_issued,&c));
 CHECK(raya_submitter_admit(&s,&r1,(void*)1));
 CHECK(raya_submitter_enqueue(&s,&r1)==RAYA_SUBMITTER_OK);
 CHECK(WaitForSingleObject(c.entered,5000)==WAIT_OBJECT_0);
 CHECK(raya_submitter_admit(&s,&r2,(void*)2));
 CHECK(raya_submitter_enqueue(&s,&r2)==RAYA_SUBMITTER_OK);
 CHECK(raya_submitter_admit(&s,&r3,(void*)3));
 CHECK(raya_submitter_enqueue(&s,&r3)==RAYA_SUBMITTER_BACKPRESSURE);
 CHECK(raya_submitter_abort_admission(&s,&r3));
 CHECK(raya_submitter_shutdown(&s));
 CHECK(SetEvent(c.release));
 a.s=&s; a.r=&r2; a.published=c.published; a.result=0;
 ack=CreateThread(NULL,0,ack_thread,&a,0,NULL); CHECK(ack!=NULL);
 CHECK(WaitForSingleObject(ack,5000)==WAIT_OBJECT_0);
 CHECK(GetExitCodeThread(ack,&ec)&&ec==0);
 CHECK(CloseHandle(ack));
 CHECK(raya_submitter_join(&s,5000));
 CHECK(r1.state==RAYA_SUBMITTER_REQ_NOT_ISSUED);
 CHECK(r2.state==RAYA_SUBMITTER_REQ_ACKED);
 CHECK(r3.state==RAYA_SUBMITTER_REQ_NOT_ISSUED);
 CHECK(InterlockedCompareExchange(&c.issues,0,0)==2);
 CHECK(InterlockedCompareExchange(&c.rejected,0,0)==1);
 CHECK(raya_submitter_snapshot(&s,&life,&q,&producers,&active,&leases));
 CHECK(life==RAYA_SUBMITTER_STOPPED&&q==0&&producers==0&&active==0&&leases==0);
 CHECK(raya_submitter_shutdown(&s));
 CHECK(raya_submitter_destroy(&s));
 CHECK(CloseHandle(c.entered)); CHECK(CloseHandle(c.release)); CHECK(CloseHandle(c.published));
 (void)printf("Dedicated Submitter backpressure: %u assertions PASS\n",checks); return 0;
}
