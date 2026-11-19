#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#define N 4
#define CHECK(x) do { ++checks; if (!(x)) { (void)fprintf(stderr,"FAIL line %d: %s err=%lu\n",__LINE__,#x,(unsigned long)GetLastError()); return 1; } } while(0)
typedef struct { CRITICAL_SECTION lock; HANDLE admitted, publish; int stopping, active, queued, accepted, rejected; } state;
static unsigned checks;
typedef struct { state *s; int id; } arg;
static int try_admit(state *s) {
 int accepted=0;
 EnterCriticalSection(&s->lock);
 if (s->stopping) ++s->rejected;
 else { ++s->active; accepted=1; }
 LeaveCriticalSection(&s->lock);
 return accepted;
}
static int can_exit(state *s) {
 int result;
 EnterCriticalSection(&s->lock);
 result=s->stopping && s->active==0 && s->queued==0;
 LeaveCriticalSection(&s->lock);
 return result;
}
static DWORD WINAPI producer(void *p) {
 arg *a=(arg*)p; state *s=a->s;
 if (!try_admit(s)) return 30;
 if (!ReleaseSemaphore(s->admitted,1,NULL)) return 31;
 if (WaitForSingleObject(s->publish,5000)!=WAIT_OBJECT_0) return 32;
 EnterCriticalSection(&s->lock);
 /* A lease admitted before STOPPING may still publish. */
 ++s->queued; ++s->accepted; --s->active;
 LeaveCriticalSection(&s->lock);
 return 0;
}
int main(void) {
 unsigned run,i;
 for(run=0;run<50;run++) {
  state s; HANDLE th[N]; arg a[N]; DWORD ec; ZeroMemory(&s,sizeof(s));
  InitializeCriticalSection(&s.lock);
  s.admitted=CreateSemaphoreW(NULL,0,N,NULL); s.publish=CreateEventW(NULL,TRUE,FALSE,NULL); CHECK(s.admitted&&s.publish);
  for(i=0;i<N;i++){ a[i].s=&s; a[i].id=(int)i; th[i]=CreateThread(NULL,0,producer,&a[i],0,NULL); CHECK(th[i]!=NULL); }
  /* Each producer signals the shared manual-reset event; wait until all leases are visible. */
  for(i=0;i<N;i++){ CHECK(WaitForSingleObject(s.admitted,5000)==WAIT_OBJECT_0); Sleep(1); }
  EnterCriticalSection(&s.lock); CHECK(s.active==N); s.stopping=1; LeaveCriticalSection(&s.lock);
  /* Exercise the same admission function used by producer threads. */
  CHECK(!try_admit(&s));
  CHECK(SetEvent(s.publish));
  for(i=0;i<N;i++){ CHECK(WaitForSingleObject(th[i],5000)==WAIT_OBJECT_0); CHECK(GetExitCodeThread(th[i],&ec)&&ec==0); CHECK(CloseHandle(th[i])); }
  EnterCriticalSection(&s.lock);
  CHECK(s.active==0); CHECK(s.queued==N); CHECK(s.accepted==N); CHECK(s.rejected==1);
  CHECK(s.stopping && s.active==0 && s.queued==N);
  LeaveCriticalSection(&s.lock);
  CHECK(!can_exit(&s));
  for(i=0;i<N;i++) {
   EnterCriticalSection(&s.lock);
   CHECK(s.queued>0);
   --s.queued;
   LeaveCriticalSection(&s.lock);
  }
  CHECK(can_exit(&s));
  CHECK(CloseHandle(s.admitted)); CHECK(CloseHandle(s.publish)); DeleteCriticalSection(&s.lock);
 }
 (void)printf("Submitter admission/STOPPING: %u assertions, 50/50 PASS\n",checks); return 0;
}