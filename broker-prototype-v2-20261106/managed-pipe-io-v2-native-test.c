#include "managed-pipe-io-v2.h"
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#define CHECK(x) do { ++checks; if(!(x)){(void)fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;} } while(0)
static unsigned checks;
static HANDLE callback_event;
static volatile LONG callback_count;
static DWORD callback_error;
static DWORD callback_bytes;
static BYTE callback_byte;
static BOOL cancel_not_found(HANDLE pipe,LPOVERLAPPED ov,void *context) {
 (void)pipe;(void)ov;(void)context;SetLastError(ERROR_NOT_FOUND);return FALSE;
}
static void completed(DWORD error,DWORD bytes,const void *data,void *context) {
 (void)context;
 callback_error=error;callback_bytes=bytes;
 if(data && bytes) callback_byte=*(const BYTE *)data;
 (void)InterlockedIncrement(&callback_count);
 (void)SetEvent(callback_event);
}
static int make_pipe(HANDLE *server,HANDLE *client,wchar_t *name,size_t cap) {
 OVERLAPPED ov;BOOL ok;DWORD e;
 (void)swprintf_s(name,cap,L"\\\\.\\pipe\\raya-managed-pipe-%lu-%llu",
  (unsigned long)GetCurrentProcessId(),(unsigned long long)GetTickCount64());
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
static int test_timeout_handoff(void) {
 HANDLE server=INVALID_HANDLE_VALUE,client=INVALID_HANDLE_VALUE;
 wchar_t name[160];raya_managed_worker worker;
 raya_managed_pipe_submit_result sr;DWORD n=0;
 callback_count=0;callback_error=ERROR_INVALID_FUNCTION;callback_bytes=0;callback_byte=0;
 CHECK(make_pipe(&server,&client,name,160));
 callback_event=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(callback_event!=NULL);
 CHECK(raya_managed_worker_start(&worker));
 sr=raya_managed_pipe_submit_with_cancel(&worker,server,RAYA_MANAGED_PIPE_READ,
     NULL,1,0,0,completed,NULL,cancel_not_found,NULL);
 CHECK(sr==RAYA_MANAGED_PIPE_HANDED_OFF);
 CHECK(WriteFile(client,"H",1,&n,NULL));CHECK(n==1);
 CHECK(WaitForSingleObject(callback_event,5000)==WAIT_OBJECT_0);
 CHECK(callback_error==ERROR_SUCCESS&&callback_bytes==1&&callback_byte=='H');
 CHECK(InterlockedCompareExchange(&callback_count,0,0)==1);
 CHECK(raya_managed_worker_shutdown(&worker));
 CHECK(raya_managed_worker_join(&worker,5000));
 CHECK(raya_managed_worker_destroy(&worker));
 CHECK(CloseHandle(callback_event));CHECK(CloseHandle(client));CHECK(CloseHandle(server));
 return 0;
}
static int test_local_completion(void) {
 HANDLE server=INVALID_HANDLE_VALUE,client=INVALID_HANDLE_VALUE;
 wchar_t name[160];raya_managed_worker worker;
 raya_managed_pipe_submit_result sr;DWORD n=0;
 callback_count=0;callback_error=ERROR_INVALID_FUNCTION;callback_bytes=0;callback_byte=0;
 CHECK(make_pipe(&server,&client,name,160));
 CHECK(WriteFile(client,"L",1,&n,NULL));CHECK(n==1);
 callback_event=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(callback_event!=NULL);
 CHECK(raya_managed_worker_start(&worker));
 sr=raya_managed_pipe_submit(&worker,server,RAYA_MANAGED_PIPE_READ,NULL,1,5000,20,completed,NULL);
 CHECK(sr==RAYA_MANAGED_PIPE_CALLBACK_INLINE);
 CHECK(InterlockedCompareExchange(&callback_count,0,0)==1);
 CHECK(callback_error==ERROR_SUCCESS&&callback_bytes==1&&callback_byte=='L');
 CHECK(raya_managed_worker_shutdown(&worker));
 CHECK(raya_managed_worker_join(&worker,5000));
 CHECK(raya_managed_worker_destroy(&worker));
 CHECK(CloseHandle(callback_event));CHECK(CloseHandle(client));CHECK(CloseHandle(server));
 return 0;
}
static int test_write_path(void) {
 HANDLE server=INVALID_HANDLE_VALUE,client=INVALID_HANDLE_VALUE;
 wchar_t name[160];raya_managed_worker worker;
 raya_managed_pipe_submit_result sr;DWORD n=0;char got=0;
 callback_count=0;callback_error=ERROR_INVALID_FUNCTION;callback_bytes=0;callback_byte=0;
 CHECK(make_pipe(&server,&client,name,160));
 callback_event=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(callback_event!=NULL);
 CHECK(raya_managed_worker_start(&worker));
 sr=raya_managed_pipe_submit(&worker,server,RAYA_MANAGED_PIPE_WRITE,"W",1,5000,50,completed,NULL);
 CHECK(sr==RAYA_MANAGED_PIPE_CALLBACK_INLINE);
 CHECK(InterlockedCompareExchange(&callback_count,0,0)==1);
 CHECK(callback_error==ERROR_SUCCESS&&callback_bytes==1);
 CHECK(ReadFile(client,&got,1,&n,NULL));CHECK(n==1&&got=='W');
 CHECK(raya_managed_worker_shutdown(&worker));
 CHECK(raya_managed_worker_join(&worker,5000));
 CHECK(raya_managed_worker_destroy(&worker));
 CHECK(CloseHandle(callback_event));CHECK(CloseHandle(client));CHECK(CloseHandle(server));
 return 0;
}
static int test_default_cancel_drain(void) {
 HANDLE server=INVALID_HANDLE_VALUE,client=INVALID_HANDLE_VALUE;
 wchar_t name[160];raya_managed_worker worker;
 raya_managed_pipe_submit_result sr;
 callback_count=0;callback_error=ERROR_INVALID_FUNCTION;callback_bytes=99;callback_byte=0;
 CHECK(make_pipe(&server,&client,name,160));
 callback_event=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(callback_event!=NULL);
 CHECK(raya_managed_worker_start(&worker));
 sr=raya_managed_pipe_submit(&worker,server,RAYA_MANAGED_PIPE_READ,NULL,1,0,1000,completed,NULL);
 CHECK(sr==RAYA_MANAGED_PIPE_CALLBACK_INLINE);
 CHECK(InterlockedCompareExchange(&callback_count,0,0)==1);
 CHECK(callback_error==ERROR_OPERATION_ABORTED&&callback_bytes==0);
 CHECK(raya_managed_worker_shutdown(&worker));
 CHECK(raya_managed_worker_join(&worker,5000));
 CHECK(raya_managed_worker_destroy(&worker));
 CHECK(CloseHandle(callback_event));CHECK(CloseHandle(client));CHECK(CloseHandle(server));
 return 0;
}
typedef struct {
 raya_managed_worker *worker;
 raya_managed_operation *operation;
 HANDLE start_event;
 DWORD cause;
 raya_worker_retain_stage stage;
 volatile LONG published;
} concurrent_retain_attempt;
static DWORD WINAPI concurrent_retain_thread(void *parameter) {
 concurrent_retain_attempt *attempt=(concurrent_retain_attempt *)parameter;
 LONG result=-1;
 if(WaitForSingleObject(attempt->start_event,5000)==WAIT_OBJECT_0) {
  EnterCriticalSection(&attempt->worker->lock);
  result=(LONG)raya_managed_worker_publish_retained_locked(
      attempt->worker,attempt->operation,attempt->cause,attempt->stage);
  LeaveCriticalSection(&attempt->worker->lock);
 }
 (void)InterlockedExchange(&attempt->published,result);
 return 0;
}
static int test_concurrent_first_retain_publication(void) {
 raya_managed_worker worker;
 raya_managed_operation operation;
 concurrent_retain_attempt attempts[2];
 HANDLE start_event=NULL,threads[2]={NULL,NULL};
 LONG accepted;
 int ok=0;
 ZeroMemory(&operation,sizeof(operation));
 CHECK(raya_managed_worker_start(&worker));
 operation.operation_initialized=TRUE;
 operation.identity_valid=TRUE;
 operation.worker_epoch=worker.worker_epoch;
 operation.worker_operation_id=1;
 operation.inventory_kind=RAYA_WORKER_INVENTORY_DIRECT;
 ZeroMemory(attempts,sizeof(attempts));
 start_event=CreateEventW(NULL,TRUE,FALSE,NULL);
 CHECK(start_event!=NULL);
 attempts[0].worker=&worker; attempts[0].operation=&operation;
 attempts[0].start_event=start_event;
 attempts[0].cause=RAYA_RECOVERY_CAUSE_HANDLE_CLOSE;
 attempts[0].stage=RAYA_WORKER_RETAIN_STAGE_DIRECT;
 attempts[1].worker=&worker; attempts[1].operation=&operation;
 attempts[1].start_event=start_event;
 attempts[1].cause=RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT;
 attempts[1].stage=RAYA_WORKER_RETAIN_STAGE_POST_TERMINAL;
 threads[0]=CreateThread(NULL,0,concurrent_retain_thread,&attempts[0],0,NULL);
 CHECK(threads[0]!=NULL);
 threads[1]=CreateThread(NULL,0,concurrent_retain_thread,&attempts[1],0,NULL);
 CHECK(threads[1]!=NULL);
 CHECK(SetEvent(start_event));
 CHECK(WaitForMultipleObjects(2,threads,TRUE,5000)==WAIT_OBJECT_0);
 CHECK(InterlockedCompareExchange(&attempts[0].published,0,0)>=0);
 CHECK(InterlockedCompareExchange(&attempts[1].published,0,0)>=0);
 accepted=(InterlockedCompareExchange(&attempts[0].published,0,0)==1)+
          (InterlockedCompareExchange(&attempts[1].published,0,0)==1);
 CHECK(accepted==1);
 CHECK(worker.retained_count==1 && worker.retained_head==&operation);
 if(InterlockedCompareExchange(&attempts[0].published,0,0)==1) {
  CHECK(operation.first_retain_cause==attempts[0].cause);
  CHECK(operation.first_retain_stage==attempts[0].stage);
 } else {
  CHECK(operation.first_retain_cause==attempts[1].cause);
  CHECK(operation.first_retain_stage==attempts[1].stage);
 }
 EnterCriticalSection(&worker.lock);
 if(worker.retained_head==&operation && worker.retained_count==1) {
  worker.retained_head=NULL;
  worker.retained_count=0;
  operation.retained_next=NULL;
  ok=1;
 }
 LeaveCriticalSection(&worker.lock);
 CHECK(ok);
 CHECK(raya_managed_worker_shutdown(&worker));
 CHECK(raya_managed_worker_join(&worker,5000));
 CHECK(raya_managed_worker_destroy(&worker));
 CHECK(CloseHandle(threads[0]));
 CHECK(CloseHandle(threads[1]));
 CHECK(CloseHandle(start_event));
 return 0;
}
typedef struct {unsigned calls;unsigned fail_on;} close_fault;
static BOOL close_with_fault(HANDLE h,void *context) {
 close_fault *f=(close_fault *)context;
 ++f->calls;
 if(f->calls==f->fail_on)return FALSE;
 return CloseHandle(h);
}
static int test_close_failure_retention(void) {
 HANDLE server=INVALID_HANDLE_VALUE,client=INVALID_HANDLE_VALUE;
 wchar_t name[160];raya_managed_worker worker;close_fault fault={0,2};
 raya_managed_pipe_submit_result sr;DWORD n=0;
 callback_count=0;callback_error=ERROR_INVALID_FUNCTION;callback_bytes=0;
 CHECK(make_pipe(&server,&client,name,160));
 CHECK(WriteFile(client,"F",1,&n,NULL));CHECK(n==1);
 callback_event=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(callback_event!=NULL);
 CHECK(raya_managed_worker_start(&worker));
 CHECK(raya_managed_worker_set_close_callback(&worker,close_with_fault,&fault));
 sr=raya_managed_pipe_submit(&worker,server,RAYA_MANAGED_PIPE_READ,NULL,1,5000,20,completed,NULL);
 CHECK(sr==RAYA_MANAGED_PIPE_RETAINED);
 CHECK(fault.calls==2);
 CHECK(InterlockedCompareExchange(&callback_count,0,0)==0);
 CHECK(worker.integrated.core.lifecycle==RAYA_REAPER_DEGRADED);
 CHECK(worker.integrated.core.degradation_causes==RAYA_RECOVERY_CAUSE_HANDLE_CLOSE);
 CHECK(worker.integrated.reserved_slots==1&&worker.integrated.core.caller_active==0);
 CHECK(worker.retained_count==1&&worker.retained_head!=NULL);
 CHECK(worker.retained_head->identity_valid==TRUE);
 CHECK(worker.retained_head->operation_initialized==TRUE);
 CHECK(worker.retained_head->worker_epoch==worker.worker_epoch && worker.worker_epoch!=0);
 CHECK(worker.retained_head->worker_operation_id!=0);
 CHECK(worker.retained_head->inventory_kind==RAYA_WORKER_INVENTORY_DIRECT);
 CHECK(worker.retained_head->adapter_identity_valid==FALSE);
 CHECK(worker.retained_head->adapter_instance_id==0 && worker.retained_head->adapter_operation_id==0);
 CHECK(worker.retained_head->first_retain_cause==RAYA_RECOVERY_CAUSE_HANDLE_CLOSE);
 CHECK(worker.retained_head->first_retain_stage==RAYA_WORKER_RETAIN_STAGE_DIRECT);
 {
  raya_managed_operation *retained=worker.retained_head;
  int duplicate_publish;
  EnterCriticalSection(&worker.lock);
  duplicate_publish=raya_managed_worker_publish_retained_locked(&worker,
      retained,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,
      RAYA_WORKER_RETAIN_STAGE_POST_TERMINAL);
  LeaveCriticalSection(&worker.lock);
  CHECK(!duplicate_publish);
  CHECK(worker.retained_count==1 && worker.retained_head==retained);
  CHECK(retained->first_retain_cause==RAYA_RECOVERY_CAUSE_HANDLE_CLOSE);
  CHECK(retained->first_retain_stage==RAYA_WORKER_RETAIN_STAGE_DIRECT);
 }
 CHECK(!raya_managed_worker_join(&worker,5000));
 CHECK(worker.joined==TRUE);
 CHECK(!raya_managed_worker_destroy(&worker));
 CHECK(CloseHandle(callback_event));CHECK(CloseHandle(client));CHECK(CloseHandle(server));
 return 0;
}
static int test_submission_failure(void) {
 HANDLE server=INVALID_HANDLE_VALUE,client=INVALID_HANDLE_VALUE;
 wchar_t name[160];raya_managed_worker worker;
 raya_managed_pipe_submit_result sr;
 callback_count=0;callback_error=ERROR_SUCCESS;callback_bytes=77;
 CHECK(make_pipe(&server,&client,name,160));
 CHECK(DisconnectNamedPipe(server));
 callback_event=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(callback_event!=NULL);
 CHECK(raya_managed_worker_start(&worker));
 sr=raya_managed_pipe_submit(&worker,server,RAYA_MANAGED_PIPE_READ,NULL,1,100,20,completed,NULL);
 CHECK(sr==RAYA_MANAGED_PIPE_CALLBACK_INLINE);
 CHECK(InterlockedCompareExchange(&callback_count,0,0)==1);
 CHECK(callback_error!=ERROR_SUCCESS);
 CHECK(worker.integrated.core.caller_active==0&&worker.integrated.reserved_slots==0);
 CHECK(raya_managed_worker_shutdown(&worker));
 CHECK(raya_managed_worker_join(&worker,5000));
 CHECK(raya_managed_worker_destroy(&worker));
 CHECK(CloseHandle(callback_event));CHECK(CloseHandle(client));CHECK(CloseHandle(server));
 return 0;
}
int main(void) {
 if(test_timeout_handoff())return 1;
 if(test_local_completion())return 1;
 if(test_write_path())return 1;
 if(test_default_cancel_drain())return 1;
 if(test_concurrent_first_retain_publication())return 1;
 if(test_close_failure_retention())return 1;
 if(test_submission_failure())return 1;
 (void)printf("Managed pipe I/O: %u assertions, 0 failures\n",checks);
 return 0;
}
