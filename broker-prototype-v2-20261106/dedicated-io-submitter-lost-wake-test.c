#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#define CHECK(x) do { ++checks; if (!(x)) { (void)fprintf(stderr,"FAIL line %d: %s err=%lu\n",__LINE__,#x,(unsigned long)GetLastError()); return 1; } } while(0)
typedef struct { CRITICAL_SECTION lock; HANDLE wake, reset_done, consumed; int queue_count, value; } state;
static unsigned checks;
static DWORD WINAPI consumer(void *p) {
 state *s=(state*)p;
 EnterCriticalSection(&s->lock);
 if (s->queue_count != 0) { LeaveCriticalSection(&s->lock); return 10; }
 if (!ResetEvent(s->wake)) { LeaveCriticalSection(&s->lock); return 11; }
 if (!SetEvent(s->reset_done)) { LeaveCriticalSection(&s->lock); return 12; }
 LeaveCriticalSection(&s->lock);
 if (WaitForSingleObject(s->wake,5000)!=WAIT_OBJECT_0) return 13;
 EnterCriticalSection(&s->lock);
 if (s->queue_count != 1) { LeaveCriticalSection(&s->lock); return 14; }
 s->value=0; s->queue_count=0;
 if (!ResetEvent(s->wake)) { LeaveCriticalSection(&s->lock); return 15; }
 LeaveCriticalSection(&s->lock);
 return SetEvent(s->consumed)?0:16;
}
static DWORD WINAPI producer(void *p) {
 state *s=(state*)p;
 if (WaitForSingleObject(s->reset_done,5000)!=WAIT_OBJECT_0) return 20;
 EnterCriticalSection(&s->lock);
 if (s->queue_count != 0) { LeaveCriticalSection(&s->lock); return 21; }
 s->value=77; s->queue_count=1;
 if (!SetEvent(s->wake)) { LeaveCriticalSection(&s->lock); return 22; }
 LeaveCriticalSection(&s->lock);
 return 0;
}
int main(void) {
 unsigned i;
 for(i=0;i<100;i++) {
  state s; HANDLE c,p; DWORD ec=99,ep=99;
  ZeroMemory(&s,sizeof(s)); InitializeCriticalSection(&s.lock);
  s.wake=CreateEventW(NULL,TRUE,FALSE,NULL); s.reset_done=CreateEventW(NULL,TRUE,FALSE,NULL); s.consumed=CreateEventW(NULL,TRUE,FALSE,NULL);
  CHECK(s.wake&&s.reset_done&&s.consumed);
  c=CreateThread(NULL,0,consumer,&s,0,NULL); CHECK(c!=NULL);
  p=CreateThread(NULL,0,producer,&s,0,NULL); CHECK(p!=NULL);
  CHECK(WaitForSingleObject(c,5000)==WAIT_OBJECT_0); CHECK(WaitForSingleObject(p,5000)==WAIT_OBJECT_0);
  CHECK(GetExitCodeThread(c,&ec)&&ec==0); CHECK(GetExitCodeThread(p,&ep)&&ep==0);
  CHECK(WaitForSingleObject(s.consumed,0)==WAIT_OBJECT_0);
  CHECK(s.queue_count==0); CHECK(WaitForSingleObject(s.wake,0)==WAIT_TIMEOUT);
  CHECK(CloseHandle(c)); CHECK(CloseHandle(p)); CHECK(CloseHandle(s.wake)); CHECK(CloseHandle(s.reset_done)); CHECK(CloseHandle(s.consumed)); DeleteCriticalSection(&s.lock);
 }
 (void)printf("Submitter lost-wake barrier: %u assertions, 100/100 PASS\n",checks); return 0;
}