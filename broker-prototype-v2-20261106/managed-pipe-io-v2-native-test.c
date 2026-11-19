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
 raya_worker_retained_record_v1 inventory[1];size_t inventory_count=0;
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
 CHECK(worker.joined==TRUE && worker.thread==NULL);
 ZeroMemory(inventory,sizeof(inventory));
 CHECK(raya_managed_worker_copy_retained_inventory_v1(&worker,
       RAYA_WORKER_RETAINED_RECORD_VERSION_1,inventory,1,
       &inventory_count)==RAYA_RETAINED_INVENTORY_OK);
 CHECK(inventory_count==1);
 CHECK(inventory[0].struct_size==sizeof(inventory[0]));
 CHECK(inventory[0].version==RAYA_WORKER_RETAINED_RECORD_VERSION_1);
 CHECK(inventory[0].identity_valid==TRUE);
 CHECK(inventory[0].inventory_kind==RAYA_WORKER_INVENTORY_DIRECT);
 CHECK(inventory[0].worker_epoch==worker.worker_epoch);
 CHECK(inventory[0].worker_operation_id!=0);
 CHECK(inventory[0].adapter_identity_valid==FALSE);
 CHECK(inventory[0].adapter_instance_id==0 && inventory[0].adapter_operation_id==0);
 CHECK(inventory[0].first_retain_cause==RAYA_RECOVERY_CAUSE_HANDLE_CLOSE);
 CHECK(inventory[0].first_retain_stage==RAYA_WORKER_RETAIN_STAGE_DIRECT);
 CHECK(!raya_managed_worker_destroy(&worker));
 CHECK(CloseHandle(callback_event));CHECK(CloseHandle(client));CHECK(CloseHandle(server));
 return 0;
}
typedef struct { HANDLE entered; HANDLE release; } handoff_gate;
typedef struct { raya_managed_worker *worker; HANDLE pipe; handoff_gate *gate; raya_managed_pipe_submit_result result; } pending_submit_attempt;
typedef struct { HANDLE release; LONG calls; } degrade_close_gate;
static BOOL cancel_wait_for_degrade(HANDLE pipe,LPOVERLAPPED ov,void *context) {
 handoff_gate *gate=(handoff_gate *)context;
 (void)pipe;(void)ov;
 (void)SetEvent(gate->entered);
 if(WaitForSingleObject(gate->release,5000)!=WAIT_OBJECT_0) { SetLastError(ERROR_TIMEOUT); return FALSE; }
 SetLastError(ERROR_NOT_FOUND);return FALSE;
}
static BOOL close_fail_first_and_release(HANDLE handle,void *context) {
 degrade_close_gate *gate=(degrade_close_gate *)context;
 if(InterlockedIncrement(&gate->calls)==1) {
  (void)SetEvent(gate->release);SetLastError(ERROR_INVALID_HANDLE);return FALSE;
 }
 return CloseHandle(handle);
}
static DWORD WINAPI pending_submit_thread(void *parameter) {
 pending_submit_attempt *attempt=(pending_submit_attempt *)parameter;
 attempt->result=raya_managed_pipe_submit_with_cancel(attempt->worker,
     attempt->pipe,RAYA_MANAGED_PIPE_READ,NULL,1,0,0,completed,NULL,
     cancel_wait_for_degrade,attempt->gate);
 return 0;
}
static int test_handoff_not_transferred_after_concurrent_degradation(void) {
 HANDLE server_a=INVALID_HANDLE_VALUE,client_a=INVALID_HANDLE_VALUE;
 HANDLE server_b=INVALID_HANDLE_VALUE,client_b=INVALID_HANDLE_VALUE;
 HANDLE entered=NULL,release=NULL,thread=NULL;
 wchar_t name_a[160],name_b[160];raya_managed_worker worker;
 handoff_gate gate;degrade_close_gate close_gate;pending_submit_attempt attempt;
 raya_managed_pipe_submit_result first_result;DWORD n=0;
 raya_worker_retained_record_v1 inventory[2];size_t inventory_count=0,k;
 int saw_close=0,saw_handoff=0;
 callback_count=0;callback_error=ERROR_INVALID_FUNCTION;callback_bytes=0;
 CHECK(make_pipe(&server_a,&client_a,name_a,160));
 Sleep(10);
 CHECK(make_pipe(&server_b,&client_b,name_b,160));
 CHECK(WriteFile(client_a,"A",1,&n,NULL));CHECK(n==1);
 entered=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(entered!=NULL);
 release=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(release!=NULL);
 callback_event=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(callback_event!=NULL);
 CHECK(raya_managed_worker_start(&worker));
 gate.entered=entered;gate.release=release;close_gate.release=release;close_gate.calls=0;
 CHECK(raya_managed_worker_set_close_callback(&worker,close_fail_first_and_release,&close_gate));
 attempt.worker=&worker;attempt.pipe=server_b;attempt.gate=&gate;attempt.result=RAYA_MANAGED_PIPE_REJECTED;
 thread=CreateThread(NULL,0,pending_submit_thread,&attempt,0,NULL);CHECK(thread!=NULL);
 CHECK(WaitForSingleObject(entered,5000)==WAIT_OBJECT_0);
 first_result=raya_managed_pipe_submit(&worker,server_a,RAYA_MANAGED_PIPE_READ,
     NULL,1,5000,0,completed,NULL);
 CHECK(first_result==RAYA_MANAGED_PIPE_RETAINED);
 CHECK(InterlockedCompareExchange(&close_gate.calls,0,0)==1);
 CHECK(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);
 CHECK(attempt.result==RAYA_MANAGED_PIPE_RETAINED);
 CHECK(worker.integrated.core.lifecycle==RAYA_REAPER_DEGRADED);
 CHECK(worker.integrated.core.degradation_causes==
       (RAYA_RECOVERY_CAUSE_HANDLE_CLOSE|RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT));
 CHECK(worker.integrated.core.caller_active==0&&worker.integrated.reserved_slots==2);
 CHECK(worker.retained_count==2);
 CHECK(!raya_managed_worker_join(&worker,5000));
 CHECK(worker.joined==TRUE&&worker.thread==NULL);
 ZeroMemory(inventory,sizeof(inventory));
 CHECK(raya_managed_worker_copy_retained_inventory_v1(&worker,
       RAYA_WORKER_RETAINED_RECORD_VERSION_1,inventory,2,&inventory_count)==
       RAYA_RETAINED_INVENTORY_OK);
 CHECK(inventory_count==2);
 for(k=0;k<inventory_count;++k) {
  CHECK(inventory[k].identity_valid==TRUE);
  CHECK(inventory[k].inventory_kind==RAYA_WORKER_INVENTORY_DIRECT);
  CHECK(inventory[k].worker_epoch==worker.worker_epoch);
  CHECK(inventory[k].worker_operation_id!=0);
  CHECK(inventory[k].first_retain_stage==RAYA_WORKER_RETAIN_STAGE_DIRECT);
  if(inventory[k].first_retain_cause==RAYA_RECOVERY_CAUSE_HANDLE_CLOSE) saw_close=1;
  if(inventory[k].first_retain_cause==RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT) saw_handoff=1;
 }
 CHECK(saw_close&&saw_handoff);
 CHECK(InterlockedCompareExchange(&callback_count,0,0)==0);
 CHECK(!raya_managed_worker_destroy(&worker));
 CHECK(CloseHandle(thread));CHECK(CloseHandle(entered));CHECK(CloseHandle(release));
 CHECK(CloseHandle(callback_event));CHECK(CloseHandle(client_a));CHECK(CloseHandle(server_a));
 CHECK(CloseHandle(client_b));CHECK(CloseHandle(server_b));
 return 0;
}
static int test_submission_failure(void) {
 HANDLE server=INVALID_HANDLE_VALUE,client=INVALID_HANDLE_VALUE;
 wchar_t name[160];raya_managed_worker worker;
 raya_managed_pipe_submit_result sr;
 callback_count=0;callback_error=ERROR_SUCCESS;callback_bytes=77;
 Sleep(10);
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
 if(test_handoff_not_transferred_after_concurrent_degradation())return 1;
 if(test_submission_failure())return 1;
 (void)printf("Managed pipe I/O: %u assertions, 0 failures\n",checks);
 return 0;
}
