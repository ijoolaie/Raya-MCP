#include "dedicated-io-submitter-v2.h"
#include <stdio.h>

static raya_submitter_issue_result issue_stub(raya_submitter_request *r, void *c) {
 (void)r; (void)c; return RAYA_SUBMITTER_ISSUE_NOT_ACCEPTED;
}
static int publish_stub(raya_submitter_request *r, void *c) {
 (void)r; (void)c; return 1;
}
static void not_issued_stub(raya_submitter_request *r, void *c) {
 (void)r; (void)c;
}
typedef struct { raya_dedicated_submitter *submitter; volatile LONG done; int result; } thread_arg;
static DWORD WINAPI snapshot_thread(void *p) {
 thread_arg *a=(thread_arg *)p;
 raya_submitter_lifecycle life;
 size_t q=0,producers=0,active=0,leases=0;
 a->result=raya_submitter_snapshot(a->submitter,&life,&q,&producers,&active,&leases);
 InterlockedExchange(&a->done,1);
 return 0;
}
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n",__LINE__,#x); return 1; } ++checks; } while (0)
static LONG checks=0;
int main(void) {
 raya_dedicated_submitter s;
 thread_arg arg;
 HANDLE t;
 LONG active=-1;
 DWORD spin;
 ZeroMemory(&s,sizeof(s)); ZeroMemory(&arg,sizeof(arg));
 CHECK(raya_submitter_start(&s,8,issue_stub,publish_stub,not_issued_stub,NULL));
 arg.submitter=&s;
 /* Hold the object lock so Snapshot remains inside the public API after
    incrementing the admission counter. */
 EnterCriticalSection(&s.lock);
 t=CreateThread(NULL,0,snapshot_thread,&arg,0,NULL);
 CHECK(t!=NULL);
 for (spin=0;spin<5000;++spin) {
  if (((ULONG)InterlockedCompareExchange(&s.api_gate,0,0)&0x7fffffffu)==1u) break;
  Sleep(1);
 }
 CHECK(spin<5000);
 CHECK(!raya_submitter_admission_close(&s,&active));
 CHECK(active==1);
 CHECK(((ULONG)InterlockedCompareExchange(&s.api_gate,0,0)&0x80000000u)!=0);
 LeaveCriticalSection(&s.lock);
 CHECK(WaitForSingleObject(t,5000)==WAIT_OBJECT_0);
 CHECK(CloseHandle(t));
 CHECK(arg.result==1 && arg.done==1);
 CHECK(raya_submitter_admission_close(&s,&active));
 CHECK(active==0);
 CHECK(!raya_submitter_snapshot(&s,&s.lifecycle,&s.queue_count,
       &s.active_producers,&s.active_issue,&s.issuer_leases));
 CHECK(raya_submitter_shutdown(&s));
 CHECK(raya_submitter_join(&s,5000));
 CHECK(raya_submitter_destroy(&s));
 printf("submitter admission gate: %ld assertions passed\n",checks);
 return 0;
}
