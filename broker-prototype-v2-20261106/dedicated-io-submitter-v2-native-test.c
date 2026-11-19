#define WIN32_LEAN_AND_MEAN
#include "dedicated-io-submitter-v2.h"
#include <stdio.h>
#define CHECK(x) do { ++checks; if (!(x)) { (void)fprintf(stderr,"FAIL line %d: %s err=%lu\n",__LINE__,#x,(unsigned long)GetLastError()); return 1; } } while(0)
static unsigned checks;
typedef struct { HANDLE entered, publish; LONG issued, rejected; } ctx;
static raya_submitter_issue_result issue(raya_submitter_request *r, void *p) {
 ctx *c=(ctx*)p; (void)r; InterlockedIncrement(&c->issued);
 if (!SetEvent(c->entered)) return RAYA_SUBMITTER_ISSUE_UNCERTAIN;
 return RAYA_SUBMITTER_ISSUE_ACCEPTED;
}
static int publish(raya_submitter_request *r, void *p) {
 ctx *c=(ctx*)p; (void)r; return SetEvent(c->publish)!=0;
}
static void not_issued(raya_submitter_request *r, void *p) {
 ctx *c=(ctx*)p; (void)r; InterlockedIncrement(&c->rejected);
}
typedef struct { raya_dedicated_submitter *s; raya_submitter_request *r; HANDLE done; int result; } ack_arg;
static DWORD WINAPI ack_thread(void *p) {
 ack_arg *a=(ack_arg*)p;
 if (WaitForSingleObject(a->done,5000)!=WAIT_OBJECT_0) return 20;
 a->result=raya_submitter_acknowledge(a->s,a->r);
 return a->result?0:21;
}
int main(void) {
 raya_dedicated_submitter s; raya_submitter_request r; ctx c;
 HANDLE ack; ack_arg a; DWORD ec=99; raya_submitter_lifecycle life;
 size_t q=99,producers=99,active=99,leases=99;
 ZeroMemory(&s,sizeof(s)); ZeroMemory(&r,sizeof(r)); ZeroMemory(&c,sizeof(c));
 c.entered=CreateEventW(NULL,TRUE,FALSE,NULL); c.publish=CreateEventW(NULL,TRUE,FALSE,NULL);
 CHECK(c.entered&&c.publish);
 CHECK(raya_submitter_start(&s,2,issue,publish,not_issued,&c));
 CHECK(raya_submitter_admit(&s,&r,(void*)1));
 CHECK(raya_submitter_enqueue(&s,&r)==RAYA_SUBMITTER_OK);
 CHECK(WaitForSingleObject(c.entered,5000)==WAIT_OBJECT_0);
 CHECK(WaitForSingleObject(c.publish,5000)==WAIT_OBJECT_0);
 a.s=&s; a.r=&r; a.done=c.publish; a.result=0;
 ack=CreateThread(NULL,0,ack_thread,&a,0,NULL); CHECK(ack!=NULL);
 CHECK(raya_submitter_shutdown(&s));
 CHECK(WaitForSingleObject(ack,5000)==WAIT_OBJECT_0);
 CHECK(GetExitCodeThread(ack,&ec)&&ec==0);
 CHECK(CloseHandle(ack));
 CHECK(r.state==RAYA_SUBMITTER_REQ_ACKED);
 CHECK(raya_submitter_join(&s,5000));
 CHECK(raya_submitter_snapshot(&s,&life,&q,&producers,&active,&leases));
 CHECK(life==RAYA_SUBMITTER_STOPPED&&q==0&&producers==0&&active==0&&leases==0);
 CHECK(raya_submitter_destroy(&s));
 CHECK(CloseHandle(c.entered)); CHECK(CloseHandle(c.publish));
 (void)printf("Dedicated Submitter core: %u assertions PASS\n",checks); return 0;
}
