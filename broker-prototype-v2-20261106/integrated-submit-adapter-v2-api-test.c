#include "integrated-submit-adapter-v2.h"
#include <stdio.h>
#include <string.h>

static LONG checks=0;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)

typedef struct {
 LONG create_calls;
 LONG duplicate_calls;
 LONG close_calls;
 LONG read_calls;
 LONG sync_success;
 LONG sync_write_success;
 DWORD read_error;
 BOOL fail_create;
 BOOL fail_duplicate;
 BOOL fail_read;
 BOOL fail_write;
 BOOL fail_close;
 BOOL block_create;
 HANDLE create_entered;
 HANDLE create_release;
 BOOL degrade_after_read_accept;
 raya_managed_worker *degrade_worker;
 BOOL fail_ack_signal;
 BOOL arm_ack_on_worker_close;
 BOOL ack_failure_injected;
 BOOL ack_fail_wake;
 volatile LONG fail_reset_after_issue;
 volatile LONG reset_failure_injected;
 volatile LONG fail_wait_after_issue;
 volatile LONG wait_failure_injected;
 BOOL fail_worker_wake;
 volatile LONG worker_wake_calls;
 HANDLE worker_wake_event;
 raya_dedicated_submitter *submitter;
 HANDLE worker_close_handles[2];
 volatile LONG worker_close_calls;
 BOOL block_read;
 HANDLE read_entered;
 HANDLE read_release;
 BOOL wait_for_peer_close;
 struct api_state_tag *peer;
} api_state;
static api_state *g_ack_states[4]={NULL,NULL,NULL,NULL};
static api_state *g_worker_close_states[4]={NULL,NULL,NULL,NULL};
static SRWLOCK g_worker_close_registry_lock=SRWLOCK_INIT;
static int register_worker_close_state(api_state *state) {
 size_t i; int result=0;
 AcquireSRWLockExclusive(&g_worker_close_registry_lock);
 for(i=0;i<4;++i) if(g_worker_close_states[i]==state) { result=1; break; }
 if(!result) for(i=0;i<4;++i) if(g_worker_close_states[i]==NULL) {
  g_worker_close_states[i]=state; result=1; break;
 }
 ReleaseSRWLockExclusive(&g_worker_close_registry_lock);
 return result;
}
static void unregister_worker_close_state(api_state *state) {
 size_t i;
 AcquireSRWLockExclusive(&g_worker_close_registry_lock);
 for(i=0;i<4;++i) if(g_worker_close_states[i]==state) g_worker_close_states[i]=NULL;
 ReleaseSRWLockExclusive(&g_worker_close_registry_lock);
}
static BOOL track_worker_owned_handle(api_state *state,HANDLE handle) {
 size_t i; BOOL tracked=FALSE;
 if(state==NULL || handle==NULL || handle==INVALID_HANDLE_VALUE) return FALSE;
 AcquireSRWLockExclusive(&g_worker_close_registry_lock);
 for(i=0;i<2;++i) if(state->worker_close_handles[i]==NULL) {
  state->worker_close_handles[i]=handle; tracked=TRUE; break;
 }
 ReleaseSRWLockExclusive(&g_worker_close_registry_lock);
 return tracked;
}
static BOOL WINAPI test_shared_worker_close(HANDLE handle,void *context) {
 size_t i,j; api_state *owner=NULL; BOOL closed=FALSE;
 (void)context;
 AcquireSRWLockExclusive(&g_worker_close_registry_lock);
 for(i=0;i<4 && owner==NULL;++i) if(g_worker_close_states[i])
  for(j=0;j<2;++j) if(g_worker_close_states[i]->worker_close_handles[j]==handle) {
   owner=g_worker_close_states[i];
   if(owner->fail_close) SetLastError(ERROR_INVALID_HANDLE);
   else if(CloseHandle(handle)) {
    owner->worker_close_handles[j]=NULL; closed=TRUE;
   }
   break;
  }
 ReleaseSRWLockExclusive(&g_worker_close_registry_lock);
 if(owner==NULL) { SetLastError(ERROR_NOT_FOUND); return FALSE; }
 if(!closed) return FALSE;
 if(owner->arm_ack_on_worker_close &&
    InterlockedIncrement(&owner->worker_close_calls)==2 &&
    owner->submitter!=NULL)
  owner->fail_ack_signal=TRUE;
 else if(!owner->arm_ack_on_worker_close)
  InterlockedIncrement(&owner->worker_close_calls);
 return TRUE;
}
static int register_ack_state(api_state *state) {
 size_t i;
 for(i=0;i<4;++i) if(g_ack_states[i]==state) return 1;
 for(i=0;i<4;++i) if(g_ack_states[i]==NULL) { g_ack_states[i]=state; return 1; }
 return 0;
}
static void unregister_ack_state(api_state *state) {
 size_t i;
 for(i=0;i<4;++i) if(g_ack_states[i]==state) g_ack_states[i]=NULL;
}
static void test_noop_release(raya_managed_operation *op,void *context) { (void)op; (void)context; }

static HANDLE WINAPI test_create_event(LPSECURITY_ATTRIBUTES sa,BOOL manual,
 BOOL initial,LPCWSTR name,void *context) {
 api_state *s=(api_state *)context;
 InterlockedIncrement(&s->create_calls);
 if (s->block_create) {
  if (!ReleaseSemaphore(s->create_entered,1,NULL)) return NULL;
  if (WaitForSingleObject(s->create_release,5000)!=WAIT_OBJECT_0) return NULL;
 }
 if (s->fail_create) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
 HANDLE created=CreateEventW(sa,manual,initial,name);
 if(created==NULL) return NULL;
 if(!track_worker_owned_handle(s,created)) {
  CloseHandle(created); SetLastError(ERROR_TOO_MANY_OPEN_FILES); return NULL;
 }
 return created;
}
static BOOL WINAPI test_duplicate(HANDLE sp,HANDLE source,HANDLE tp,
 LPHANDLE target,DWORD access,BOOL inherit,DWORD options,void *context) {
 api_state *s=(api_state *)context;
 InterlockedIncrement(&s->duplicate_calls);
 if (s->fail_duplicate) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
 if(!DuplicateHandle(sp,source,tp,target,access,inherit,options)) return FALSE;
 if(!track_worker_owned_handle(s,*target)) {
  CloseHandle(*target); *target=NULL; SetLastError(ERROR_TOO_MANY_OPEN_FILES); return FALSE;
 }
 return TRUE;
}
static BOOL WINAPI test_read(HANDLE pipe,LPVOID buffer,DWORD length,
 LPDWORD transferred,LPOVERLAPPED ov,void *context) {
 api_state *s=(api_state *)context;
 BOOL ok;
 InterlockedIncrement(&s->read_calls);
 if (s->fail_read) { SetLastError(ERROR_BROKEN_PIPE); return FALSE; }
 ok=ReadFile(pipe,buffer,length,transferred,ov);
 if (ok) InterlockedIncrement(&s->sync_success);
 else {
  DWORD error=GetLastError();
  if (error==ERROR_IO_PENDING && s->degrade_after_read_accept &&
      s->degrade_worker!=NULL) {
   EnterCriticalSection(&s->degrade_worker->lock);
   (void)raya_reaper_mark_degraded_cause(
       &s->degrade_worker->integrated.core,
       RAYA_RECOVERY_CAUSE_COMPLETION_UNCERTAIN);
   LeaveCriticalSection(&s->degrade_worker->lock);
  }
  SetLastError(error);
 }
 return ok;
}
static BOOL WINAPI test_write(HANDLE pipe,LPCVOID buffer,DWORD length,
 LPDWORD transferred,LPOVERLAPPED ov,void *context) {
 api_state *s=(api_state *)context;
 BOOL ok;
 if (s->fail_write) { SetLastError(ERROR_BROKEN_PIPE); return FALSE; }
 ok=WriteFile(pipe,buffer,length,transferred,ov);
 if (ok) InterlockedIncrement(&s->sync_write_success);
 return ok;
}
static DWORD WINAPI test_last_error(void *context) {
 (void)context; return GetLastError();
}
static HANDLE WINAPI test_submitter_create_event(LPSECURITY_ATTRIBUTES sa,BOOL manual,BOOL initial,LPCWSTR name) { return CreateEventW(sa,manual,initial,name); }
static HANDLE WINAPI test_submitter_create_thread(LPSECURITY_ATTRIBUTES sa,SIZE_T stack,LPTHREAD_START_ROUTINE entry,LPVOID parameter,DWORD flags,LPDWORD id) { return CreateThread(sa,stack,entry,parameter,flags,id); }
static BOOL WINAPI test_submitter_set_event(HANDLE event_handle) {
 size_t i;
 for(i=0;i<4;++i) {
  api_state *s=g_ack_states[i];
  if (s && s->fail_ack_signal && s->submitter!=NULL &&
      s->submitter->issuer_leases==0 &&
      event_handle==(s->ack_fail_wake ? s->submitter->wake_event :
                          s->submitter->changed_event)) {
   s->fail_ack_signal=FALSE;
   s->ack_failure_injected=TRUE;
   SetLastError(ERROR_INVALID_HANDLE);
   return FALSE;
  }
 }
 return SetEvent(event_handle);
}
static BOOL WINAPI test_submitter_reset_event(HANDLE event_handle) { return ResetEvent(event_handle); }
static DWORD WINAPI test_submitter_wait(HANDLE handle,DWORD timeout) { return WaitForSingleObject(handle,timeout); }
static BOOL WINAPI test_submitter_exit_code(HANDLE thread,LPDWORD code) { return GetExitCodeThread(thread,code); }
static BOOL WINAPI test_submitter_close(HANDLE handle) { return CloseHandle(handle); }
static raya_submitter_win32_api make_submitter_api(void) {
 raya_submitter_win32_api api;
 api.create_event=test_submitter_create_event;
 api.create_thread=test_submitter_create_thread;
 api.set_event=test_submitter_set_event;
 api.reset_event=test_submitter_reset_event;
 api.wait_for_single_object=test_submitter_wait;
 api.get_exit_code_thread=test_submitter_exit_code;
 api.close_handle=test_submitter_close;
 return api;
}
#ifdef RAYA_ADAPTER_TESTING
static BOOL WINAPI test_worker_set_event(HANDLE event_handle,void *context) {
 api_state *s=(api_state *)context;
 InterlockedIncrement(&s->worker_wake_calls);
 if (s->fail_worker_wake && event_handle==s->worker_wake_event) {
  s->fail_worker_wake=FALSE;
  SetLastError(ERROR_INVALID_HANDLE);
  return FALSE;
 }
 return SetEvent(event_handle);
}
#endif
static BOOL WINAPI test_close(HANDLE handle,void *context) {
 api_state *s=(api_state *)context;
 InterlockedIncrement(&s->close_calls);
 if (s->fail_close) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
 return CloseHandle(handle);
}

static void completion(DWORD error,DWORD transferred,const void *data,void *context) {
 volatile LONG *calls=(volatile LONG *)context;
 (void)error; (void)transferred; (void)data;
 InterlockedIncrement(calls);
}
static raya_integrated_adapter_api make_api(api_state *s) {
 raya_integrated_adapter_api api;
 api.create_event=test_create_event;
 api.duplicate_handle=test_duplicate;
 api.read_file=test_read;
 api.write_file=test_write;
 api.get_last_error=test_last_error;
 api.close_handle=test_close;
 api.context=s;
 return api;
}
static int stop_components(raya_integrated_submit_adapter *a,
 raya_dedicated_submitter *s,raya_managed_worker *w) {
 if (!raya_submitter_shutdown(s) || !raya_managed_worker_shutdown(w) ||
     !raya_submitter_join(s,5000) || !raya_managed_worker_join(w,5000) ||
     raya_integrated_adapter_retained_count(a)!=0 ||
     !raya_integrated_adapter_destroy(a) ||
     !raya_submitter_destroy(s) || !raya_managed_worker_destroy(w)) return 0;
 return 1;
}
static int setup(raya_integrated_submit_adapter *a,raya_dedicated_submitter *s,
 raya_managed_worker *w,api_state *state) {
 raya_integrated_adapter_api api=make_api(state);
 ZeroMemory(s,sizeof(*s)); ZeroMemory(w,sizeof(*w)); ZeroMemory(a,sizeof(*a));
 if (!raya_managed_worker_start(w)) return 0;
 if (!raya_integrated_adapter_start_with_api(a,s,w,4,&api)) return 0;
 return 1;
}
static int setup_ack_failure(raya_integrated_submit_adapter *a,
 raya_dedicated_submitter *s,raya_managed_worker *w,api_state *state) {
 raya_integrated_adapter_api adapter_api=make_api(state);
 raya_submitter_win32_api submitter_api=make_submitter_api();
 ZeroMemory(s,sizeof(*s)); ZeroMemory(w,sizeof(*w)); ZeroMemory(a,sizeof(*a));
 if (!raya_managed_worker_start(w)) return 0;
 state->submitter=s;
 state->arm_ack_on_worker_close=TRUE;
 if (!register_worker_close_state(state) || !register_ack_state(state)) return 0;
 if (!raya_managed_worker_set_close_callback(w,test_shared_worker_close,NULL)) return 0;
 if (!raya_integrated_adapter_start_with_apis(a,s,w,4,
       &adapter_api,&submitter_api)) return 0;
 return 1;
}
static int test_multi_submitter_ack_dispatcher(void) {
 api_state first,second;
 raya_dedicated_submitter first_submitter,second_submitter;
 HANDLE first_event,second_event;
 ZeroMemory(&first,sizeof(first)); ZeroMemory(&second,sizeof(second));
 ZeroMemory(&first_submitter,sizeof(first_submitter));
 ZeroMemory(&second_submitter,sizeof(second_submitter));
 first_event=CreateEventW(NULL,TRUE,FALSE,NULL);
 second_event=CreateEventW(NULL,TRUE,FALSE,NULL);
 CHECK(first_event!=NULL && second_event!=NULL);
 first.submitter=&first_submitter; second.submitter=&second_submitter;
 first_submitter.changed_event=first_event;
 second_submitter.changed_event=second_event;
 first.fail_ack_signal=TRUE; second.fail_ack_signal=TRUE;
 CHECK(register_ack_state(&first)); CHECK(register_ack_state(&second));
 CHECK(!test_submitter_set_event(first_event));
 CHECK(first.ack_failure_injected==TRUE && first.fail_ack_signal==FALSE);
 CHECK(second.ack_failure_injected==FALSE && second.fail_ack_signal==TRUE);
 CHECK(!test_submitter_set_event(second_event));
 CHECK(second.ack_failure_injected==TRUE && second.fail_ack_signal==FALSE);
 unregister_ack_state(&first); unregister_ack_state(&second);
 CHECK(CloseHandle(first_event)); CHECK(CloseHandle(second_event));
 return 0;
}
static int test_shared_worker_close_dispatcher(void) {
 api_state first,second;
 raya_dedicated_submitter first_submitter,second_submitter;
 HANDLE events[4];
 ZeroMemory(&first,sizeof(first)); ZeroMemory(&second,sizeof(second));
 ZeroMemory(&first_submitter,sizeof(first_submitter));
 ZeroMemory(&second_submitter,sizeof(second_submitter));
 events[0]=CreateEventW(NULL,TRUE,FALSE,NULL);
 events[1]=CreateEventW(NULL,TRUE,FALSE,NULL);
 events[2]=CreateEventW(NULL,TRUE,FALSE,NULL);
 events[3]=CreateEventW(NULL,TRUE,FALSE,NULL);
 CHECK(events[0] && events[1] && events[2] && events[3]);
 first.submitter=&first_submitter; second.submitter=&second_submitter;
 first.arm_ack_on_worker_close=TRUE; second.arm_ack_on_worker_close=TRUE;
 CHECK(track_worker_owned_handle(&first,events[0]));
 CHECK(track_worker_owned_handle(&first,events[1]));
 CHECK(track_worker_owned_handle(&second,events[2]));
 CHECK(track_worker_owned_handle(&second,events[3]));
 CHECK(register_worker_close_state(&first));
 CHECK(register_worker_close_state(&second));
 CHECK(test_shared_worker_close(events[0],NULL));
 CHECK(first.worker_close_calls==1 && !first.fail_ack_signal);
 CHECK(first.worker_close_handles[0]==NULL && first.worker_close_handles[1]==events[1]);
 CHECK(second.worker_close_calls==0 && !second.fail_ack_signal);
 CHECK(test_shared_worker_close(events[2],NULL));
 CHECK(second.worker_close_calls==1 && !second.fail_ack_signal);
 CHECK(test_shared_worker_close(events[1],NULL));
 CHECK(first.worker_close_calls==2 && first.fail_ack_signal);
 CHECK(second.worker_close_calls==1 && !second.fail_ack_signal);
 CHECK(test_shared_worker_close(events[3],NULL));
 CHECK(second.worker_close_calls==2 && second.fail_ack_signal);
 CHECK(first.worker_close_handles[0]==NULL && first.worker_close_handles[1]==NULL);
 CHECK(second.worker_close_handles[0]==NULL && second.worker_close_handles[1]==NULL);
 unregister_worker_close_state(&first);
 unregister_worker_close_state(&second);
 return 0;
}
static int test_create_failure(void) {
 raya_integrated_submit_adapter a; raya_dedicated_submitter s; raya_managed_worker w;
 api_state state; volatile LONG callbacks=0;
 HANDLE pipe=CreateEventW(NULL,TRUE,FALSE,NULL);
 ZeroMemory(&state,sizeof(state)); state.fail_create=TRUE;
 CHECK(pipe!=NULL); CHECK(setup(&a,&s,&w,&state));
 CHECK(raya_integrated_adapter_submit(&a,pipe,RAYA_MANAGED_PIPE_READ,NULL,1,
       completion,(void *)&callbacks)==RAYA_ADAPTER_REJECTED);
 CHECK(state.create_calls==1 && state.duplicate_calls==0 && state.close_calls==0);
 CHECK(a.active_count==0 && raya_integrated_adapter_retained_count(&a)==0);
 CHECK(callbacks==0);
 CHECK(stop_components(&a,&s,&w));
 CHECK(CloseHandle(pipe));
 return 0;
}
static int test_duplicate_failure(void) {
 raya_integrated_submit_adapter a; raya_dedicated_submitter s; raya_managed_worker w;
 api_state state; volatile LONG callbacks=0;
 HANDLE pipe=CreateEventW(NULL,TRUE,FALSE,NULL);
 ZeroMemory(&state,sizeof(state)); state.fail_duplicate=TRUE;
 CHECK(pipe!=NULL); CHECK(setup(&a,&s,&w,&state));
 CHECK(raya_integrated_adapter_submit(&a,pipe,RAYA_MANAGED_PIPE_READ,NULL,1,
       completion,(void *)&callbacks)==RAYA_ADAPTER_REJECTED);
 CHECK(state.create_calls==1 && state.duplicate_calls==1 && state.close_calls==1);
 CHECK(a.active_count==0 && raya_integrated_adapter_retained_count(&a)==0);
 CHECK(callbacks==0);
 CHECK(stop_components(&a,&s,&w));
 CHECK(CloseHandle(pipe));
 return 0;
}
static int test_immediate_read_failure(void) {
 raya_integrated_submit_adapter a; raya_dedicated_submitter s; raya_managed_worker w;
 api_state state; volatile LONG callbacks=0;
 HANDLE server=INVALID_HANDLE_VALUE,client=INVALID_HANDLE_VALUE;
 ZeroMemory(&state,sizeof(state)); state.fail_read=TRUE;
 server=CreateNamedPipeW(L"\\\\.\\pipe\\raya-adapter-api-read-fail",
     PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
     PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,4096,4096,0,NULL);
 CHECK(server!=INVALID_HANDLE_VALUE);
 client=CreateFileW(L"\\\\.\\pipe\\raya-adapter-api-read-fail",
     GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
 CHECK(client!=INVALID_HANDLE_VALUE);
 CHECK(setup(&a,&s,&w,&state));
 CHECK(raya_integrated_adapter_submit(&a,server,RAYA_MANAGED_PIPE_READ,NULL,1,
       completion,(void *)&callbacks)==RAYA_ADAPTER_QUEUED);
 {
  DWORD deadline=GetTickCount()+5000;
  while (InterlockedCompareExchange(&callbacks,0,0)==0 &&
         (LONG)(deadline-GetTickCount())>0) Sleep(1);
 }
 CHECK(callbacks==1 && state.read_calls==1);
 CHECK(state.close_calls==2 && a.active_count==0);
 CHECK(raya_integrated_adapter_retained_count(&a)==0);
 CHECK(stop_components(&a,&s,&w));
 CloseHandle(client); CloseHandle(server);
 return 0;
}
static int test_immediate_write_failure(void) {
 raya_integrated_submit_adapter a; raya_dedicated_submitter s; raya_managed_worker w;
 api_state state; volatile LONG callbacks=0;
 HANDLE server=INVALID_HANDLE_VALUE,client=INVALID_HANDLE_VALUE;
 char payload[]="fail-write";
 ZeroMemory(&state,sizeof(state)); state.fail_write=TRUE;
 server=CreateNamedPipeW(L"\\\\.\\pipe\\raya-adapter-api-write-fail",
     PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
     PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,4096,4096,0,NULL);
 CHECK(server!=INVALID_HANDLE_VALUE);
 client=CreateFileW(L"\\\\.\\pipe\\raya-adapter-api-write-fail",
     GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
 CHECK(client!=INVALID_HANDLE_VALUE);
 CHECK(setup(&a,&s,&w,&state));
 CHECK(raya_integrated_adapter_submit(&a,server,RAYA_MANAGED_PIPE_WRITE,
       payload,(DWORD)(sizeof(payload)-1),completion,(void *)&callbacks)==RAYA_ADAPTER_QUEUED);
 {
  DWORD deadline=GetTickCount()+5000;
  while (InterlockedCompareExchange(&callbacks,0,0)==0 &&
         (LONG)(deadline-GetTickCount())>0) Sleep(1);
 }
 CHECK(callbacks==1 && state.sync_write_success==0);
 CHECK(state.close_calls==2 && a.active_count==0);
 CHECK(raya_integrated_adapter_retained_count(&a)==0);
 CHECK(stop_components(&a,&s,&w));
 CloseHandle(client); CloseHandle(server);
 return 0;
}
static int test_sync_read_success(void) {
 raya_integrated_submit_adapter a; raya_dedicated_submitter s; raya_managed_worker w;
 api_state state; volatile LONG callbacks=0;
 HANDLE server=INVALID_HANDLE_VALUE,client=INVALID_HANDLE_VALUE,done=NULL;
 DWORD written=0;
 char payload[]="sync-read";
 ZeroMemory(&state,sizeof(state));
 done=CreateEventW(NULL,TRUE,FALSE,NULL);
 CHECK(done!=NULL);
 server=CreateNamedPipeW(L"\\\\.\\pipe\\raya-adapter-api-sync",
     PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
     PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,4096,4096,0,NULL);
 CHECK(server!=INVALID_HANDLE_VALUE);
 client=CreateFileW(L"\\\\.\\pipe\\raya-adapter-api-sync",
     GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
 CHECK(client!=INVALID_HANDLE_VALUE);
 CHECK(WriteFile(client,payload,(DWORD)(sizeof(payload)-1),&written,NULL));
 CHECK(written==sizeof(payload)-1);
 CHECK(setup(&a,&s,&w,&state));
 /* callback signals separately so the API call's return mode is observable. */
 CHECK(raya_integrated_adapter_submit(&a,server,RAYA_MANAGED_PIPE_READ,NULL,
       (DWORD)(sizeof(payload)-1),completion,(void *)&callbacks)==RAYA_ADAPTER_QUEUED);
 /* Destroy closes admission even while a previously accepted I/O is active. */
 CHECK(!raya_integrated_adapter_destroy(&a));
 CHECK(raya_integrated_adapter_submit(&a,server,RAYA_MANAGED_PIPE_READ,NULL,1,
       completion,(void *)&callbacks)==RAYA_ADAPTER_REJECTED);
  {
  DWORD deadline=GetTickCount()+5000;
  while (InterlockedCompareExchange(&callbacks,0,0)==0 &&
         (LONG)(deadline-GetTickCount())>0) Sleep(1);
 }
 CHECK(callbacks==1);
 CHECK(state.read_calls==1);

 CHECK(state.sync_success==1);
 CHECK(stop_components(&a,&s,&w));
 CloseHandle(client); CloseHandle(server); CloseHandle(done);
 return 0;
}
static int test_sync_write_success(void) {
 raya_integrated_submit_adapter a; raya_dedicated_submitter s; raya_managed_worker w;
 api_state state; volatile LONG callbacks=0;
 HANDLE server=INVALID_HANDLE_VALUE,client=INVALID_HANDLE_VALUE;
 DWORD received=0; char payload[]="sync-write"; char actual[32]={0};
 ZeroMemory(&state,sizeof(state));
 server=CreateNamedPipeW(L"\\\\.\\pipe\\raya-adapter-api-sync-write",
     PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
     PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,4096,4096,0,NULL);
 CHECK(server!=INVALID_HANDLE_VALUE);
 client=CreateFileW(L"\\\\.\\pipe\\raya-adapter-api-sync-write",
     GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
 CHECK(client!=INVALID_HANDLE_VALUE);
 CHECK(setup(&a,&s,&w,&state));
 CHECK(raya_integrated_adapter_submit(&a,server,RAYA_MANAGED_PIPE_WRITE,
       payload,(DWORD)(sizeof(payload)-1),completion,(void *)&callbacks)==RAYA_ADAPTER_QUEUED);
 {
  DWORD deadline=GetTickCount()+5000;
  while (InterlockedCompareExchange(&callbacks,0,0)==0 &&
         (LONG)(deadline-GetTickCount())>0) Sleep(1);
 }
 CHECK(callbacks==1);
 CHECK(state.read_calls==0 && state.sync_write_success==1);
 CHECK(ReadFile(client,actual,sizeof(actual),&received,NULL));
 CHECK(received==sizeof(payload)-1);
 CHECK(memcmp(actual,payload,received)==0);
 CHECK(stop_components(&a,&s,&w));
 CloseHandle(client); CloseHandle(server);
 return 0;
}
static int test_handoff_failure_after_os_acceptance_retains(void) {
 raya_integrated_submit_adapter a; raya_dedicated_submitter s; raya_managed_worker w;
 api_state state; volatile LONG callbacks=0;
 HANDLE server=INVALID_HANDLE_VALUE,client=INVALID_HANDLE_VALUE;
 raya_submitter_lifecycle lifecycle=RAYA_SUBMITTER_RUNNING;
 size_t queued=0,producers=0,active=0,leases=0;
 ZeroMemory(&state,sizeof(state));
 state.degrade_after_read_accept=TRUE;
 server=CreateNamedPipeW(L"\\\\.\\pipe\\raya-adapter-api-handoff-fail",
     PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
     PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,4096,4096,0,NULL);
 CHECK(server!=INVALID_HANDLE_VALUE);
 client=CreateFileW(L"\\\\.\\pipe\\raya-adapter-api-handoff-fail",
     GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
 CHECK(client!=INVALID_HANDLE_VALUE);
 CHECK(setup(&a,&s,&w,&state));
 state.degrade_worker=&w;
 CHECK(raya_integrated_adapter_submit(&a,server,RAYA_MANAGED_PIPE_READ,NULL,1,
       completion,(void *)&callbacks)==RAYA_ADAPTER_QUEUED);
 {
  DWORD deadline=GetTickCount()+5000;
  while ((raya_integrated_adapter_retained_count(&a)==0 ||
          !raya_submitter_snapshot(&s,&lifecycle,&queued,&producers,&active,&leases) ||
          lifecycle!=RAYA_SUBMITTER_DEGRADED) &&
         (LONG)(deadline-GetTickCount())>0) Sleep(1);
 }
 CHECK(state.read_calls==1);

 CHECK(raya_integrated_adapter_retained_count(&a)==1);
 CHECK(a.active_count==1 && w.retained_count==1);
 CHECK(a.retained_head!=NULL);
 CHECK(w.retained_head!=NULL);
 CHECK(w.retained_head->owner.state==RAYA_PIPE_OWNER_CALLER_UNCERTAIN);
 CHECK(w.retained_head->integrated.reservation_state==
       RAYA_INTEGRATED_RESERVATION_HELD);
 CHECK(w.integrated.core.caller_active==0);
 CHECK(w.integrated.core.outstanding==0 && w.integrated.reserved_slots==1);
 CHECK(w.integrated.registry.count==0);
 CHECK((w.integrated.core.degradation_causes &
       RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT)!=0);
 CHECK(callbacks==0);
 CHECK(raya_submitter_snapshot(&s,&lifecycle,&queued,&producers,&active,&leases));
 CHECK(lifecycle==RAYA_SUBMITTER_DEGRADED);
 CHECK(queued==0 && producers==0 && active==0 && leases==1);
 CHECK(!raya_integrated_adapter_destroy(&a));
 /* OS-accepted I/O and its exact handles/buffer remain retained until process exit. */
 (void)client; (void)server;
 return 0;
}

#ifdef RAYA_ADAPTER_TESTING
typedef enum {
 RAYA_RETAINED_VALID=1,
 RAYA_RETAINED_INVALID=0
} raya_retained_validation_result;
static raya_retained_validation_result validate_retained_snapshot(
 const raya_adapter_test_retained_item *adapter_items,size_t adapter_count,
 const raya_worker_test_retained_item *worker_items,size_t worker_count);
#endif

static int test_ack_signal_failure_retains_after_resource_release(BOOL fail_wake_event) {
 raya_integrated_submit_adapter a; raya_dedicated_submitter s; raya_managed_worker w;
 api_state state; volatile LONG callbacks=0;
 HANDLE server=INVALID_HANDLE_VALUE,client=INVALID_HANDLE_VALUE;
 raya_submitter_lifecycle lifecycle=RAYA_SUBMITTER_RUNNING;
 size_t queued=0,producers=0,active=0,leases=0;
 DWORD written=0,wait_result;
 raya_managed_operation direct_retained,partial_retained;
 raya_adapter_retained_record_v1 adapter_v1[1];
 raya_worker_retained_record_v1 worker_v1[4];
 size_t inventory_required=0;
 LONG submitter_entrants=0;
 ZeroMemory(&partial_retained,sizeof(partial_retained));
 ZeroMemory(adapter_v1,sizeof(adapter_v1));
 ZeroMemory(worker_v1,sizeof(worker_v1));
 HANDLE direct_pipe=NULL,direct_event=NULL;
#ifdef RAYA_ADAPTER_TESTING
 size_t adapter_count=0,worker_count=0;
 raya_adapter_test_retained_item adapter_items[2];
 raya_worker_test_retained_item worker_items[4];
#endif
 ZeroMemory(&state,sizeof(state));
 state.ack_fail_wake=fail_wake_event;
 server=CreateNamedPipeW(fail_wake_event ?
     L"\\\\.\\pipe\\raya-adapter-api-ack-wake-fail" :
     L"\\\\.\\pipe\\raya-adapter-api-ack-changed-fail",
     PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
     PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,4096,4096,0,NULL);
 CHECK(server!=INVALID_HANDLE_VALUE);
 client=CreateFileW(fail_wake_event ?
     L"\\\\.\\pipe\\raya-adapter-api-ack-wake-fail" :
     L"\\\\.\\pipe\\raya-adapter-api-ack-changed-fail",
     GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
 CHECK(client!=INVALID_HANDLE_VALUE);
 CHECK(setup_ack_failure(&a,&s,&w,&state));
 CHECK(raya_integrated_adapter_submit(&a,server,RAYA_MANAGED_PIPE_READ,NULL,1,
       completion,(void *)&callbacks)==RAYA_ADAPTER_QUEUED);
 CHECK(WriteFile(client,"A",1,&written,NULL));
 CHECK(written==1);
 {
  DWORD deadline=GetTickCount()+5000;
  while ((!state.ack_failure_injected ||
          raya_integrated_adapter_retained_count(&a)==0) &&
         (LONG)(deadline-GetTickCount())>0) Sleep(1);
 }
 CHECK(state.worker_close_calls==2);
 CHECK(state.close_calls==0);
 CHECK(state.ack_failure_injected==TRUE);
 CHECK(callbacks==0);
 CHECK(raya_integrated_adapter_retained_count(&a)==1);
 CHECK(a.active_count==1 && a.retained_head!=NULL);
 CHECK(w.retained_head->owner.state==RAYA_PIPE_OWNER_RELEASED);
 CHECK(w.retained_head->finalizer.state==RAYA_FINALIZER_RELEASED);
 CHECK(w.retained_count==1 && w.retained_head!=NULL);
 CHECK(w.integrated.core.outstanding==0 && w.integrated.reserved_slots==0);
 CHECK((w.integrated.core.degradation_causes &
       RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT)!=0);
 CHECK(raya_submitter_snapshot(&s,&lifecycle,&queued,&producers,&active,&leases));
 CHECK(lifecycle==RAYA_SUBMITTER_DEGRADED);
 CHECK(queued==0 && producers==0 && active==0 && leases==0);
direct_pipe=CreateEventW(NULL,TRUE,FALSE,NULL);
 direct_event=CreateEventW(NULL,TRUE,FALSE,NULL);
 CHECK(direct_pipe && direct_event);
 CHECK(raya_managed_operation_init(&direct_retained,direct_pipe,direct_event,NULL,0,test_noop_release,NULL));
 CHECK(raya_managed_worker_register_operation(&w,&direct_retained,RAYA_WORKER_INVENTORY_DIRECT,0,0));
 EnterCriticalSection(&w.lock);
 CHECK(raya_managed_worker_publish_retained_locked(&w,&direct_retained,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,RAYA_WORKER_RETAIN_STAGE_DIRECT));
 CHECK(direct_retained.first_retain_cause==RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
 CHECK(direct_retained.first_retain_stage==RAYA_WORKER_RETAIN_STAGE_DIRECT);
 CHECK(!raya_managed_worker_publish_retained_locked(&w,&direct_retained,RAYA_RECOVERY_CAUSE_HANDLE_CLOSE,RAYA_WORKER_RETAIN_STAGE_POST_TERMINAL));
 CHECK(direct_retained.first_retain_cause==RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
 CHECK(direct_retained.first_retain_stage==RAYA_WORKER_RETAIN_STAGE_DIRECT);
 LeaveCriticalSection(&w.lock);
 CHECK(w.retained_count==2);
 EnterCriticalSection(&w.lock);
 CHECK(raya_managed_worker_publish_retained_locked(&w,&partial_retained,
       RAYA_RECOVERY_CAUSE_HANDLE_CLOSE,RAYA_WORKER_RETAIN_STAGE_PARTIAL_RECORD));
 LeaveCriticalSection(&w.lock);
 CHECK(w.retained_count==3);

 CHECK(!raya_integrated_adapter_destroy(&a));
 CHECK(!raya_submitter_shutdown(&s));
 wait_result=WaitForSingleObject(s.thread,5000);
 CHECK(wait_result==WAIT_OBJECT_0);
 CHECK(!raya_submitter_join(&s,100));
 CHECK(s.joined==TRUE && s.thread==NULL);
 CHECK(!raya_submitter_destroy(&s));
 CHECK(!raya_managed_worker_shutdown(&w));
 CHECK(WaitForSingleObject(w.thread,5000)==WAIT_OBJECT_0);
 CHECK(!raya_managed_worker_join(&w,100));
 CHECK(w.joined==TRUE && w.thread==NULL);
#ifdef RAYA_ADAPTER_TESTING
 CHECK(raya_adapter_test_retained_snapshot(&a,adapter_items,2,&adapter_count,
       worker_items,4,&worker_count)==1);
 CHECK(adapter_count==1 && worker_count==3);
  CHECK(adapter_items[0].adapter_instance_id!=0);
  CHECK(adapter_items[0].stable_operation_id==1);
  CHECK(adapter_items[0].first_retain_cause!=0);
  CHECK(adapter_items[0].first_retain_stage!=RAYA_ADAPTER_RETAIN_STAGE_UNKNOWN);
 CHECK(validate_retained_snapshot(adapter_items,adapter_count,
       worker_items,worker_count)==RAYA_RETAINED_VALID);
 { size_t k; int direct_seen=0,adapter_seen=0;
   for(k=0;k<worker_count;++k) {
     if(worker_items[k].inventory_kind==RAYA_WORKER_INVENTORY_DIRECT) { direct_seen=1; CHECK(worker_items[k].identity_valid); CHECK(worker_items[k].worker_epoch==w.worker_epoch); CHECK(!worker_items[k].adapter_identity_valid); }
     else if(worker_items[k].inventory_kind==RAYA_WORKER_INVENTORY_ADAPTER_ASSOCIATED) { adapter_seen=1; CHECK(worker_items[k].identity_valid); CHECK(worker_items[k].adapter_identity_valid); }
   }
   CHECK(direct_seen && adapter_seen);
 }
 { size_t k; int stable_pair_seen=0;
   for(k=0;k<worker_count;++k) if(worker_items[k].inventory_kind==RAYA_WORKER_INVENTORY_ADAPTER_ASSOCIATED && worker_items[k].adapter_identity_valid && worker_items[k].adapter_instance_id==adapter_items[0].adapter_instance_id && worker_items[k].adapter_operation_id==adapter_items[0].stable_operation_id) { stable_pair_seen=1; CHECK(worker_items[k].operation_id==adapter_items[0].operation_id); }
   CHECK(stable_pair_seen);
 }
 CHECK((adapter_items[0].flags & 31u)==31u);
 CHECK(adapter_items[0].owner_state==RAYA_PIPE_OWNER_RELEASED);
 CHECK(adapter_items[0].terminal_collection_recorded==1u);
 CHECK(adapter_items[0].completion_error==ERROR_SUCCESS);
 CHECK(adapter_items[0].finalizer_state==RAYA_FINALIZER_RELEASED);
 CHECK(adapter_items[0].finalizer_close_attempts==2);
 CHECK(adapter_items[0].finalizer_successful_closes==2);
 CHECK(adapter_items[0].finalizer_release_commits==1);
 CHECK(adapter_items[0].reservation_state==RAYA_INTEGRATED_RESERVATION_RELEASED);
 CHECK(adapter_items[0].registry_matches==0);
 CHECK(adapter_items[0].worker_reserved_slots==0);
 CHECK(adapter_items[0].worker_outstanding==0);
 CHECK(adapter_items[0].worker_queued==0);
 CHECK(adapter_items[0].worker_caller_active==0);
 CHECK(adapter_items[0].worker_registry_count==0);
 CHECK((adapter_items[0].worker_degradation_causes &
       RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT)!=0);
 CHECK(adapter_items[0].submitter_request_state==RAYA_SUBMITTER_REQ_ACKED);
#endif
 inventory_required=99;
 CHECK(raya_integrated_adapter_copy_retained_inventory_v1(&a,
       RAYA_ADAPTER_RETAINED_RECORD_VERSION_1,adapter_v1,1,&inventory_required)==
       RAYA_RETAINED_INVENTORY_NOT_QUIESCENT);
 CHECK(inventory_required==0);
 CHECK(raya_submitter_admission_close(&s,&submitter_entrants));
 CHECK(submitter_entrants==0);
 adapter_v1[0].adapter_instance_id=0x1234u;
 inventory_required=0;
 CHECK(raya_integrated_adapter_copy_retained_inventory_v1(&a,
       RAYA_ADAPTER_RETAINED_RECORD_VERSION_1,NULL,0,&inventory_required)==
       RAYA_RETAINED_INVENTORY_INSUFFICIENT_CAPACITY);
 CHECK(inventory_required==1 && adapter_v1[0].adapter_instance_id==0x1234u);
 inventory_required=99;
 CHECK(raya_integrated_adapter_copy_retained_inventory_v1(&a,2,
       adapter_v1,1,&inventory_required)==RAYA_RETAINED_INVENTORY_UNSUPPORTED_VERSION);
 CHECK(inventory_required==0);
 CHECK(raya_integrated_adapter_copy_retained_inventory_v1(&a,
       RAYA_ADAPTER_RETAINED_RECORD_VERSION_1,adapter_v1,1,&inventory_required)==
       RAYA_RETAINED_INVENTORY_OK);
 CHECK(inventory_required==1 && adapter_v1[0].struct_size==sizeof(adapter_v1[0]));
 CHECK(adapter_v1[0].version==RAYA_ADAPTER_RETAINED_RECORD_VERSION_1);
 CHECK(adapter_v1[0].adapter_instance_id==a.instance_id);
 CHECK(adapter_v1[0].adapter_operation_id==1);
 CHECK(adapter_v1[0].first_retain_cause!=0);
 CHECK(adapter_v1[0].first_retain_stage==RAYA_ADAPTER_RETAIN_STAGE_SUBMITTER_ACK);
 worker_v1[0].worker_epoch=0x5678u;
 inventory_required=0;
 CHECK(raya_managed_worker_copy_retained_inventory_v1(&w,
       RAYA_WORKER_RETAINED_RECORD_VERSION_1,NULL,0,&inventory_required)==
       RAYA_RETAINED_INVENTORY_INSUFFICIENT_CAPACITY);
 CHECK(inventory_required==3 && worker_v1[0].worker_epoch==0x5678u);
 CHECK(raya_managed_worker_copy_retained_inventory_v1(&w,
       RAYA_WORKER_RETAINED_RECORD_VERSION_1,worker_v1,4,&inventory_required)==
       RAYA_RETAINED_INVENTORY_OK);
 CHECK(inventory_required==3);
 { size_t k; int adapter_link=0,direct_record=0,partial_record=0;
   for(k=0;k<inventory_required;++k) {
     CHECK(worker_v1[k].struct_size==sizeof(worker_v1[k]));
     CHECK(worker_v1[k].version==RAYA_WORKER_RETAINED_RECORD_VERSION_1);
     if(worker_v1[k].inventory_kind==RAYA_WORKER_INVENTORY_ADAPTER_ASSOCIATED) {
       adapter_link=1;
       CHECK(worker_v1[k].adapter_identity_valid);
       CHECK(worker_v1[k].adapter_instance_id==adapter_v1[0].adapter_instance_id);
       CHECK(worker_v1[k].adapter_operation_id==adapter_v1[0].adapter_operation_id);
     } else if(worker_v1[k].inventory_kind==RAYA_WORKER_INVENTORY_DIRECT) {
       direct_record=1;
       CHECK(worker_v1[k].identity_valid && !worker_v1[k].adapter_identity_valid);
     } else if(!worker_v1[k].identity_valid &&
               worker_v1[k].inventory_kind==RAYA_WORKER_INVENTORY_UNSPECIFIED) {
       partial_record=1;
       CHECK(worker_v1[k].worker_epoch==0 && worker_v1[k].worker_operation_id==0);
       CHECK(!worker_v1[k].adapter_identity_valid);
       CHECK(worker_v1[k].first_retain_stage==RAYA_WORKER_RETAIN_STAGE_PARTIAL_RECORD);
     }
   }
   CHECK(adapter_link && direct_record && partial_record);
 }
 CHECK(a.retained_count==1 && a.active_count==1);
 CHECK(w.retained_count==3 && w.retained_head==&partial_retained);
#ifdef RAYA_ADAPTER_TESTING

  CHECK(adapter_items[0].issue_effect==RAYA_SUBMITTER_EFFECT_COMMITTED);
  CHECK(adapter_items[0].publish_effect==RAYA_SUBMITTER_EFFECT_COMMITTED);
 CHECK(adapter_items[0].submitter_lifecycle==RAYA_SUBMITTER_DEGRADED);
 CHECK(adapter_items[0].submitter_issuer_leases==0);
 CHECK(adapter_items[0].submitter_active_issue==0);
 CHECK(adapter_items[0].submitter_active_producers==0);
 CHECK(adapter_items[0].submitter_queue_count==0);
#endif
 EnterCriticalSection(&w.lock);
 { raya_managed_operation **link=&w.retained_head; while(*link && *link!=&direct_retained) link=&(*link)->retained_next; CHECK(*link==&direct_retained); *link=direct_retained.retained_next; --w.retained_count; direct_retained.retained_next=NULL; }
 LeaveCriticalSection(&w.lock);
 CHECK(!raya_managed_worker_destroy(&w));
 CHECK(CloseHandle(direct_pipe)); CHECK(CloseHandle(direct_event));
 unregister_ack_state(&state);
  unregister_worker_close_state(&state);
  CHECK(state.worker_close_handles[0]==NULL && state.worker_close_handles[1]==NULL);
 CHECK(CloseHandle(client));
 CHECK(CloseHandle(server));
 return 0;
}



#ifdef RAYA_ADAPTER_TESTING
static raya_retained_validation_result validate_retained_snapshot(
 const raya_adapter_test_retained_item *adapter_items,size_t adapter_count,
 const raya_worker_test_retained_item *worker_items,size_t worker_count) {
 size_t i,j; unsigned f; int worker_found; ULONGLONG observed_worker_epoch=0;
 if ((adapter_count && !adapter_items) || (worker_count && !worker_items))
  return RAYA_RETAINED_INVALID;
 for (i=0;i<adapter_count;++i) {
  f=adapter_items[i].flags;
  if (adapter_items[i].issue_effect>RAYA_SUBMITTER_EFFECT_UNKNOWN ||
      adapter_items[i].publish_effect>RAYA_SUBMITTER_EFFECT_UNKNOWN)
   return RAYA_RETAINED_INVALID;
  if (adapter_items[i].submitter_request_state==RAYA_SUBMITTER_REQ_NOT_ISSUED &&
      (adapter_items[i].issue_effect!=RAYA_SUBMITTER_EFFECT_NOT_ATTEMPTED ||
       adapter_items[i].publish_effect!=RAYA_SUBMITTER_EFFECT_NOT_ATTEMPTED))
   return RAYA_RETAINED_INVALID;
  if (adapter_items[i].submitter_request_state==RAYA_SUBMITTER_REQ_ACKED &&
      (adapter_items[i].issue_effect!=RAYA_SUBMITTER_EFFECT_COMMITTED ||
       adapter_items[i].publish_effect!=RAYA_SUBMITTER_EFFECT_COMMITTED))
   return RAYA_RETAINED_INVALID;
  if (!adapter_items[i].adapter_instance_id ||
      !adapter_items[i].stable_operation_id ||
      !adapter_items[i].first_retain_cause ||
      adapter_items[i].first_retain_stage==RAYA_ADAPTER_RETAIN_STAGE_UNKNOWN)
   return RAYA_RETAINED_INVALID;
  if ((f & 4u) && adapter_items[i].first_retain_stage!=
      RAYA_ADAPTER_RETAIN_STAGE_SUBMITTER_ACK) return RAYA_RETAINED_INVALID;
  if (!adapter_items[i].envelope_id || !adapter_items[i].operation_id ||
      !(f & 1u) || (f & ~31u)) return RAYA_RETAINED_INVALID;
  if ((f & 2u) && !(f & 8u)) return RAYA_RETAINED_INVALID;
  if ((f & 16u) && !(f & 8u)) return RAYA_RETAINED_INVALID;
  if ((f & 16u) && !(f & 8u)) return RAYA_RETAINED_INVALID;
  if ((f & 4u) && ((f & (2u|8u|16u)) != (2u|8u|16u)))
   return RAYA_RETAINED_INVALID;
  /* Reject out-of-domain state and impossible registry multiplicity. */
  if (adapter_items[i].owner_state<RAYA_PIPE_OWNER_CALLER_PENDING ||
      adapter_items[i].owner_state>RAYA_PIPE_OWNER_RELEASED ||
      adapter_items[i].finalizer_state>RAYA_FINALIZER_RELEASED ||
      adapter_items[i].operation_state<RAYA_REAPER_OP_CALLER_PENDING ||
      adapter_items[i].operation_state>RAYA_REAPER_OP_CALLER_TERMINAL ||
      adapter_items[i].collect_state>RAYA_INTEGRATED_UNCERTAIN ||
      adapter_items[i].reservation_state>RAYA_INTEGRATED_RESERVATION_TERMINAL_HELD ||
      adapter_items[i].registry_matches>1u)
   return RAYA_RETAINED_INVALID;
  /* Cross-check the independently recorded terminal/release planes. */
  if (adapter_items[i].owner_terminal_collections>1u ||
      adapter_items[i].owner_release_commits>1u ||
      adapter_items[i].finalizer_close_attempts>2u ||
      adapter_items[i].finalizer_successful_closes>2u ||
      adapter_items[i].finalizer_successful_closes>
       adapter_items[i].finalizer_close_attempts ||
      adapter_items[i].finalizer_release_commits>1u ||
      adapter_items[i].terminal_collection_recorded!=
       (unsigned)(adapter_items[i].owner_terminal_collections==1u))
   return RAYA_RETAINED_INVALID;
  /* Owner terminal states must agree with the terminal-collection counter. */
  if ((adapter_items[i].owner_state==RAYA_PIPE_OWNER_CALLER_TERMINAL ||
       adapter_items[i].owner_state==RAYA_PIPE_OWNER_REAPER_TERMINAL ||
       adapter_items[i].owner_state==RAYA_PIPE_OWNER_RELEASED) !=
      (adapter_items[i].owner_terminal_collections==1u))
   return RAYA_RETAINED_INVALID;
  /* Finalizer completion is valid only after both close attempts succeeded. */
  if ((adapter_items[i].finalizer_state==RAYA_FINALIZER_HANDLES_CLOSED ||
       adapter_items[i].finalizer_state==RAYA_FINALIZER_RELEASED) &&
      (adapter_items[i].finalizer_close_attempts!=2u ||
       adapter_items[i].finalizer_successful_closes!=2u))
   return RAYA_RETAINED_INVALID;
  if (adapter_items[i].finalizer_state==RAYA_FINALIZER_CLOSE_UNCERTAIN &&
      adapter_items[i].finalizer_close_attempts<=
       adapter_items[i].finalizer_successful_closes)
   return RAYA_RETAINED_INVALID;
  if (!adapter_items[i].terminal_collection_recorded &&
      (adapter_items[i].finalizer_release_commits ||
       adapter_items[i].owner_release_commits))
   return RAYA_RETAINED_INVALID;
  if (adapter_items[i].finalizer_release_commits &&
      (!adapter_items[i].terminal_collection_recorded ||
       adapter_items[i].finalizer_state!=RAYA_FINALIZER_RELEASED ||
       adapter_items[i].finalizer_successful_closes!=2u))
   return RAYA_RETAINED_INVALID;
  if ((adapter_items[i].finalizer_state==RAYA_FINALIZER_RELEASED) !=
      (adapter_items[i].finalizer_release_commits==1u))
   return RAYA_RETAINED_INVALID;
  if (adapter_items[i].owner_release_commits &&
      (!adapter_items[i].finalizer_release_commits ||
       adapter_items[i].owner_state!=RAYA_PIPE_OWNER_RELEASED))
   return RAYA_RETAINED_INVALID;
  if ((adapter_items[i].owner_state==RAYA_PIPE_OWNER_RELEASED) !=
      (adapter_items[i].owner_release_commits==1u))
   return RAYA_RETAINED_INVALID;
  /* Reservation state constrains operation state and registry membership. */
  if (adapter_items[i].reservation_state==RAYA_INTEGRATED_RESERVATION_HELD &&
      (adapter_items[i].registry_matches!=0u ||
       (adapter_items[i].operation_state!=RAYA_REAPER_OP_CALLER_PENDING &&
        adapter_items[i].operation_state!=RAYA_REAPER_OP_CALLER_TERMINAL)))
   return RAYA_RETAINED_INVALID;
  if (adapter_items[i].reservation_state==RAYA_INTEGRATED_RESERVATION_HANDED_OFF &&
      adapter_items[i].operation_state!=RAYA_REAPER_OP_QUEUED &&
      adapter_items[i].operation_state!=RAYA_REAPER_OP_WORKER_PENDING)
   return RAYA_RETAINED_INVALID;
  if (adapter_items[i].reservation_state==RAYA_INTEGRATED_RESERVATION_TERMINAL_HELD &&
      (adapter_items[i].operation_state!=RAYA_REAPER_OP_TERMINAL_HELD ||
       adapter_items[i].registry_matches!=0u))
   return RAYA_RETAINED_INVALID;
  if (adapter_items[i].reservation_state==RAYA_INTEGRATED_RESERVATION_RELEASED &&
      (adapter_items[i].registry_matches!=0u ||
       (adapter_items[i].operation_state!=RAYA_REAPER_OP_CALLER_TERMINAL &&
        adapter_items[i].operation_state!=RAYA_REAPER_OP_WORKER_TERMINAL &&
        adapter_items[i].operation_state!=RAYA_REAPER_OP_RELEASED)))
   return RAYA_RETAINED_INVALID;
  if (adapter_items[i].registry_matches==1u &&
      (adapter_items[i].reservation_state!=RAYA_INTEGRATED_RESERVATION_HANDED_OFF ||
       adapter_items[i].operation_state!=RAYA_REAPER_OP_WORKER_PENDING))
   return RAYA_RETAINED_INVALID;
  if (adapter_items[i].reservation_state==RAYA_INTEGRATED_RESERVATION_RELEASED &&
      (!adapter_items[i].owner_release_commits ||
       !adapter_items[i].finalizer_release_commits ||
       adapter_items[i].registry_matches!=0u))
   return RAYA_RETAINED_INVALID;
  if ((f & 4u) &&
      (!adapter_items[i].owner_release_commits ||
       !adapter_items[i].finalizer_release_commits ||
       adapter_items[i].reservation_state!=RAYA_INTEGRATED_RESERVATION_RELEASED ||
       adapter_items[i].registry_matches!=0u))
   return RAYA_RETAINED_INVALID;
  for (j=i+1;j<adapter_count;++j)
   if (adapter_items[i].envelope_id==adapter_items[j].envelope_id ||
       adapter_items[i].operation_id==adapter_items[j].operation_id ||
       (adapter_items[i].adapter_instance_id==adapter_items[j].adapter_instance_id &&
        adapter_items[i].stable_operation_id==adapter_items[j].stable_operation_id))
    return RAYA_RETAINED_INVALID;
  worker_found=0;
  for (j=0;j<worker_count;++j) {
   const raya_worker_test_retained_item *wi=&worker_items[j];
   if (wi->identity_valid && wi->adapter_identity_valid &&
       wi->adapter_instance_id==adapter_items[i].adapter_instance_id &&
       wi->adapter_operation_id==adapter_items[i].stable_operation_id)
    ++worker_found;
   else if (!wi->identity_valid &&
            wi->operation_address==adapter_items[i].operation_id)
    ++worker_found; /* Address compatibility is limited to identity-less records. */
  }
  if ((f & 4u) || !(f & 2u)) {
   if (worker_found!=1) return RAYA_RETAINED_INVALID;
  } else {
   /* Worker-owned, not terminal-released: Adapter-only pending-I/O class. */
   if (worker_found!=0) return RAYA_RETAINED_INVALID;
  }
 }
 for (i=0;i<worker_count;++i) {
  const raya_worker_test_retained_item *wi=&worker_items[i];
  if (!wi->operation_address || !wi->operation_id ||
      wi->operation_address!=wi->operation_id ||
      !wi->first_retain_cause ||
      wi->first_retain_stage==RAYA_WORKER_RETAIN_STAGE_UNKNOWN ||
      wi->first_retain_stage>RAYA_WORKER_RETAIN_STAGE_POST_TERMINAL)
   return RAYA_RETAINED_INVALID;
  if (wi->identity_valid) {
   if (!wi->worker_epoch || !wi->worker_operation_id ||
       (observed_worker_epoch && observed_worker_epoch!=wi->worker_epoch) ||
       (wi->inventory_kind!=RAYA_WORKER_INVENTORY_DIRECT &&
        wi->inventory_kind!=RAYA_WORKER_INVENTORY_ADAPTER_ASSOCIATED))
    return RAYA_RETAINED_INVALID;
   observed_worker_epoch=wi->worker_epoch;
   if (wi->inventory_kind==RAYA_WORKER_INVENTORY_DIRECT) {
    if (wi->adapter_identity_valid || wi->adapter_instance_id ||
        wi->adapter_operation_id ||
        (wi->first_retain_stage!=RAYA_WORKER_RETAIN_STAGE_DIRECT &&
         wi->first_retain_stage!=RAYA_WORKER_RETAIN_STAGE_POST_TERMINAL))
     return RAYA_RETAINED_INVALID;
   } else if (!wi->adapter_identity_valid || !wi->adapter_instance_id ||
              !wi->adapter_operation_id ||
              (wi->first_retain_stage!=RAYA_WORKER_RETAIN_STAGE_ADAPTER_ASSOCIATED &&
               wi->first_retain_stage!=RAYA_WORKER_RETAIN_STAGE_POST_TERMINAL))
    return RAYA_RETAINED_INVALID;
  } else if (wi->worker_epoch || wi->worker_operation_id ||
             wi->inventory_kind!=RAYA_WORKER_INVENTORY_UNSPECIFIED ||
             wi->adapter_identity_valid || wi->adapter_instance_id ||
             wi->adapter_operation_id ||
             (wi->first_retain_stage!=RAYA_WORKER_RETAIN_STAGE_PARTIAL_RECORD &&
              wi->first_retain_stage!=RAYA_WORKER_RETAIN_STAGE_POST_TERMINAL))
   return RAYA_RETAINED_INVALID;
  for (j=i+1;j<worker_count;++j) {
   const raya_worker_test_retained_item *wj=&worker_items[j];
   if (wi->operation_address==wj->operation_address)
    return RAYA_RETAINED_INVALID;
   if (wi->identity_valid && wj->identity_valid &&
       wi->worker_epoch==wj->worker_epoch &&
       wi->worker_operation_id==wj->worker_operation_id)
    return RAYA_RETAINED_INVALID;
   if (wi->identity_valid && wj->identity_valid &&
       wi->adapter_identity_valid && wj->adapter_identity_valid &&
       wi->adapter_instance_id==wj->adapter_instance_id &&
       wi->adapter_operation_id==wj->adapter_operation_id)
    return RAYA_RETAINED_INVALID;
  }
  worker_found=0;
  if (wi->identity_valid &&
      wi->inventory_kind==RAYA_WORKER_INVENTORY_ADAPTER_ASSOCIATED) {
   for (j=0;j<adapter_count;++j)
    if (wi->adapter_instance_id==adapter_items[j].adapter_instance_id &&
        wi->adapter_operation_id==adapter_items[j].stable_operation_id)
     ++worker_found;
   if (worker_found!=1) return RAYA_RETAINED_INVALID;
  } else if (!wi->identity_valid) {
   for (j=0;j<adapter_count;++j)
    if (wi->operation_address==adapter_items[j].operation_id)
     ++worker_found;
   if (worker_found>1) return RAYA_RETAINED_INVALID;
  }
 }
 return RAYA_RETAINED_VALID;
}

static int test_retained_snapshot_validator(void) {
 raya_adapter_test_retained_item a[2];
 raya_worker_test_retained_item w[2];
 ZeroMemory(a,sizeof(a)); ZeroMemory(w,sizeof(w));
 /* Post-terminal bookkeeping failure: dual membership is required. */
 a[0].envelope_id=1; a[0].operation_id=101; a[0].flags=1u|2u|4u|8u|16u;
 a[0].adapter_instance_id=1; a[0].stable_operation_id=1;
 a[0].first_retain_cause=1;
 a[0].first_retain_stage=RAYA_ADAPTER_RETAIN_STAGE_SUBMITTER_ACK;
 a[0].issue_effect=RAYA_SUBMITTER_EFFECT_COMMITTED;
 a[0].publish_effect=RAYA_SUBMITTER_EFFECT_COMMITTED;
 a[0].submitter_request_state=RAYA_SUBMITTER_REQ_ACKED;
 a[0].owner_state=RAYA_PIPE_OWNER_RELEASED;
 a[0].owner_terminal_collections=1; a[0].owner_release_commits=1;
 a[0].finalizer_state=RAYA_FINALIZER_RELEASED;
  a[0].finalizer_close_attempts=2;
 a[0].finalizer_successful_closes=2; a[0].finalizer_release_commits=1;
 a[0].reservation_state=RAYA_INTEGRATED_RESERVATION_RELEASED;
 a[0].operation_state=RAYA_REAPER_OP_RELEASED;
 a[0].collect_state=RAYA_INTEGRATED_COLLECTING;
 a[0].terminal_collection_recorded=1; a[0].registry_matches=0;
 w[0].operation_id=101;
w[0].operation_id=909; w[0].operation_address=909; /* Deliberately differs from Adapter address diagnostic. */
w[0].identity_valid=TRUE;
w[0].worker_epoch=9; w[0].worker_operation_id=1;
w[0].inventory_kind=RAYA_WORKER_INVENTORY_ADAPTER_ASSOCIATED;
w[0].adapter_identity_valid=TRUE;
w[0].adapter_instance_id=1; w[0].adapter_operation_id=1;
w[0].first_retain_cause=1;
w[0].first_retain_stage=RAYA_WORKER_RETAIN_STAGE_ADAPTER_ASSOCIATED;
w[0].inventory_kind=RAYA_WORKER_INVENTORY_DIRECT;
w[0].adapter_identity_valid=FALSE; w[0].adapter_instance_id=0; w[0].adapter_operation_id=0;
w[0].first_retain_stage=RAYA_WORKER_RETAIN_STAGE_DIRECT;
CHECK(validate_retained_snapshot(NULL,0,w,1)==RAYA_RETAINED_VALID);
w[1]=w[0]; w[1].operation_id=910; w[1].operation_address=910;
w[1].worker_operation_id=2;
CHECK(validate_retained_snapshot(NULL,0,w,2)==RAYA_RETAINED_VALID);
w[1].worker_epoch=10;
CHECK(validate_retained_snapshot(NULL,0,w,2)==RAYA_RETAINED_INVALID);
w[1].worker_epoch=9; w[1].worker_operation_id=1;
CHECK(validate_retained_snapshot(NULL,0,w,2)==RAYA_RETAINED_INVALID);
w[0].inventory_kind=RAYA_WORKER_INVENTORY_ADAPTER_ASSOCIATED;
w[0].adapter_identity_valid=TRUE; w[0].adapter_instance_id=1; w[0].adapter_operation_id=1;
w[0].first_retain_stage=RAYA_WORKER_RETAIN_STAGE_ADAPTER_ASSOCIATED; CHECK(validate_retained_snapshot(a,1,w,1)==RAYA_RETAINED_VALID);  /* Stable Adapter/Worker identity, not equal pointer diagnostics, establishes membership. */
  w[0].adapter_operation_id=2;
  CHECK(validate_retained_snapshot(a,1,w,1)==RAYA_RETAINED_INVALID);
  w[0].adapter_operation_id=1;
  /* Identity-less partial records retain the explicit address-only compatibility path. */
  w[0].identity_valid=FALSE; w[0].worker_epoch=0; w[0].worker_operation_id=0;
  w[0].inventory_kind=RAYA_WORKER_INVENTORY_UNSPECIFIED;
  w[0].adapter_identity_valid=FALSE; w[0].adapter_instance_id=0; w[0].adapter_operation_id=0;
  w[0].operation_id=101; w[0].operation_address=101;
  w[0].first_retain_stage=RAYA_WORKER_RETAIN_STAGE_PARTIAL_RECORD;
  CHECK(validate_retained_snapshot(a,1,w,1)==RAYA_RETAINED_VALID);
  w[0].operation_id=909; w[0].operation_address=909;
  CHECK(validate_retained_snapshot(a,1,w,1)==RAYA_RETAINED_INVALID);
  w[0].identity_valid=TRUE; w[0].worker_epoch=9; w[0].worker_operation_id=1;
  w[0].inventory_kind=RAYA_WORKER_INVENTORY_ADAPTER_ASSOCIATED;
  w[0].adapter_identity_valid=TRUE; w[0].adapter_instance_id=1; w[0].adapter_operation_id=1;
  w[0].operation_id=909; w[0].operation_address=909;
  w[0].first_retain_stage=RAYA_WORKER_RETAIN_STAGE_ADAPTER_ASSOCIATED;
w[1]=w[0]; w[1].operation_id=910; w[1].operation_address=910;
w[1].worker_operation_id=2; w[1].adapter_operation_id=2;
a[1]=a[0]; a[1].envelope_id=2; a[1].operation_id=910; a[1].stable_operation_id=2;
CHECK(validate_retained_snapshot(a,2,w,2)==RAYA_RETAINED_VALID);
w[1].worker_epoch=10;
CHECK(validate_retained_snapshot(a,2,w,2)==RAYA_RETAINED_INVALID);
w[1].worker_epoch=9; w[1].worker_operation_id=1;
CHECK(validate_retained_snapshot(a,2,w,2)==RAYA_RETAINED_INVALID);
w[1].worker_operation_id=2; w[1].adapter_operation_id=1;
CHECK(validate_retained_snapshot(a,2,w,2)==RAYA_RETAINED_INVALID);
w[1]=w[0]; /* Owner terminal state without terminal-collection evidence is rejected. */
  a[0].owner_terminal_collections=0;
  a[0].terminal_collection_recorded=0;
  CHECK(validate_retained_snapshot(a,1,w,1)==RAYA_RETAINED_INVALID);
  a[0].owner_terminal_collections=1;
  a[0].terminal_collection_recorded=1;
  /* Finalizer successful closes cannot exceed close attempts. */
  a[0].finalizer_close_attempts=1;
  CHECK(validate_retained_snapshot(a,1,w,1)==RAYA_RETAINED_INVALID);
  a[0].finalizer_close_attempts=2;
  /* Operation and collection enum domains are validated as well. */
 a[0].operation_state=0;
 CHECK(validate_retained_snapshot(a,1,w,1)==RAYA_RETAINED_INVALID);
 a[0].operation_state=RAYA_REAPER_OP_RELEASED;
 a[0].collect_state=3u;
 CHECK(validate_retained_snapshot(a,1,w,1)==RAYA_RETAINED_INVALID);
 a[0].collect_state=RAYA_INTEGRATED_COLLECTING;
 /* Out-of-domain reservation and duplicate registry membership are rejected. */
 a[0].reservation_state=99u;
 CHECK(validate_retained_snapshot(a,1,w,1)==RAYA_RETAINED_INVALID);
 a[0].reservation_state=RAYA_INTEGRATED_RESERVATION_RELEASED;
 a[0].registry_matches=2u;
 CHECK(validate_retained_snapshot(a,1,w,1)==RAYA_RETAINED_INVALID);
 a[0].registry_matches=0;
 /* Contradictory finalizer and owner release evidence is rejected. */
 a[0].finalizer_successful_closes=1;
 CHECK(validate_retained_snapshot(a,1,w,1)==RAYA_RETAINED_INVALID);
 a[0].finalizer_successful_closes=2;
 a[0].owner_release_commits=0;
 CHECK(validate_retained_snapshot(a,1,w,1)==RAYA_RETAINED_INVALID);
 a[0].owner_release_commits=1;
 /* A released reservation cannot still be in the Worker registry. */
  a[0].registry_matches=1;
  CHECK(validate_retained_snapshot(a,1,w,1)==RAYA_RETAINED_INVALID);
  a[0].registry_matches=0;
  /* A held reservation cannot describe a Worker-pending operation. */
  a[0].reservation_state=RAYA_INTEGRATED_RESERVATION_HELD;
  CHECK(validate_retained_snapshot(a,1,w,1)==RAYA_RETAINED_INVALID);
  a[0].reservation_state=RAYA_INTEGRATED_RESERVATION_RELEASED;
  /* Pre-handoff uncertain ownership: dual membership, worker-owned is false. */
 a[0].flags=1u|8u;
 a[0].owner_state=RAYA_PIPE_OWNER_CALLER_UNCERTAIN;
 a[0].owner_terminal_collections=0; a[0].owner_release_commits=0;
 a[0].finalizer_state=RAYA_FINALIZER_NOT_STARTED;
  a[0].finalizer_close_attempts=0;
 a[0].finalizer_successful_closes=0; a[0].finalizer_release_commits=0;
 a[0].reservation_state=RAYA_INTEGRATED_RESERVATION_HELD;
 a[0].operation_state=RAYA_REAPER_OP_CALLER_PENDING;
 a[0].collect_state=RAYA_INTEGRATED_COLLECT_PENDING;
 a[0].terminal_collection_recorded=0;
 CHECK(validate_retained_snapshot(a,1,w,1)==RAYA_RETAINED_VALID);
 /* Worker-owned pending I/O: Adapter-only; counts need not match. */
 a[0].flags=1u|2u|8u;
 a[0].owner_state=RAYA_PIPE_OWNER_REAPER_PENDING;
 a[0].reservation_state=RAYA_INTEGRATED_RESERVATION_HANDED_OFF;
  a[0].operation_state=RAYA_REAPER_OP_QUEUED;
 CHECK(validate_retained_snapshot(a,1,w,0)==RAYA_RETAINED_VALID);
 CHECK(validate_retained_snapshot(a,1,w,1)==RAYA_RETAINED_INVALID);
 /* Duplicate envelope identity. */
 a[1]=a[0]; a[1].envelope_id=2; a[1].operation_id=202;
 CHECK(validate_retained_snapshot(a,2,w,0)==RAYA_RETAINED_INVALID); /* duplicate Adapter stable identity */
 a[1].stable_operation_id=2; a[1].operation_id=a[0].operation_id;
 /* Duplicate operation identity. */
 a[1].operation_id=a[0].operation_id;
 CHECK(validate_retained_snapshot(a,2,w,0)==RAYA_RETAINED_INVALID);
 /* Direct Worker inventory uniqueness is validated independently of Adapter inventory. */
w[0].identity_valid=TRUE; w[0].worker_epoch=9; w[0].worker_operation_id=1;
w[0].inventory_kind=RAYA_WORKER_INVENTORY_DIRECT;
w[0].adapter_identity_valid=FALSE; w[0].adapter_instance_id=0; w[0].adapter_operation_id=0;
w[0].first_retain_stage=RAYA_WORKER_RETAIN_STAGE_DIRECT;
w[1]=w[0]; w[1].operation_id=910; w[1].operation_address=910; w[1].worker_operation_id=2;
CHECK(validate_retained_snapshot(NULL,0,w,2)==RAYA_RETAINED_VALID);
w[1].worker_operation_id=1;
CHECK(validate_retained_snapshot(NULL,0,w,2)==RAYA_RETAINED_INVALID);
w[1].worker_operation_id=2; w[1].operation_id=909; w[1].operation_address=909;
CHECK(validate_retained_snapshot(NULL,0,w,2)==RAYA_RETAINED_INVALID);/* Missing required Worker membership for pre-handoff retention. */
 a[0].flags=1u|8u;
 CHECK(validate_retained_snapshot(a,1,w,0)==RAYA_RETAINED_INVALID);
 /* Contradictory lifecycle flags: terminal release without ownership/admission. */
 a[0].flags=1u|4u|16u;
 CHECK(validate_retained_snapshot(a,1,w,0)==RAYA_RETAINED_INVALID);
 /* Finished without admission. */
 a[0].flags=1u|16u;
 CHECK(validate_retained_snapshot(a,1,w,0)==RAYA_RETAINED_INVALID);
 /* Zero identities and unknown flag bits. */
 a[0].flags=1u|2u|8u; a[0].envelope_id=0;
 CHECK(validate_retained_snapshot(a,1,w,0)==RAYA_RETAINED_INVALID);
 a[0].envelope_id=1; a[0].flags|=32u;
 CHECK(validate_retained_snapshot(a,1,w,0)==RAYA_RETAINED_INVALID);
 return 0;
}
#endif

static int test_finalizer_close_failure_retains(void) {
 raya_integrated_submit_adapter a; raya_dedicated_submitter s; raya_managed_worker w;
 api_state state; volatile LONG callbacks=0;
 HANDLE server=INVALID_HANDLE_VALUE,client=INVALID_HANDLE_VALUE;
 raya_submitter_lifecycle lifecycle=RAYA_SUBMITTER_RUNNING;
 size_t queued=0,producers=0,active=0,leases=0;
#ifdef RAYA_ADAPTER_TESTING
 size_t adapter_count=0,worker_count=0;
 raya_adapter_test_retained_item adapter_items[2];
 raya_worker_test_retained_item worker_items[4];
#endif
 ZeroMemory(&state,sizeof(state)); state.fail_read=TRUE; state.fail_close=TRUE;
 server=CreateNamedPipeW(L"\\\\.\\pipe\\raya-adapter-api-close-fail",
     PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
     PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,4096,4096,0,NULL);
 CHECK(server!=INVALID_HANDLE_VALUE);
 client=CreateFileW(L"\\\\.\\pipe\\raya-adapter-api-close-fail",
     GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
 CHECK(client!=INVALID_HANDLE_VALUE);
 CHECK(setup(&a,&s,&w,&state));
 CHECK(raya_integrated_adapter_submit(&a,server,RAYA_MANAGED_PIPE_READ,NULL,1,
       completion,(void *)&callbacks)==RAYA_ADAPTER_QUEUED);
 {
  DWORD deadline=GetTickCount()+5000;
  while ((raya_integrated_adapter_retained_count(&a)==0 ||
          !raya_submitter_snapshot(&s,&lifecycle,&queued,&producers,&active,&leases) ||
          leases!=0) && (LONG)(deadline-GetTickCount())>0) Sleep(1);
 }
 CHECK(callbacks==0);
 CHECK(state.close_calls==1);
 CHECK(raya_integrated_adapter_retained_count(&a)==1);
 CHECK(a.active_count==1);
 CHECK(w.retained_count==1);
 CHECK((w.integrated.core.degradation_causes & RAYA_RECOVERY_CAUSE_HANDLE_CLOSE)!=0);
 CHECK(w.integrated.core.outstanding==0 && w.integrated.reserved_slots==1);
 CHECK(w.integrated.core.caller_active==0);
 CHECK(raya_submitter_snapshot(&s,&lifecycle,&queued,&producers,&active,&leases));
 CHECK(queued==0 && producers==0 && active==0 && leases==0);
#ifdef RAYA_ADAPTER_TESTING
 CHECK(raya_adapter_test_retained_snapshot(&a,adapter_items,2,&adapter_count,
       worker_items,2,&worker_count)==0);
#endif
 CHECK(!raya_integrated_adapter_destroy(&a));
 CHECK(raya_submitter_shutdown(&s));
 CHECK(raya_submitter_join(&s,5000));
#ifdef RAYA_ADAPTER_TESTING
 CHECK(!raya_managed_worker_join(&w,5000));
 CHECK(w.joined==TRUE && w.thread==NULL);
 CHECK(raya_adapter_test_retained_snapshot(&a,adapter_items,2,&adapter_count,
       worker_items,4,&worker_count)==1);
 CHECK(adapter_count==1 && worker_count==1);
  CHECK(adapter_items[0].adapter_instance_id!=0);
  CHECK(adapter_items[0].stable_operation_id==1);
  CHECK(adapter_items[0].first_retain_cause!=0);
  CHECK(adapter_items[0].first_retain_stage!=RAYA_ADAPTER_RETAIN_STAGE_UNKNOWN);
 CHECK(validate_retained_snapshot(adapter_items,adapter_count,
       worker_items,worker_count)==RAYA_RETAINED_VALID);
 CHECK(raya_adapter_test_retained_snapshot(&a,adapter_items,0,&adapter_count,
       worker_items,2,&worker_count)==-1);
 CHECK(adapter_items[0].operation_id==worker_items[0].operation_id);
 CHECK((adapter_items[0].flags & 1u)!=0);
 CHECK((adapter_items[0].flags & 4u)==0);
 CHECK((adapter_items[0].flags & 8u)!=0);
 CHECK((adapter_items[0].flags & 16u)!=0);
 CHECK(adapter_items[0].owner_state==RAYA_PIPE_OWNER_CALLER_TERMINAL);
 CHECK(adapter_items[0].terminal_collection_recorded==1u);
 CHECK(adapter_items[0].finalizer_state==RAYA_FINALIZER_CLOSE_UNCERTAIN);
 CHECK(adapter_items[0].finalizer_close_attempts==1);
 CHECK(adapter_items[0].finalizer_successful_closes==0);
 CHECK(adapter_items[0].finalizer_release_commits==0);
 CHECK(adapter_items[0].reservation_state==RAYA_INTEGRATED_RESERVATION_HELD);
 CHECK(adapter_items[0].registry_matches==0);
 CHECK(adapter_items[0].worker_reserved_slots==1);
 CHECK(adapter_items[0].worker_outstanding==0);
 CHECK(adapter_items[0].worker_queued==0);
 CHECK(adapter_items[0].worker_caller_active==0);
 CHECK(adapter_items[0].worker_registry_count==0);
 CHECK((adapter_items[0].worker_degradation_causes &
       RAYA_RECOVERY_CAUSE_HANDLE_CLOSE)!=0);
 CHECK(adapter_items[0].submitter_request_state==RAYA_SUBMITTER_REQ_NOT_ISSUED);
  CHECK(adapter_items[0].issue_effect==RAYA_SUBMITTER_EFFECT_NOT_ATTEMPTED);
  CHECK(adapter_items[0].publish_effect==RAYA_SUBMITTER_EFFECT_NOT_ATTEMPTED);
 CHECK(adapter_items[0].submitter_lifecycle==RAYA_SUBMITTER_STOPPED);
 CHECK(adapter_items[0].submitter_issuer_leases==0);
 CHECK(adapter_items[0].submitter_active_issue==0);
 CHECK(adapter_items[0].submitter_active_producers==0);
 CHECK(adapter_items[0].submitter_queue_count==0);
 #endif
 CHECK(raya_submitter_destroy(&s));
 /* Snapshot is diagnostic only; retained resources remain untouched. */
 /* Worker, adapter envelope, and uncertain OS handles intentionally remain retained. */
 (void)client; (void)server;
 return 0;
}
typedef struct {
 raya_integrated_submit_adapter *adapter;
 HANDLE pipe;
 raya_adapter_result result;
} adapter_race_arg;
static DWORD WINAPI adapter_race_submit_thread(void *context) {
 adapter_race_arg *arg=(adapter_race_arg *)context;
 arg->result=raya_integrated_adapter_submit(arg->adapter,arg->pipe,
     RAYA_MANAGED_PIPE_READ,NULL,1,completion,NULL);
 return 0;
}
static int test_destroy_waits_for_concurrent_submit_entrants(void) {
 enum { THREADS=8 };
 raya_integrated_submit_adapter a; raya_dedicated_submitter s;
 raya_managed_worker w; api_state state; HANDLE pipe=NULL,threads[THREADS];
 adapter_race_arg args[THREADS]; DWORD i,wait_result;
 raya_adapter_destroy_result destroy_result;
 ZeroMemory(&state,sizeof(state)); state.fail_create=TRUE; state.block_create=TRUE;
 state.create_entered=CreateSemaphoreW(NULL,0,THREADS,NULL);
 state.create_release=CreateEventW(NULL,TRUE,FALSE,NULL);
 pipe=CreateEventW(NULL,TRUE,FALSE,NULL);
 if (!state.create_entered || !state.create_release || !pipe) return 1;
 CHECK(setup(&a,&s,&w,&state));
 for (i=0;i<THREADS;++i) {
  args[i].adapter=&a; args[i].pipe=pipe; args[i].result=RAYA_ADAPTER_QUEUED;
  threads[i]=CreateThread(NULL,0,adapter_race_submit_thread,&args[i],0,NULL);
  CHECK(threads[i]!=NULL);
 }
 for (i=0;i<THREADS;++i)
  CHECK(WaitForSingleObject(state.create_entered,5000)==WAIT_OBJECT_0);
 CHECK(state.create_calls==THREADS);
 CHECK(a.active_count==THREADS);
 /* All callers are inside the API after acquiring an entrant lease. */
 CHECK(raya_integrated_adapter_destroy_ex(&a,&destroy_result)==RAYA_ADAPTER_DESTROY_BUSY);
 CHECK(destroy_result.effect==RAYA_ADAPTER_DESTROY_EFFECT_ADMISSION_CLOSED);
 CHECK(destroy_result.retry==RAYA_ADAPTER_DESTROY_RETRY_RECONCILE);
 CHECK(destroy_result.step==1 && destroy_result.entrants==THREADS);
 CHECK(destroy_result.admission_closed==TRUE);
 CHECK(!raya_integrated_adapter_destroy(&a));
 CHECK(raya_integrated_adapter_submit(&a,pipe,RAYA_MANAGED_PIPE_READ,NULL,1,
       completion,NULL)==RAYA_ADAPTER_REJECTED);
 CHECK(SetEvent(state.create_release));
 wait_result=WaitForMultipleObjects(THREADS,threads,TRUE,5000);
 CHECK(wait_result==WAIT_OBJECT_0);
 for (i=0;i<THREADS;++i) {
  CHECK(args[i].result==RAYA_ADAPTER_REJECTED);
  CHECK(CloseHandle(threads[i]));
 }
 CHECK(a.active_count==0);
 CHECK((a.lifecycle_state & 0x3fffffff)==0);
 CHECK(raya_integrated_adapter_destroy(&a));
 CHECK(raya_submitter_shutdown(&s)); CHECK(raya_managed_worker_shutdown(&w));
 CHECK(raya_submitter_join(&s,5000)); CHECK(raya_managed_worker_join(&w,5000));
 CHECK(raya_submitter_destroy(&s)); CHECK(raya_managed_worker_destroy(&w));
 CHECK(CloseHandle(state.create_entered)); CHECK(CloseHandle(state.create_release));
 CHECK(CloseHandle(pipe));
 return 0;
}


static int test_submit_rejected_after_worker_degraded(void) {
 raya_integrated_submit_adapter a; raya_dedicated_submitter s;
 raya_managed_worker w; api_state state; volatile LONG callbacks=0;
 HANDLE pipe=NULL; DWORD wait_result;
 ZeroMemory(&state,sizeof(state));
 pipe=CreateEventW(NULL,TRUE,FALSE,NULL);
 CHECK(pipe!=NULL);
 CHECK(setup(&a,&s,&w,&state));
 EnterCriticalSection(&w.lock);
 CHECK(raya_reaper_mark_degraded_cause(&w.integrated.core,
       RAYA_RECOVERY_CAUSE_WAIT_FAILED));
 CHECK(SetEvent(w.wake_event));
 LeaveCriticalSection(&w.lock);
 CHECK(WaitForSingleObject(w.thread,5000)==WAIT_OBJECT_0);
 CHECK(raya_integrated_adapter_submit(&a,pipe,RAYA_MANAGED_PIPE_READ,NULL,1,
       completion,(void *)&callbacks)==RAYA_ADAPTER_REJECTED);
 CHECK(state.create_calls==1 && state.duplicate_calls==1);
 CHECK(state.read_calls==0);
 CHECK(state.close_calls==2 && callbacks==0);
 CHECK(a.active_count==0 && raya_integrated_adapter_retained_count(&a)==0);
 CHECK(raya_integrated_adapter_destroy(&a));
 CHECK(raya_submitter_shutdown(&s));
 CHECK(raya_submitter_join(&s,5000));
 CHECK(raya_submitter_destroy(&s));
 CHECK(!raya_managed_worker_shutdown(&w));
 wait_result=WaitForSingleObject(w.thread,0);
 CHECK(wait_result==WAIT_OBJECT_0);
 CHECK(!raya_managed_worker_join(&w,100));
 CHECK(w.joined==TRUE && w.thread==NULL);
 CHECK(w.thread_exit_code==ERROR_INVALID_STATE);
 CHECK(!raya_managed_worker_destroy(&w));
 CHECK(CloseHandle(pipe));
 return 0;
}

static int test_worker_retained_identity_publication_contract(void) {
 raya_managed_worker w;
 raya_managed_operation valid_op,wrong_epoch_op,partial_op;
 raya_managed_operation direct_registered,adapter_registered;
 HANDLE direct_pipe=NULL,direct_event=NULL,adapter_pipe=NULL,adapter_event=NULL;
 ZeroMemory(&valid_op,sizeof(valid_op));
 ZeroMemory(&wrong_epoch_op,sizeof(wrong_epoch_op));
 ZeroMemory(&partial_op,sizeof(partial_op));
 CHECK(raya_managed_worker_start(&w));
 direct_pipe=CreateEventW(NULL,TRUE,FALSE,NULL);
 direct_event=CreateEventW(NULL,TRUE,FALSE,NULL);
 adapter_pipe=CreateEventW(NULL,TRUE,FALSE,NULL);
 adapter_event=CreateEventW(NULL,TRUE,FALSE,NULL);
 CHECK(direct_pipe && direct_event && adapter_pipe && adapter_event);
 CHECK(raya_managed_operation_init(&direct_registered,direct_pipe,direct_event,NULL,0,test_noop_release,NULL));
 CHECK(raya_managed_operation_init(&adapter_registered,adapter_pipe,adapter_event,NULL,0,test_noop_release,NULL));
 CHECK(raya_managed_worker_register_operation(&w,&direct_registered,RAYA_WORKER_INVENTORY_DIRECT,0,0));
 CHECK(raya_managed_worker_register_operation(&w,&adapter_registered,RAYA_WORKER_INVENTORY_ADAPTER_ASSOCIATED,7,11));
 CHECK(direct_registered.identity_valid && adapter_registered.identity_valid);
 CHECK(direct_registered.worker_epoch==w.worker_epoch && adapter_registered.worker_epoch==w.worker_epoch);
 CHECK(direct_registered.worker_operation_id!=adapter_registered.worker_operation_id);
 CHECK(direct_registered.inventory_kind==RAYA_WORKER_INVENTORY_DIRECT && !direct_registered.adapter_identity_valid && direct_registered.adapter_instance_id==0 && direct_registered.adapter_operation_id==0);
 CHECK(adapter_registered.inventory_kind==RAYA_WORKER_INVENTORY_ADAPTER_ASSOCIATED && adapter_registered.adapter_identity_valid && adapter_registered.adapter_instance_id==7 && adapter_registered.adapter_operation_id==11);
 CHECK(!raya_managed_worker_register_operation(&w,&adapter_registered,RAYA_WORKER_INVENTORY_ADAPTER_ASSOCIATED,7,12));
 CHECK(!raya_managed_worker_register_operation(&w,&direct_registered,RAYA_WORKER_INVENTORY_DIRECT,7,11));
 valid_op.operation_initialized=TRUE;
 valid_op.identity_valid=TRUE;
 valid_op.worker_epoch=w.worker_epoch;
 valid_op.worker_operation_id=1;
 valid_op.inventory_kind=RAYA_WORKER_INVENTORY_ADAPTER_ASSOCIATED;
 valid_op.adapter_identity_valid=TRUE;
 valid_op.adapter_instance_id=7;
 valid_op.adapter_operation_id=11;
 EnterCriticalSection(&w.lock);
 CHECK(raya_managed_worker_publish_retained_locked(&w,&valid_op,
       RAYA_RECOVERY_CAUSE_HANDLE_CLOSE,
       RAYA_WORKER_RETAIN_STAGE_ADAPTER_ASSOCIATED));
 CHECK(w.retained_count==1 && w.retained_head==&valid_op);
 CHECK(valid_op.first_retain_cause==RAYA_RECOVERY_CAUSE_HANDLE_CLOSE);
 CHECK(valid_op.first_retain_stage==RAYA_WORKER_RETAIN_STAGE_ADAPTER_ASSOCIATED);
 CHECK(!raya_managed_worker_publish_retained_locked(&w,&valid_op,
       RAYA_RECOVERY_CAUSE_WAKE_SIGNAL,
       RAYA_WORKER_RETAIN_STAGE_POST_TERMINAL));
 CHECK(w.retained_count==1);
 CHECK(valid_op.first_retain_cause==RAYA_RECOVERY_CAUSE_HANDLE_CLOSE);
 CHECK(valid_op.first_retain_stage==RAYA_WORKER_RETAIN_STAGE_ADAPTER_ASSOCIATED);
 wrong_epoch_op=valid_op;
 wrong_epoch_op.retained_next=NULL;
 wrong_epoch_op.worker_operation_id=2;
 ++wrong_epoch_op.worker_epoch;
 CHECK(!raya_managed_worker_publish_retained_locked(&w,&wrong_epoch_op,
       RAYA_RECOVERY_CAUSE_HANDLE_CLOSE,
       RAYA_WORKER_RETAIN_STAGE_ADAPTER_ASSOCIATED));
 CHECK(w.retained_count==1);
 partial_op.operation_initialized=TRUE;
 CHECK(raya_managed_worker_publish_retained_locked(&w,&partial_op,
       RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,
       RAYA_WORKER_RETAIN_STAGE_PARTIAL_RECORD));
 CHECK(w.retained_count==2 && w.retained_head==&partial_op);
 CHECK(!partial_op.identity_valid && partial_op.worker_epoch==0 &&
       partial_op.worker_operation_id==0);
 CHECK(partial_op.first_retain_cause==RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
 w.retained_head=NULL;
 w.retained_count=0;
 LeaveCriticalSection(&w.lock);
 CHECK(raya_managed_worker_shutdown(&w));
 CHECK(raya_managed_worker_join(&w,5000));
 CHECK(raya_managed_worker_destroy(&w));
 CHECK(CloseHandle(direct_pipe)); CHECK(CloseHandle(direct_event));
 CHECK(CloseHandle(adapter_pipe)); CHECK(CloseHandle(adapter_event));
 return 0;
}
#ifdef RAYA_ADAPTER_TESTING
static int test_worker_wake_failure_transferred_untracked(void) {
 raya_integrated_submit_adapter a; raya_dedicated_submitter s; raya_managed_worker w;
 api_state state; volatile LONG callbacks=0;
 HANDLE server=INVALID_HANDLE_VALUE,client=INVALID_HANDLE_VALUE;
 raya_submitter_lifecycle submitter_lifecycle=RAYA_SUBMITTER_RUNNING;
 size_t queued=0,producers=0,active=0,leases=0;
 raya_managed_operation *queued_op;
 raya_adapter_test_retained_item adapter_items[1];
 raya_worker_test_retained_item worker_items[1];
 size_t adapter_count=0,worker_count=0;
 DWORD deadline;
 ZeroMemory(&state,sizeof(state)); state.fail_worker_wake=TRUE;
 server=CreateNamedPipeW(L"\\\\.\\pipe\\raya-adapter-worker-wake-fail",
     PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
     PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,4096,4096,0,NULL);
 CHECK(server!=INVALID_HANDLE_VALUE);
 client=CreateFileW(L"\\\\.\\pipe\\raya-adapter-worker-wake-fail",
     GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
 CHECK(client!=INVALID_HANDLE_VALUE);
 CHECK(setup(&a,&s,&w,&state));
 a.test_signal_worker_wake_on_quarantine=FALSE;
 state.worker_wake_event=w.wake_event;
 CHECK(raya_managed_worker_set_wake_callback(&w,test_worker_set_event,&state));
 Sleep(50);
 CHECK(WaitForSingleObject(w.thread,0)==WAIT_TIMEOUT);
 CHECK(raya_integrated_adapter_submit(&a,server,RAYA_MANAGED_PIPE_READ,NULL,1,
       completion,(void *)&callbacks)==RAYA_ADAPTER_QUEUED);
 deadline=GetTickCount()+5000;
 while (w.integrated.core.lifecycle!=RAYA_REAPER_DEGRADED &&
        (LONG)(deadline-GetTickCount())>0) Sleep(1);
 CHECK(w.integrated.core.lifecycle==RAYA_REAPER_DEGRADED);
 CHECK(state.worker_wake_calls==1);
 CHECK((w.integrated.core.degradation_causes & RAYA_RECOVERY_CAUSE_WAKE_SIGNAL)!=0);
 CHECK(WaitForSingleObject(w.thread,1000)==WAIT_OBJECT_0);
 CHECK(!raya_managed_worker_join(&w,5000));
 CHECK(w.joined==TRUE && w.thread==NULL && w.thread_exit_code==ERROR_INVALID_STATE);
 CHECK(w.integrated.core.queued==1 && w.integrated.core.outstanding==1);
 CHECK(w.integrated.core.caller_active==0 && w.integrated.reserved_slots==1);
 CHECK(w.integrated.core.head!=NULL && w.integrated.core.head->state==RAYA_REAPER_OP_QUEUED);
 CHECK(w.integrated.registry.count==0);


 CHECK(a.retained_count==1 && a.retained_head!=NULL);
 CHECK(w.retained_count==0 && w.retained_head==NULL);
 CHECK(a.active_count==1 && callbacks==0);
 queued_op=(raya_managed_operation *)w.integrated.core.head;
 CHECK(queued_op!=NULL && queued_op->integrated.base.state==RAYA_REAPER_OP_QUEUED);
 CHECK(queued_op->owner.state==RAYA_PIPE_OWNER_REAPER_QUEUED);
 CHECK(queued_op->integrated.collect_state==RAYA_INTEGRATED_COLLECT_PENDING);
 CHECK(queued_op!=NULL && queued_op->buffer!=NULL && queued_op->buffer_length==1);
 CHECK(queued_op->issuing_pipe!=NULL && queued_op->issuing_pipe!=INVALID_HANDLE_VALUE);
 { DWORD flags=0; CHECK(GetHandleInformation(queued_op->issuing_pipe,&flags));
   CHECK(GetHandleInformation(queued_op->integrated.completion_event,&flags)); }
 CHECK(queued_op->overlapped.hEvent==queued_op->integrated.completion_event);
 CHECK(raya_submitter_snapshot(&s,&submitter_lifecycle,&queued,&producers,&active,&leases));
 CHECK(submitter_lifecycle==RAYA_SUBMITTER_RUNNING);
 CHECK(queued==0 && producers==0 && active==0 && leases==1);
 CHECK(!raya_integrated_adapter_destroy(&a));
 CHECK((InterlockedCompareExchange(&a.lifecycle_state,0,0)&0x40000000)!=0);
 CHECK(raya_adapter_test_retained_snapshot(&a,adapter_items,1,&adapter_count,
       worker_items,1,&worker_count)==1);
 CHECK(adapter_count==1 && worker_count==0);
CHECK(adapter_items[0].operation_id==(uintptr_t)queued_op);
CHECK((adapter_items[0].flags&1u)!=0 && (adapter_items[0].flags&2u)!=0);
CHECK((adapter_items[0].flags&4u)==0 && (adapter_items[0].flags&32u)!=0);
CHECK(adapter_items[0].registry_matches==0);
 CHECK(a.active_count==1);
 CHECK(w.integrated.core.queued==1 && w.integrated.core.outstanding==1);
 CHECK(w.integrated.reserved_slots==1 && w.integrated.registry.count==0);
 CHECK(callbacks==0);
 /* Preserve in-flight operation, envelope, buffer and handles until process exit. */
 (void)client; (void)server;
 return 0;
}
#endif
static int test_destroy_ex_reports_effect_and_reconciliation(void) {
 raya_integrated_submit_adapter a; raya_dedicated_submitter s;
 raya_managed_worker w; api_state state; raya_adapter_destroy_result result;
 ZeroMemory(&state,sizeof(state));
 CHECK(setup(&a,&s,&w,&state));
 a.active_count=2;
 CHECK(raya_integrated_adapter_destroy_ex(&a,&result)==RAYA_ADAPTER_DESTROY_BUSY);
 CHECK(result.status==RAYA_ADAPTER_DESTROY_BUSY);
 CHECK(result.effect==RAYA_ADAPTER_DESTROY_EFFECT_ADMISSION_CLOSED);
 CHECK(result.retry==RAYA_ADAPTER_DESTROY_RETRY_RECONCILE);
 CHECK(result.step==2 && result.active==2 && result.entrants==0);
 CHECK(result.admission_closed==TRUE && result.lock_present==FALSE);
 CHECK((a.lifecycle_state & 0x40000000)!=0);
 a.active_count=0;
 a.retained_count=1;
 a.retained_head=(raya_adapter_envelope *)1;
 CHECK(raya_integrated_adapter_destroy_ex(&a,&result)==RAYA_ADAPTER_DESTROY_BUSY);
 CHECK(result.effect==RAYA_ADAPTER_DESTROY_EFFECT_ADMISSION_CLOSED);
 CHECK(result.retry==RAYA_ADAPTER_DESTROY_RETRY_RECONCILE);
 CHECK(result.step==2 && result.retained==1 && result.active==0);
 a.retained_count=0;
 a.retained_head=NULL;
 CHECK(raya_integrated_adapter_destroy_ex(&a,&result)==RAYA_ADAPTER_DESTROY_SUCCESS);
 CHECK(result.effect==RAYA_ADAPTER_DESTROY_EFFECT_DESTROYED);
 CHECK(result.status==RAYA_ADAPTER_DESTROY_SUCCESS && result.step==3);
 CHECK(result.admission_closed==TRUE && result.lock_present==FALSE);
 CHECK(raya_submitter_shutdown(&s)); CHECK(raya_managed_worker_shutdown(&w));
 CHECK(raya_submitter_join(&s,5000)); CHECK(raya_managed_worker_join(&w,5000));
 CHECK(raya_submitter_destroy(&s)); CHECK(raya_managed_worker_destroy(&w));
 return 0;
}

static int test_shared_worker_two_adapter_success_then_ack_quarantine(void) {
 raya_managed_worker w;
 raya_managed_operation *retained;
 DWORD retained_cause;
 int retained_stage;
 raya_integrated_submit_adapter a,b;
 raya_dedicated_submitter sa,sb;
 raya_integrated_adapter_api api_a,api_b;
 raya_submitter_win32_api submitter_api=make_submitter_api();
 api_state state_a,state_b;
#ifdef RAYA_ADAPTER_TESTING
 size_t adapter_count=0,worker_count=0;
 raya_adapter_test_retained_item adapter_items[2];
 raya_worker_test_retained_item worker_items[4];
#endif
 volatile LONG callbacks_a=0,callbacks_b=0;
 HANDLE server_a=INVALID_HANDLE_VALUE,client_a=INVALID_HANDLE_VALUE;
 HANDLE server_b=INVALID_HANDLE_VALUE,client_b=INVALID_HANDLE_VALUE;
 DWORD written=0,deadline;
 ZeroMemory(&w,sizeof(w)); ZeroMemory(&a,sizeof(a)); ZeroMemory(&b,sizeof(b));
 ZeroMemory(&sa,sizeof(sa)); ZeroMemory(&sb,sizeof(sb));
 ZeroMemory(&state_a,sizeof(state_a)); ZeroMemory(&state_b,sizeof(state_b));
 server_a=CreateNamedPipeW(L"\\\\.\\pipe\\raya-shared-worker-adapter-a",
     PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
     PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,4096,4096,0,NULL);
 CHECK(server_a!=INVALID_HANDLE_VALUE);
 client_a=CreateFileW(L"\\\\.\\pipe\\raya-shared-worker-adapter-a",
     GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
 CHECK(client_a!=INVALID_HANDLE_VALUE);
 server_b=CreateNamedPipeW(L"\\\\.\\pipe\\raya-shared-worker-adapter-b",
     PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
     PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,4096,4096,0,NULL);
 CHECK(server_b!=INVALID_HANDLE_VALUE);
 client_b=CreateFileW(L"\\\\.\\pipe\\raya-shared-worker-adapter-b",
     GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
 CHECK(client_b!=INVALID_HANDLE_VALUE);
 CHECK(raya_managed_worker_start(&w));
 CHECK(raya_managed_worker_set_close_callback(&w,test_shared_worker_close,NULL));
 state_a.submitter=&sa;
 state_b.submitter=&sb;
 state_b.arm_ack_on_worker_close=TRUE;
 CHECK(register_worker_close_state(&state_a));
 CHECK(register_worker_close_state(&state_b));
 CHECK(register_ack_state(&state_a));
 CHECK(register_ack_state(&state_b));
 api_a=make_api(&state_a); api_b=make_api(&state_b);
 CHECK(raya_integrated_adapter_start_with_apis(&a,&sa,&w,4,&api_a,&submitter_api));
 CHECK(raya_integrated_adapter_start_with_apis(&b,&sb,&w,4,&api_b,&submitter_api));
 CHECK(a.instance_id!=0 && b.instance_id!=0 && a.instance_id!=b.instance_id);
 CHECK(raya_integrated_adapter_submit(&a,server_a,RAYA_MANAGED_PIPE_READ,NULL,1,
       completion,(void *)&callbacks_a)==RAYA_ADAPTER_QUEUED);
 CHECK(WriteFile(client_a,"A",1,&written,NULL) && written==1);
 deadline=GetTickCount()+5000;
 while(InterlockedCompareExchange(&callbacks_a,0,0)==0 &&
       (LONG)(deadline-GetTickCount())>0) Sleep(1);
 CHECK(callbacks_a==1);
 CHECK(state_a.worker_close_calls==2 && !state_a.ack_failure_injected);
 CHECK(raya_integrated_adapter_retained_count(&a)==0 && a.active_count==0);
 CHECK(raya_integrated_adapter_submit(&b,server_b,RAYA_MANAGED_PIPE_READ,NULL,1,
       completion,(void *)&callbacks_b)==RAYA_ADAPTER_QUEUED);
 CHECK(WriteFile(client_b,"B",1,&written,NULL) && written==1);
 deadline=GetTickCount()+5000;
 while((!state_b.ack_failure_injected || raya_integrated_adapter_retained_count(&b)==0) &&
       (LONG)(deadline-GetTickCount())>0) Sleep(1);
 CHECK(state_b.worker_close_calls==2 && state_b.ack_failure_injected);
 CHECK(state_a.ack_failure_injected==FALSE);
 CHECK(callbacks_b==0);
 CHECK(raya_integrated_adapter_retained_count(&a)==0 && a.active_count==0);
 CHECK(raya_integrated_adapter_retained_count(&b)==1 && b.active_count==1);
 CHECK(w.retained_count==1 && w.retained_head!=NULL);
 CHECK(w.retained_head->identity_valid);
 CHECK(w.retained_head->inventory_kind==RAYA_WORKER_INVENTORY_ADAPTER_ASSOCIATED);
 CHECK(w.retained_head->adapter_identity_valid);
 CHECK(w.retained_head->adapter_instance_id==b.instance_id);
 CHECK(w.retained_head->adapter_operation_id==1);
 CHECK(w.retained_head->worker_epoch==w.worker_epoch);
 CHECK(w.retained_head->worker_operation_id!=0);
 CHECK(w.retained_head->first_retain_stage==RAYA_WORKER_RETAIN_STAGE_POST_TERMINAL);
 CHECK(w.retained_head->owner.state==RAYA_PIPE_OWNER_RELEASED);
 CHECK(w.retained_head->finalizer.state==RAYA_FINALIZER_RELEASED);
 CHECK(w.integrated.core.outstanding==0 && w.integrated.reserved_slots==0);
 retained=w.retained_head;
 retained_cause=retained->first_retain_cause;
 retained_stage=retained->first_retain_stage;
 EnterCriticalSection(&w.lock);
 CHECK(!raya_managed_worker_publish_retained_locked(&w,retained,
       RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,RAYA_WORKER_RETAIN_STAGE_POST_TERMINAL));
 CHECK(w.retained_count==1 && w.retained_head==retained);
 CHECK(retained->first_retain_cause==retained_cause);
 CHECK(retained->first_retain_stage==retained_stage);
 LeaveCriticalSection(&w.lock);
 CHECK(raya_integrated_adapter_destroy(&a));
 CHECK(!raya_integrated_adapter_destroy(&b));
 CHECK(raya_submitter_shutdown(&sa));
 CHECK(raya_submitter_join(&sa,5000));
 CHECK(raya_submitter_destroy(&sa));
 CHECK(!raya_submitter_shutdown(&sb));
 CHECK(WaitForSingleObject(sb.thread,5000)==WAIT_OBJECT_0);
 CHECK(!raya_submitter_join(&sb,100));
 CHECK(sb.joined==TRUE && sb.thread==NULL);
 CHECK(!raya_submitter_destroy(&sb));
 CHECK(!raya_managed_worker_shutdown(&w));
 CHECK(WaitForSingleObject(w.thread,5000)==WAIT_OBJECT_0);
 CHECK(!raya_managed_worker_join(&w,100));
 CHECK(w.joined==TRUE && w.thread==NULL);
#ifdef RAYA_ADAPTER_TESTING
 CHECK(raya_adapter_test_retained_snapshot(&b,adapter_items,2,&adapter_count,worker_items,4,&worker_count)==1);
 CHECK(adapter_count==1 && worker_count==1);
 CHECK(validate_retained_snapshot(adapter_items,adapter_count,worker_items,worker_count)==RAYA_RETAINED_VALID);
 CHECK(adapter_items[0].adapter_instance_id==b.instance_id);
 CHECK(adapter_items[0].stable_operation_id==1);
 CHECK((adapter_items[0].flags & 31u)==31u);
 CHECK(adapter_items[0].owner_state==RAYA_PIPE_OWNER_RELEASED);
 CHECK(adapter_items[0].finalizer_state==RAYA_FINALIZER_RELEASED);
 CHECK(adapter_items[0].issue_effect==RAYA_SUBMITTER_EFFECT_COMMITTED);
 CHECK(adapter_items[0].publish_effect==RAYA_SUBMITTER_EFFECT_COMMITTED);
 CHECK(worker_items[0].identity_valid);
 CHECK(worker_items[0].inventory_kind==RAYA_WORKER_INVENTORY_ADAPTER_ASSOCIATED);
 CHECK(worker_items[0].adapter_identity_valid);
 CHECK(worker_items[0].adapter_instance_id==b.instance_id);
 CHECK(worker_items[0].adapter_operation_id==adapter_items[0].stable_operation_id);
 CHECK(worker_items[0].first_retain_stage==RAYA_WORKER_RETAIN_STAGE_POST_TERMINAL);
#endif
 CHECK(!raya_managed_worker_destroy(&w));
 CHECK(state_a.worker_close_handles[0]==NULL && state_a.worker_close_handles[1]==NULL);
 CHECK(state_b.worker_close_handles[0]==NULL && state_b.worker_close_handles[1]==NULL);
 unregister_ack_state(&state_a); unregister_ack_state(&state_b);
 unregister_worker_close_state(&state_a); unregister_worker_close_state(&state_b);
 CHECK(CloseHandle(client_a)); CHECK(CloseHandle(server_a));
 CHECK(CloseHandle(client_b)); CHECK(CloseHandle(server_b));
 return 0;
}
int main(void) {
 if (test_worker_retained_identity_publication_contract()) return 1;
#ifdef RAYA_ADAPTER_TESTING
 if (test_retained_snapshot_validator()) return 1;
 if (test_worker_wake_failure_transferred_untracked()) return 1;
#endif

 int i;
 if (test_multi_submitter_ack_dispatcher()) return 1;
 if (test_shared_worker_close_dispatcher()) return 1;
 if (test_shared_worker_two_adapter_success_then_ack_quarantine()) return 1;
 if (test_destroy_ex_reports_effect_and_reconciliation()) return 1;
 if (test_destroy_waits_for_concurrent_submit_entrants()) return 1;
 if (test_submit_rejected_after_worker_degraded()) return 1;
 for (i=0;i<10;++i) if (test_create_failure()) return 1;
 for (i=0;i<10;++i) if (test_duplicate_failure()) return 1;
 for (i=0;i<10;++i) if (test_immediate_read_failure()) return 1;
 for (i=0;i<10;++i) if (test_immediate_write_failure()) return 1;
 for (i=0;i<10;++i) if (test_sync_read_success()) return 1;
 for (i=0;i<10;++i) if (test_sync_write_success()) return 1;
 if (test_handoff_failure_after_os_acceptance_retains()) return 1;
 if (test_ack_signal_failure_retains_after_resource_release(FALSE)) return 1;
 if (test_ack_signal_failure_retains_after_resource_release(TRUE)) return 1;
 /* Reserved for follow-up integration hardening; direct ACK wait-path matrix is active. */
 if (test_finalizer_close_failure_retains()) return 1;
#ifdef RAYA_ADAPTER_TESTING
 printf("adapter API seam: %ld assertions across 68 cases passed\n",checks);
#else
 printf("adapter API seam: %ld assertions across 66 cases passed\n",checks);
#endif
 return 0;
}


