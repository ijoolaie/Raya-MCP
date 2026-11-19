#include "managed-pipe-io-v2.h"
#include <stdio.h>
#include <wchar.h>
#define CHECK(x) do { ++checks; if(!(x)){(void)fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;} } while(0)
typedef struct {
 raya_managed_worker *worker;
 HANDLE pipe;
 HANDLE callback_event;
 raya_managed_pipe_submit_result result;
 volatile LONG callback_count;
 DWORD callback_error;
 DWORD callback_bytes;
} exit_test;
static unsigned checks;
static BOOL cancel_not_found(HANDLE pipe,LPOVERLAPPED ov,void *context) {
 (void)pipe;(void)ov;(void)context;
 SetLastError(ERROR_NOT_FOUND);
 return FALSE;
}
static void completion(DWORD error,DWORD bytes,const void *data,void *context) {
 exit_test *s=(exit_test *)context;
 (void)data;
 s->callback_error=error;s->callback_bytes=bytes;
 (void)InterlockedIncrement(&s->callback_count);
 (void)SetEvent(s->callback_event);
}
static DWORD WINAPI issue_and_exit(void *context) {
 exit_test *s=(exit_test *)context;
 s->result=raya_managed_pipe_submit_with_cancel(
  s->worker,s->pipe,RAYA_MANAGED_PIPE_READ,NULL,1,0,0,
  completion,s,cancel_not_found,NULL);
 return 0;
}
static int make_pipe(HANDLE *server,HANDLE *client,wchar_t *name,size_t cap) {
 OVERLAPPED ov;BOOL ok;DWORD e;
 (void)swprintf_s(name,cap,L"\\\\.\\pipe\\raya-thread-exit-%lu-%llu",
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
int main(void) {
 HANDLE server=INVALID_HANDLE_VALUE,client=INVALID_HANDLE_VALUE,thread;
 HANDLE callback_event;
 wchar_t name[160];
 raya_managed_worker worker;
 exit_test state;
 ZeroMemory(&worker,sizeof(worker));ZeroMemory(&state,sizeof(state));
 CHECK(make_pipe(&server,&client,name,160));
 callback_event=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(callback_event!=NULL);
 state.worker=&worker;state.pipe=server;state.callback_event=callback_event;
 CHECK(raya_managed_worker_start(&worker));
 thread=CreateThread(NULL,0,issue_and_exit,&state,0,NULL);CHECK(thread!=NULL);
 CHECK(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);
 CHECK(state.result==RAYA_MANAGED_PIPE_HANDED_OFF);
 CHECK(CloseHandle(thread));
 CHECK(WaitForSingleObject(callback_event,5000)==WAIT_OBJECT_0);
 CHECK(state.callback_count==1);
 CHECK(state.callback_error==ERROR_OPERATION_ABORTED);
 CHECK(state.callback_bytes==0);
 CHECK(raya_managed_worker_shutdown(&worker));
 CHECK(raya_managed_worker_join(&worker,5000));
 CHECK(worker.thread_exit_code==ERROR_SUCCESS);
 CHECK(raya_managed_worker_destroy(&worker));
 CHECK(CloseHandle(callback_event));CHECK(CloseHandle(client));CHECK(CloseHandle(server));
 (void)printf("Issuing-thread exit characterization: %u assertions, ERROR_OPERATION_ABORTED observed\n",checks);
 return 0;
}
