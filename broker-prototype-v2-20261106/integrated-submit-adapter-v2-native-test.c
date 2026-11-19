#include "integrated-submit-adapter-v2.h"
#include <stdio.h>
#include <string.h>

static LONG checks=0;
#define CHECK(x) do { ++checks; if (!(x)) { \
 fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)

typedef struct {
 HANDLE event_handle;
 DWORD error;
 DWORD transferred;
 char data[32];
 volatile LONG calls;
} callback_state;

static void completion(DWORD error,DWORD transferred,const void *data,void *context) {
 callback_state *s=(callback_state *)context;
 s->error=error;
 s->transferred=transferred;
 if (data && transferred<=sizeof(s->data))
  memcpy(s->data,data,transferred);
 InterlockedIncrement(&s->calls);
 SetEvent(s->event_handle);
}

typedef struct {
 raya_integrated_submit_adapter *adapter;
 HANDLE pipe;
 callback_state *callback;
 raya_managed_pipe_direction direction;
 const void *write_data;
 DWORD length;
 raya_adapter_result result;
} producer_args;

static DWORD WINAPI producer_main(void *context) {
 producer_args *args=(producer_args *)context;
 args->result=raya_integrated_adapter_submit(args->adapter,args->pipe,
     args->direction,args->write_data,args->length,completion,args->callback);
 return 0;
}

static int run_write_once(void) {
 raya_dedicated_submitter submitter;
 raya_managed_worker worker;
 raya_integrated_submit_adapter adapter;
 callback_state cb;
 HANDLE server=INVALID_HANDLE_VALUE,client=INVALID_HANDLE_VALUE,producer=NULL;
 DWORD producer_code=0,received=0;
 producer_args pargs;
 raya_submitter_lifecycle sl;
 size_t q=0,p=0,a=0,l=0;
 char payload[]="raya-write";
 char received_data[32]={0};
 ZeroMemory(&submitter,sizeof(submitter));
 ZeroMemory(&worker,sizeof(worker));
 ZeroMemory(&adapter,sizeof(adapter));
 ZeroMemory(&cb,sizeof(cb));
 cb.event_handle=CreateEventW(NULL,TRUE,FALSE,NULL);
 CHECK(cb.event_handle!=NULL);
 server=CreateNamedPipeW(L"\\\\.\\pipe\\raya-submit-adapter-v2",
     PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
     PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,4096,4096,0,NULL);
 CHECK(server!=INVALID_HANDLE_VALUE);
 client=CreateFileW(L"\\\\.\\pipe\\raya-submit-adapter-v2",
     GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
 CHECK(client!=INVALID_HANDLE_VALUE);
 CHECK(raya_managed_worker_start(&worker));
 CHECK(raya_integrated_adapter_start(&adapter,&submitter,&worker,4));
 ZeroMemory(&pargs,sizeof(pargs));
 pargs.adapter=&adapter; pargs.pipe=server; pargs.callback=&cb;
 pargs.direction=RAYA_MANAGED_PIPE_WRITE;
 pargs.write_data=payload; pargs.length=(DWORD)(sizeof(payload)-1);
 producer=CreateThread(NULL,0,producer_main,&pargs,0,NULL);
 CHECK(producer!=NULL);
 CHECK(WaitForSingleObject(producer,5000)==WAIT_OBJECT_0);
 CHECK(GetExitCodeThread(producer,&producer_code) && producer_code==0);
 CHECK(CloseHandle(producer));
 producer=NULL;
 CHECK(pargs.result==RAYA_ADAPTER_QUEUED);
 CHECK(WaitForSingleObject(cb.event_handle,5000)==WAIT_OBJECT_0);
 CHECK(InterlockedCompareExchange(&cb.calls,0,0)==1);
 CHECK(cb.error==ERROR_SUCCESS);
 CHECK(cb.transferred==sizeof(payload)-1);
 CHECK(ReadFile(client,received_data,sizeof(received_data),&received,NULL));
 CHECK(received==sizeof(payload)-1);
 CHECK(memcmp(received_data,payload,sizeof(payload)-1)==0);
 CHECK(raya_submitter_snapshot(&submitter,&sl,&q,&p,&a,&l));
 CHECK(sl==RAYA_SUBMITTER_RUNNING && q==0 && p==0 && a==0 && l==0);
 CHECK(raya_submitter_shutdown(&submitter));
 CHECK(raya_managed_worker_shutdown(&worker));
 CHECK(raya_submitter_join(&submitter,5000));
 CHECK(raya_managed_worker_join(&worker,5000));
 CHECK(raya_integrated_adapter_retained_count(&adapter)==0);
 CHECK(raya_integrated_adapter_destroy(&adapter));
 CHECK(raya_submitter_destroy(&submitter));
 CHECK(raya_managed_worker_destroy(&worker));
 CloseHandle(client); CloseHandle(server); CloseHandle(cb.event_handle);
 return 0;
}

static int run_once(int mode) {
 raya_dedicated_submitter submitter;
 raya_managed_worker worker;
 raya_integrated_submit_adapter adapter;
 callback_state cb;
 HANDLE server=INVALID_HANDLE_VALUE,client=INVALID_HANDLE_VALUE,producer=NULL;
 DWORD written=0,producer_code=0;
 producer_args pargs;
 raya_adapter_result ar;
 raya_submitter_lifecycle sl;
 size_t q=0,p=0,a=0,l=0;
 char payload[]="raya-io-ok";
 ZeroMemory(&submitter,sizeof(submitter));
 ZeroMemory(&worker,sizeof(worker));
 ZeroMemory(&adapter,sizeof(adapter));
 ZeroMemory(&cb,sizeof(cb));
 cb.event_handle=CreateEventW(NULL,TRUE,FALSE,NULL);
 CHECK(cb.event_handle!=NULL);

 server=CreateNamedPipeW(L"\\\\.\\pipe\\raya-submit-adapter-v2",
     PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
     PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,4096,4096,0,NULL);
 CHECK(server!=INVALID_HANDLE_VALUE);
 client=CreateFileW(L"\\\\.\\pipe\\raya-submit-adapter-v2",
     GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
 CHECK(client!=INVALID_HANDLE_VALUE);
 CHECK(raya_managed_worker_start(&worker));
 CHECK(raya_integrated_adapter_start(&adapter,&submitter,&worker,4));

 if (mode==1) {
  CHECK(WriteFile(client,payload,(DWORD)(sizeof(payload)-1),&written,NULL));
  CHECK(written==sizeof(payload)-1);
 }
 if (mode==2) { CloseHandle(client); client=INVALID_HANDLE_VALUE; }
 ZeroMemory(&pargs,sizeof(pargs));
 pargs.adapter=&adapter; pargs.pipe=server; pargs.callback=&cb;
 pargs.direction=RAYA_MANAGED_PIPE_READ; pargs.length=10;
 producer=CreateThread(NULL,0,producer_main,&pargs,0,NULL);
 CHECK(producer!=NULL);
 CHECK(WaitForSingleObject(producer,5000)==WAIT_OBJECT_0);
 CHECK(GetExitCodeThread(producer,&producer_code) && producer_code==0);
 CHECK(CloseHandle(producer));
 producer=NULL;
 ar=pargs.result;
 CHECK(ar==RAYA_ADAPTER_QUEUED);
 if (mode==0) {
  CHECK(WriteFile(client,payload,(DWORD)(sizeof(payload)-1),&written,NULL));
  CHECK(written==sizeof(payload)-1);
 }
 { DWORD wr=WaitForSingleObject(cb.event_handle,5000); if(wr!=WAIT_OBJECT_0){
 raya_submitter_snapshot(&submitter,&sl,&q,&p,&a,&l);
 fprintf(stderr,"timeout: submitter=%d q=%llu p=%llu active=%llu leases=%llu worker=%d queued=%llu outstanding=%llu callers=%llu reserved=%llu registry=%llu retained=%llu cb=%ld\\n",
 (int)sl,(unsigned long long)q,(unsigned long long)p,(unsigned long long)a,(unsigned long long)l,
 (int)worker.integrated.core.lifecycle,(unsigned long long)worker.integrated.core.queued,
 (unsigned long long)worker.integrated.core.outstanding,(unsigned long long)worker.integrated.core.caller_active,
 (unsigned long long)worker.integrated.reserved_slots,(unsigned long long)worker.integrated.registry.count,
 (unsigned long long)worker.retained_count,(long)cb.calls); return 1; } }
 CHECK(InterlockedCompareExchange(&cb.calls,0,0)==1);
 if (mode==2) {
  CHECK(cb.error!=ERROR_SUCCESS);
  CHECK(cb.transferred==0);
 } else {
  CHECK(cb.error==ERROR_SUCCESS);
  CHECK(cb.transferred==sizeof(payload)-1);
  CHECK(memcmp(cb.data,payload,sizeof(payload)-1)==0);
 }
 CHECK(raya_submitter_snapshot(&submitter,&sl,&q,&p,&a,&l));
 CHECK(sl==RAYA_SUBMITTER_RUNNING && q==0 && p==0 && a==0 && l==0);
 CHECK(raya_submitter_shutdown(&submitter));
 CHECK(raya_managed_worker_shutdown(&worker));
 CHECK(raya_submitter_join(&submitter,5000));
 CHECK(raya_managed_worker_join(&worker,5000));
 CHECK(raya_integrated_adapter_retained_count(&adapter)==0);
 CHECK(raya_integrated_adapter_destroy(&adapter));
 CHECK(raya_submitter_destroy(&submitter));
 CHECK(raya_managed_worker_destroy(&worker));
 CloseHandle(client);
 CloseHandle(server);
 CloseHandle(cb.event_handle);
 return 0;
}

int main(void) {
 int i;
 for (i=0;i<10;++i) if (run_once(0)) return 1;
 for (i=0;i<10;++i) if (run_once(1)) return 1;
 for (i=0;i<10;++i) if (run_once(2)) return 1;
 for (i=0;i<10;++i) if (run_write_once()) return 1;
 printf("integrated adapter: %ld assertions passed across 40 cases\n",checks);
 return 0;
}
