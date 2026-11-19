#define WIN32_LEAN_AND_MEAN
#include "dedicated-io-submitter-v2.h"
#include <stdio.h>
#define CHECK(x) do { ++checks; if (!(x)) { (void)fprintf(stderr,"FAIL line %d: %s err=%lu\n",__LINE__,#x,(unsigned long)GetLastError()); return 1; } } while(0)
static unsigned checks;
typedef struct { HANDLE publish_entered, allow_return, published; } ctx;
static raya_submitter_issue_result issue(raya_submitter_request *r,void *p) {
 (void)r;(void)p; return RAYA_SUBMITTER_ISSUE_ACCEPTED;
}
static int publish(raya_submitter_request *r,void *p) {
 ctx *c=(ctx*)p;(void)r;
 if (!SetEvent(c->publish_entered)) return 0;
 if (WaitForSingleObject(c->allow_return,5000)!=WAIT_OBJECT_0) return 0;
 return SetEvent(c->published)!=0;
}
static void not_issued(raya_submitter_request *r,void *p){(void)r;(void)p;}
typedef struct {raya_dedicated_submitter*s;raya_submitter_request*r;HANDLE go;DWORD result;} arg;
static DWORD WINAPI ack_main(void*p){
 arg*a=(arg*)p;
 if(WaitForSingleObject(a->go,5000)!=WAIT_OBJECT_0)return 10;
 a->result=(DWORD)raya_submitter_acknowledge(a->s,a->r);
 return a->result?0:11;
}
int main(void){
 raya_dedicated_submitter s;raya_submitter_request r;ctx c;arg a;
 HANDLE ack;DWORD ec=99; raya_submitter_lifecycle life;
 size_t q,pd,ac,ls;
 ZeroMemory(&s,sizeof(s));ZeroMemory(&r,sizeof(r));ZeroMemory(&c,sizeof(c));
 c.publish_entered=CreateEventW(NULL,TRUE,FALSE,NULL);
 c.allow_return=CreateEventW(NULL,TRUE,FALSE,NULL);
 c.published=CreateEventW(NULL,TRUE,FALSE,NULL);
 CHECK(c.publish_entered&&c.allow_return&&c.published);
 CHECK(raya_submitter_start(&s,2,issue,publish,not_issued,&c));
 CHECK(raya_submitter_admit(&s,&r,NULL));
 CHECK(raya_submitter_enqueue(&s,&r)==RAYA_SUBMITTER_OK);
 CHECK(WaitForSingleObject(c.publish_entered,5000)==WAIT_OBJECT_0);
 a.s=&s;a.r=&r;a.go=c.publish_entered;a.result=99;
 ack=CreateThread(NULL,0,ack_main,&a,0,NULL);CHECK(ack!=NULL);
 CHECK(WaitForSingleObject(ack,100)==WAIT_TIMEOUT);
 CHECK(r.state==RAYA_SUBMITTER_REQ_ISSUING);
 CHECK(SetEvent(c.allow_return));
 CHECK(WaitForSingleObject(c.published,5000)==WAIT_OBJECT_0);
 CHECK(WaitForSingleObject(ack,5000)==WAIT_OBJECT_0);
 CHECK(GetExitCodeThread(ack,&ec)&&ec==0);
 CHECK(CloseHandle(ack));
 CHECK(r.state==RAYA_SUBMITTER_REQ_ACKED);
 CHECK(raya_submitter_shutdown(&s));
 CHECK(raya_submitter_join(&s,5000));
 CHECK(raya_submitter_snapshot(&s,&life,&q,&pd,&ac,&ls));
 CHECK(life==RAYA_SUBMITTER_STOPPED&&q==0&&pd==0&&ac==0&&ls==0);
 CHECK(raya_submitter_destroy(&s));
 CHECK(CloseHandle(c.publish_entered));CHECK(CloseHandle(c.allow_return));CHECK(CloseHandle(c.published));
 (void)printf("Submitter publish/ack race: %u assertions PASS\n",checks);return 0;
}
