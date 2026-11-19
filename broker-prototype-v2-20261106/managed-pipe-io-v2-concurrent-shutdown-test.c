#include "managed-pipe-io-v2.h"
#include <stdio.h>
#include <wchar.h>
#define OP_COUNT 70
#define CHECK(x) do { ++checks; if(!(x)){(void)fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;} } while(0)
typedef struct {
 HANDLE all_entered;
 HANDLE release_cancel;
 HANDLE all_submitted;
 HANDLE producer_release;
 volatile LONG cancel_entered;
 volatile LONG submitted;
 volatile LONG pre_error;
 volatile LONG post_error;
} cancel_barrier;
typedef struct {
 volatile LONG callbacks;
 volatile LONG callback_errors;
 volatile LONG first_error;
 volatile LONG first_bytes;
 HANDLE callbacks_done;
} callback_stats;
typedef struct {
 raya_managed_worker *worker;
 HANDLE pipe;
 cancel_barrier *barrier;
 callback_stats *stats;
 raya_managed_pipe_submit_result result;
} producer_arg;
static unsigned checks;
static BOOL cancel_barrier_fn(HANDLE pipe,LPOVERLAPPED ov,void *context) {
 cancel_barrier *b=(cancel_barrier *)context;
 LONG n;DWORD transferred=0;BOOL got;
 got=GetOverlappedResult(pipe,ov,&transferred,FALSE);
 (void)InterlockedCompareExchange(&b->pre_error,got?ERROR_SUCCESS:(LONG)GetLastError(),0);
 n=InterlockedIncrement(&b->cancel_entered);
 if(n==OP_COUNT)(void)SetEvent(b->all_entered);
 if(WaitForSingleObject(b->release_cancel,15000)!=WAIT_OBJECT_0)
  SetLastError(ERROR_TIMEOUT);
 else SetLastError(ERROR_NOT_FOUND);
 got=GetOverlappedResult(pipe,ov,&transferred,FALSE);
 (void)InterlockedCompareExchange(&b->post_error,got?ERROR_SUCCESS:(LONG)GetLastError(),0);
 return FALSE;
}
static void completion(DWORD error,DWORD bytes,const void *data,void *context) {
 callback_stats *s=(callback_stats *)context;
 (void)data;
 if(error!=ERROR_SUCCESS || bytes!=1) {
  (void)InterlockedIncrement(&s->callback_errors);
  (void)InterlockedCompareExchange(&s->first_error,(LONG)error,0);
  (void)InterlockedCompareExchange(&s->first_bytes,(LONG)bytes,-1);
 }
 if(InterlockedIncrement(&s->callbacks)==OP_COUNT)
  (void)SetEvent(s->callbacks_done);
}
static DWORD WINAPI producer_main(void *context) {
 producer_arg *a=(producer_arg *)context;
 a->result=raya_managed_pipe_submit_with_cancel(
  a->worker,a->pipe,RAYA_MANAGED_PIPE_READ,NULL,1,0,0,
  completion,a->stats,cancel_barrier_fn,a->barrier);
 if(InterlockedIncrement(&a->barrier->submitted)==OP_COUNT)
  (void)SetEvent(a->barrier->all_submitted);
 (void)WaitForSingleObject(a->barrier->producer_release,15000);
 return 0;
}
static int make_pipe(HANDLE *server,HANDLE *client,wchar_t *name,size_t cap,unsigned index) {
 OVERLAPPED ov;BOOL ok;DWORD e;
 (void)swprintf_s(name,cap,L"\\\\.\\pipe\\raya-managed-stress-%lu-%llu-%u",
  (unsigned long)GetCurrentProcessId(),(unsigned long long)GetTickCount64(),index);
 *server=CreateNamedPipeW(name,PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
  PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,4096,4096,0,NULL);
 if(*server==INVALID_HANDLE_VALUE)return 0;
 *client=CreateFileW(name,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
 if(*client==INVALID_HANDLE_VALUE){CloseHandle(*server);return 0;}
 ZeroMemory(&ov,sizeof(ov));ov.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);
 if(!ov.hEvent){CloseHandle(*client);CloseHandle(*server);return 0;}
 ok=ConnectNamedPipe(*server,&ov);e=ok?ERROR_SUCCESS:GetLastError();
 CloseHandle(ov.hEvent);
 if(!ok && e!=ERROR_PIPE_CONNECTED){CloseHandle(*client);CloseHandle(*server);return 0;}
 return 1;
}
int main(void) {
 HANDLE servers[OP_COUNT]={0},clients[OP_COUNT]={0};
 HANDLE threads[OP_COUNT]={0};
 producer_arg args[OP_COUNT];
 wchar_t names[OP_COUNT][160];
 raya_managed_worker worker;
 cancel_barrier barrier;
 callback_stats stats;
 BYTE payload[OP_COUNT];
 DWORD written=0;
 size_t outstanding_snapshot,queued_snapshot,registry_snapshot,reserved_snapshot;
 LONG i;
 int ok=0;
 ZeroMemory(&worker,sizeof(worker));
 ZeroMemory(&barrier,sizeof(barrier));
 ZeroMemory(&stats,sizeof(stats));
 for(i=0;i<OP_COUNT;i++)
  CHECK(make_pipe(&servers[i],&clients[i],names[i],160,(unsigned)i));
 barrier.all_entered=CreateEventW(NULL,TRUE,FALSE,NULL);
 barrier.release_cancel=CreateEventW(NULL,TRUE,FALSE,NULL);
 barrier.all_submitted=CreateEventW(NULL,TRUE,FALSE,NULL);
 barrier.producer_release=CreateEventW(NULL,TRUE,FALSE,NULL);
 stats.callbacks_done=CreateEventW(NULL,TRUE,FALSE,NULL);
 CHECK(barrier.all_entered!=NULL&&barrier.release_cancel!=NULL&&
       barrier.all_submitted!=NULL&&barrier.producer_release!=NULL&&stats.callbacks_done!=NULL);
 CHECK(raya_managed_worker_start(&worker));
 for(i=0;i<OP_COUNT;i++) {
  args[i].worker=&worker;args[i].pipe=servers[i];
  args[i].barrier=&barrier;args[i].stats=&stats;
  args[i].result=RAYA_MANAGED_PIPE_REJECTED;
  threads[i]=CreateThread(NULL,0,producer_main,&args[i],0,NULL);
  CHECK(threads[i]!=NULL);
 }
 CHECK(WaitForSingleObject(barrier.all_entered,15000)==WAIT_OBJECT_0);
 CHECK(barrier.cancel_entered==OP_COUNT);
 CHECK(worker.integrated.core.caller_active==OP_COUNT);
 CHECK(worker.integrated.reserved_slots==OP_COUNT);
 CHECK(raya_managed_worker_shutdown(&worker));
 CHECK(worker.integrated.core.lifecycle==RAYA_REAPER_STOPPING);
 CHECK(SetEvent(barrier.release_cancel));
 CHECK(WaitForSingleObject(barrier.all_submitted,15000)==WAIT_OBJECT_0);
 for(i=0;i<OP_COUNT;i++) CHECK(args[i].result==RAYA_MANAGED_PIPE_HANDED_OFF);
 EnterCriticalSection(&worker.lock);
 outstanding_snapshot=worker.integrated.core.outstanding;
 queued_snapshot=worker.integrated.core.queued;
 registry_snapshot=worker.integrated.registry.count;
 reserved_snapshot=worker.integrated.reserved_slots;
 LeaveCriticalSection(&worker.lock);
 CHECK(worker.integrated.core.caller_active==0);
 CHECK(outstanding_snapshot==OP_COUNT);
 CHECK(reserved_snapshot==OP_COUNT);
 CHECK(queued_snapshot+registry_snapshot==OP_COUNT);
 CHECK(barrier.pre_error==ERROR_IO_INCOMPLETE);
 CHECK(barrier.post_error==ERROR_IO_INCOMPLETE);
 for(i=0;i<OP_COUNT;i++) {
  payload[i]=(BYTE)(i+1);
  CHECK(WriteFile(clients[i],&payload[i],1,&written,NULL));
  CHECK(written==1);
 }
 CHECK(WaitForSingleObject(stats.callbacks_done,15000)==WAIT_OBJECT_0);
 CHECK(stats.callbacks==OP_COUNT);
 CHECK(stats.callback_errors==0);
 for(i=0;i<OP_COUNT;i++) { CHECK(CloseHandle(servers[i])); servers[i]=NULL; }
 CHECK(SetEvent(barrier.producer_release));
 for(i=0;i<OP_COUNT;i++) {
  CHECK(WaitForSingleObject(threads[i],15000)==WAIT_OBJECT_0);
  CHECK(CloseHandle(threads[i]));threads[i]=NULL;
 }
 CHECK(raya_managed_worker_join(&worker,15000));
 CHECK(worker.thread_exit_code==ERROR_SUCCESS);
 CHECK(raya_managed_worker_destroy(&worker));
 ok=1;
 (void)printf("Managed Pipe I/O concurrent shutdown: %u assertions, %d producers, callbacks=%ld, errors=%ld\n",
  checks,OP_COUNT,(long)stats.callbacks,(long)stats.callback_errors);
 if(!ok)return 1;
 for(i=0;i<OP_COUNT;i++)if(threads[i])CloseHandle(threads[i]);
 CloseHandle(stats.callbacks_done);CloseHandle(barrier.producer_release);
 CloseHandle(barrier.all_submitted);CloseHandle(barrier.release_cancel);
 CloseHandle(barrier.all_entered);
 for(i=0;i<OP_COUNT;i++)if(clients[i])CloseHandle(clients[i]);
 return 0;
}
