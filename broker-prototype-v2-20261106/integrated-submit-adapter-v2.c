#include "integrated-submit-adapter-v2.h"
#include <stdlib.h>
#include <string.h>

#define RAYA_ADAPTER_CLOSING_BIT ((LONG)0x40000000)
#define RAYA_ADAPTER_ENTRY_MASK ((LONG)0x3fffffff)

static volatile LONG64 g_adapter_instance_sequence=0;

static int adapter_allocate_instance_id(ULONGLONG *id) {
 LONG64 value;
 if (!id) return 0;
 value=InterlockedIncrement64(&g_adapter_instance_sequence);
 if (value<=0) return 0; /* exhaustion/wrap permanently fails closed */
 *id=(ULONGLONG)value;
 return 1;
}

static int adapter_allocate_operation_id(raya_integrated_submit_adapter *a,
 ULONGLONG *id) {
 int ok=0;
 if (!a || !id) return 0;
 EnterCriticalSection(&a->lock);
 if (a->next_operation_id!=(ULONGLONG)~(ULONGLONG)0) {
  ++a->next_operation_id;
  if (a->next_operation_id!=0) { *id=a->next_operation_id; ok=1; }
 }
 LeaveCriticalSection(&a->lock);
 return ok;
}

static int adapter_enter(raya_integrated_submit_adapter *a) {
 LONG state;
 if (!a) return 0;
 for (;;) {
  state=InterlockedCompareExchange(&a->lifecycle_state,0,0);
  if ((state & RAYA_ADAPTER_CLOSING_BIT)!=0 ||
      (state & RAYA_ADAPTER_ENTRY_MASK)==RAYA_ADAPTER_ENTRY_MASK) return 0;
  if (InterlockedCompareExchange(&a->lifecycle_state,state+1,state)==state)
   return 1;
 }
}
static void adapter_leave(raya_integrated_submit_adapter *a) {
 (void)InterlockedDecrement(&a->lifecycle_state);
}

static HANDLE WINAPI adapter_default_create_event(LPSECURITY_ATTRIBUTES sa,
 BOOL manual,BOOL initial,LPCWSTR name,void *context) {
 (void)context; return CreateEventW(sa,manual,initial,name);
}
static BOOL WINAPI adapter_default_duplicate_handle(HANDLE source_process,
 HANDLE source,HANDLE target_process,LPHANDLE target,DWORD access,
 BOOL inherit,DWORD options,void *context) {
 (void)context; return DuplicateHandle(source_process,source,target_process,
     target,access,inherit,options);
}
static BOOL WINAPI adapter_default_read_file(HANDLE pipe,LPVOID buffer,
 DWORD length,LPDWORD transferred,LPOVERLAPPED overlapped,void *context) {
 (void)context; return ReadFile(pipe,buffer,length,transferred,overlapped);
}
static BOOL WINAPI adapter_default_write_file(HANDLE pipe,LPCVOID buffer,
 DWORD length,LPDWORD transferred,LPOVERLAPPED overlapped,void *context) {
 (void)context; return WriteFile(pipe,buffer,length,transferred,overlapped);
}
static DWORD WINAPI adapter_default_get_last_error(void *context) {
 (void)context; return GetLastError();
}
static BOOL WINAPI adapter_default_close_handle(HANDLE handle,void *context) {
 (void)context; return CloseHandle(handle);
}
static BOOL adapter_close_handle(HANDLE handle,void *context) {
 raya_integrated_submit_adapter *a=(raya_integrated_submit_adapter *)context;
 return a->api.close_handle(handle,a->api.context);
}
static int adapter_api_valid(const raya_integrated_adapter_api *api) {
 return api && api->create_event && api->duplicate_handle &&
     api->read_file && api->write_file && api->get_last_error &&
     api->close_handle;
}
static raya_integrated_adapter_api adapter_default_api(void) {
 raya_integrated_adapter_api api;
 api.create_event=adapter_default_create_event;
 api.duplicate_handle=adapter_default_duplicate_handle;
 api.read_file=adapter_default_read_file;
 api.write_file=adapter_default_write_file;
 api.get_last_error=adapter_default_get_last_error;
 api.close_handle=adapter_default_close_handle;
 api.context=NULL;
 return api;
}

struct raya_adapter_envelope {
 raya_submitter_request request;
 raya_managed_operation operation;
 raya_integrated_submit_adapter *adapter;
 ULONGLONG operation_id;
 DWORD first_retain_cause;
 raya_adapter_retain_stage first_retain_stage;
 raya_managed_pipe_completion_fn callback;
 void *callback_context;
 DWORD immediate_error;
 BOOL read_operation;
 BOOL retained;
 BOOL worker_admitted;
 BOOL worker_finished;
 BOOL worker_owns_operation;
 BOOL worker_terminal_released;
#ifdef RAYA_ADAPTER_TESTING
 BOOL transferred_degraded_quarantined;
#endif
 BOOL tracked;
 struct raya_adapter_envelope *retained_next;
};

static int worker_finish_once(raya_adapter_envelope *e) {
 raya_managed_worker *w=e->adapter->worker;
 int ok=1;
 EnterCriticalSection(&w->lock);
 if (e->worker_admitted && !e->worker_finished) {
  if (!raya_reaper_caller_finish(&w->integrated.core)) {
   (void)raya_reaper_mark_degraded_cause(&w->integrated.core,
       RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
   ok=0;
  } else {
   e->worker_finished=TRUE;
   if (w->integrated.core.lifecycle==RAYA_REAPER_STOPPING &&
       raya_reaper_integrated_can_exit(&w->integrated) &&
       !SetEvent(w->wake_event)) {
    (void)raya_reaper_mark_degraded_cause(&w->integrated.core,
        RAYA_RECOVERY_CAUSE_WAKE_SIGNAL);
    ok=0;
   }
  }
 }
 LeaveCriticalSection(&w->lock);
 return ok;
}

static void retain_envelope(raya_adapter_envelope *e, uint32_t cause,
 raya_adapter_retain_stage stage) {
 raya_integrated_submit_adapter *a=e->adapter;
 raya_managed_worker *w=a->worker;
 BOOL newly_retained=FALSE;
 if (e->worker_terminal_released &&
     cause==RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT)
  stage=RAYA_ADAPTER_RETAIN_STAGE_SUBMITTER_ACK;
 EnterCriticalSection(&a->lock);
 if (!e->retained) {
  e->first_retain_cause=cause;
  e->first_retain_stage=stage;
  e->retained=TRUE;
  e->retained_next=a->retained_head;
  a->retained_head=e;
  ++a->retained_count;
  newly_retained=TRUE;
 }
 LeaveCriticalSection(&a->lock);
 EnterCriticalSection(&w->lock);
 if (newly_retained &&
     (!e->worker_owns_operation || e->worker_terminal_released)) {
  {
   raya_worker_retain_stage worker_stage=e->worker_terminal_released ?
       RAYA_WORKER_RETAIN_STAGE_POST_TERMINAL :
       (e->operation.identity_valid ?
        RAYA_WORKER_RETAIN_STAGE_ADAPTER_ASSOCIATED :
        RAYA_WORKER_RETAIN_STAGE_PARTIAL_RECORD);
   if (!raya_managed_worker_publish_retained_locked(w,&e->operation,
         cause,worker_stage))
    (void)raya_reaper_mark_degraded_cause(&w->integrated.core,
        RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
  }
 }
 (void)raya_reaper_mark_degraded_cause(&w->integrated.core,cause);
 LeaveCriticalSection(&w->lock);
 (void)worker_finish_once(e);
 EnterCriticalSection(&w->lock);
 if (!SetEvent(w->wake_event))
  (void)raya_reaper_mark_degraded_cause(&w->integrated.core,
      RAYA_RECOVERY_CAUSE_WAKE_SIGNAL);
 LeaveCriticalSection(&w->lock);
}

static void retain_post_terminal_ack_notification(raya_adapter_envelope *e) {
 raya_integrated_submit_adapter *a=e->adapter;
 raya_managed_worker *w=a->worker;
 BOOL newly_retained=FALSE, worker_published=FALSE;
 /* Successful handoff consumed the caller lease; these flags must remain true. */
 if (!e->worker_terminal_released || !e->worker_owns_operation ||
      !e->worker_admitted || !e->worker_finished) {
  retain_envelope(e,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,
      RAYA_ADAPTER_RETAIN_STAGE_OWNERSHIP_INVARIANT);
  return;
 }
 EnterCriticalSection(&a->lock);
 if (!e->retained) {
  e->first_retain_cause=RAYA_RECOVERY_CAUSE_SUBMITTER_ACK_NOTIFICATION;
  e->first_retain_stage=RAYA_ADAPTER_RETAIN_STAGE_SUBMITTER_ACK;
  e->retained=TRUE;
  e->retained_next=a->retained_head;
  a->retained_head=e;
  ++a->retained_count;
  newly_retained=TRUE;
 }
 LeaveCriticalSection(&a->lock);
 EnterCriticalSection(&w->lock);
 if (newly_retained)
  worker_published=raya_managed_worker_publish_retained_locked(w,
      &e->operation,RAYA_RECOVERY_CAUSE_SUBMITTER_ACK_NOTIFICATION,
      RAYA_WORKER_RETAIN_STAGE_POST_TERMINAL);
 if (!worker_published)
  (void)raya_reaper_mark_degraded_cause(&w->integrated.core,
      RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
 LeaveCriticalSection(&w->lock);
 if (!worker_published) {
  return;
 }
 /* The callback runs on the Worker thread and returns directly to its loop.
    Do not signal the Worker or release retained storage. */
}
static void adapter_free(raya_adapter_envelope *e) {
 if (e->tracked) {
  EnterCriticalSection(&e->adapter->lock);
  if (e->adapter->active_count>0) --e->adapter->active_count;
  LeaveCriticalSection(&e->adapter->lock);
 }
 free(e->operation.buffer);
 free(e);
}

static void adapter_worker_release(raya_managed_operation *op, void *context) {
 raya_adapter_envelope *e=(raya_adapter_envelope *)context;
 raya_integrated_submit_adapter *a=e->adapter;
 e->worker_terminal_released=TRUE;
 {
  raya_submitter_ack_status ack=raya_submitter_acknowledge_ex(
      a->submitter,&e->request);
  if (ack==RAYA_SUBMITTER_ACK_COMMITTED_NOTIFICATION_FAILED) {
   retain_post_terminal_ack_notification(e);
   return;
  }
  if (ack!=RAYA_SUBMITTER_ACK_SUCCESS) {
   retain_envelope(e,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,
       RAYA_ADAPTER_RETAIN_STAGE_OWNERSHIP_INVARIANT);
   return;
  }
 }
 e->callback(op->completion_error,op->transferred,
     e->read_operation ? op->buffer : NULL,e->callback_context);
 adapter_free(e);
}

static raya_submitter_issue_result adapter_issue(
 raya_submitter_request *request, void *context) {
 raya_adapter_envelope *e=(raya_adapter_envelope *)request->item;
 BOOL ok;
 DWORD error;
 (void)context;
 if (e->read_operation)
  ok=e->adapter->api.read_file(e->operation.issuing_pipe,e->operation.buffer,
      e->operation.buffer_length,&e->operation.transferred,
      &e->operation.overlapped,e->adapter->api.context);
 else
  ok=e->adapter->api.write_file(e->operation.issuing_pipe,e->operation.buffer,
      e->operation.buffer_length,&e->operation.transferred,
      &e->operation.overlapped,e->adapter->api.context);
 if (ok) {
  e->operation.completion_error=ERROR_SUCCESS;
  return RAYA_SUBMITTER_ISSUE_ACCEPTED;
 }
 error=e->adapter->api.get_last_error(e->adapter->api.context);
 if (error==ERROR_IO_PENDING) return RAYA_SUBMITTER_ISSUE_ACCEPTED;
 e->immediate_error=error;
 return RAYA_SUBMITTER_ISSUE_NOT_ACCEPTED;
}

#ifdef RAYA_ADAPTER_TESTING
static int adapter_quarantine_transferred_degraded(
 raya_managed_operation *op, void *context) {
 raya_adapter_envelope *e=(raya_adapter_envelope *)context;
 raya_integrated_submit_adapter *a;
 if (!e || op!=&e->operation || !e->adapter) return 0;
 a=e->adapter;
 /* Called under Worker lock; sole nested lock order is Worker -> Adapter. */
 EnterCriticalSection(&a->lock);
 if (e->retained || e->transferred_degraded_quarantined) {
  LeaveCriticalSection(&a->lock);
  return 0;
 }
 e->retained=TRUE;
 e->transferred_degraded_quarantined=TRUE;
 e->retained_next=a->retained_head;
 a->retained_head=e;
 ++a->retained_count;
 LeaveCriticalSection(&a->lock);
 /* Test-only race: Worker thread is awakened but blocks on Worker lock until publication ends. */
 if (a->test_signal_worker_wake_on_quarantine &&
     !SetEvent(a->worker->wake_event)) return 0;
 return 1;
}
#endif
static int adapter_publish(raya_submitter_request *request, void *context) {
 raya_adapter_envelope *e=(raya_adapter_envelope *)request->item;
 raya_managed_worker *w=e->adapter->worker;
 raya_managed_handoff_result hr;
 (void)context;
#ifdef RAYA_ADAPTER_TESTING
 hr=raya_managed_worker_handoff_with_hook(w,&e->operation,
     adapter_quarantine_transferred_degraded,e);
#else
 hr=raya_managed_worker_handoff(w,&e->operation);
#endif
 if (hr==RAYA_MANAGED_HANDOFF_NOT_TRANSFERRED) {
  /* The OS already accepted this I/O: never route it through NOT_ISSUED cleanup. */
  (void)raya_pipe_owner_caller_collect(&e->operation.owner,
      FALSE, TRUE, FALSE, ERROR_INVALID_DATA);
  retain_envelope(e,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,
      RAYA_ADAPTER_RETAIN_STAGE_OWNERSHIP_INVARIANT);
  return 0;
 }
 e->worker_owns_operation=TRUE;
 /* Integrated handoff consumes the caller admission lease atomically. */
 e->worker_finished=TRUE;
 return 1;
}

static void adapter_not_issued(raya_submitter_request *request, void *context) {
 raya_adapter_envelope *e=(raya_adapter_envelope *)request->item;
 raya_managed_worker *w=e->adapter->worker;
 raya_finalizer_result fr;
 raya_close_handle_fn close_fn;
 void *close_context;
 (void)context;

 if (!raya_pipe_owner_caller_submission_failed(&e->operation.owner)) {
  retain_envelope(e,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,
      RAYA_ADAPTER_RETAIN_STAGE_OWNERSHIP_INVARIANT);
  return;
 }
 EnterCriticalSection(&w->lock);
 if (!raya_reaper_integrated_caller_terminal(&w->integrated,
       &e->operation.integrated)) {
  (void)raya_reaper_mark_degraded_cause(&w->integrated.core,
      RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
  LeaveCriticalSection(&w->lock);
  retain_envelope(e,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,
      RAYA_ADAPTER_RETAIN_STAGE_OWNERSHIP_INVARIANT);
  return;
 }
 close_fn=adapter_close_handle;
 close_context=e->adapter;
 LeaveCriticalSection(&w->lock);

 fr=raya_pipe_finalizer_close_handles(&e->operation.finalizer,
      close_fn,close_context);
 if (fr!=RAYA_FINALIZER_OK ||
     !raya_pipe_finalizer_commit_release(&e->operation.finalizer) ||
     !raya_pipe_owner_release(&e->operation.owner)) {
  retain_envelope(e,fr==RAYA_FINALIZER_CLOSE_FAILED ?
      RAYA_RECOVERY_CAUSE_HANDLE_CLOSE : RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,
        RAYA_ADAPTER_RETAIN_STAGE_HANDLE_CLOSE_FAILURE);
  return;
 }
 EnterCriticalSection(&w->lock);
 if (!raya_reaper_integrated_unreserve(&w->integrated,
       &e->operation.integrated)) {
  (void)raya_reaper_mark_degraded_cause(&w->integrated.core,
      RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
  LeaveCriticalSection(&w->lock);
  retain_envelope(e,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,
      RAYA_ADAPTER_RETAIN_STAGE_OWNERSHIP_INVARIANT);
  return;
 }
 LeaveCriticalSection(&w->lock);
 if (!worker_finish_once(e)) {
  retain_envelope(e,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,
      RAYA_ADAPTER_RETAIN_STAGE_OWNERSHIP_INVARIANT);
  return;
 }
 e->callback(e->immediate_error,0,NULL,e->callback_context);
 adapter_free(e);
}

int raya_integrated_adapter_start_with_apis(raya_integrated_submit_adapter *a,
 raya_dedicated_submitter *s, raya_managed_worker *w,size_t capacity,
 const raya_integrated_adapter_api *api,
 const raya_submitter_win32_api *submitter_api) {
 if (!a || !s || !w || !w->initialized || !w->started ||
     s->initialized || capacity==0 || capacity>4096 ||
     !adapter_api_valid(api)) return 0;
 memset(a,0,sizeof(*a));
 if (!InitializeCriticalSectionEx(&a->lock,0,0)) return 0;
 if (!adapter_allocate_instance_id(&a->instance_id)) {
  DeleteCriticalSection(&a->lock);
  memset(a,0,sizeof(*a));
  return 0;
 }
 a->api=*api;
 a->submitter=s; a->worker=w; a->initialized=TRUE;
 if (!raya_submitter_start_with_api(s,capacity,submitter_api,
       adapter_issue,adapter_publish,adapter_not_issued,a)) {
  a->initialized=FALSE;
  DeleteCriticalSection(&a->lock);
  memset(a,0,sizeof(*a));
  return 0;
 }
 return 1;
}
int raya_integrated_adapter_start_with_api(raya_integrated_submit_adapter *a,
 raya_dedicated_submitter *s, raya_managed_worker *w,size_t capacity,
 const raya_integrated_adapter_api *api) {
 return raya_integrated_adapter_start_with_apis(a,s,w,capacity,api,NULL);
}
int raya_integrated_adapter_start(raya_integrated_submit_adapter *a,
 raya_dedicated_submitter *s, raya_managed_worker *w,size_t capacity) {
 raya_integrated_adapter_api api=adapter_default_api();
 return raya_integrated_adapter_start_with_api(a,s,w,capacity,&api);
}

static raya_adapter_result adapter_submit_impl(
 raya_integrated_submit_adapter *a, HANDLE pipe,
 raya_managed_pipe_direction direction, const void *write_data, DWORD length,
 raya_managed_pipe_completion_fn callback, void *callback_context) {
 raya_adapter_envelope *e;
 HANDLE duplicate=NULL,event_handle=NULL;
 raya_adapter_result result=RAYA_ADAPTER_REJECTED;
 if (!a || !a->initialized || !pipe || pipe==INVALID_HANDLE_VALUE ||
     !callback || length==0 || length>RAYA_MANAGED_PIPE_MAX_IO ||
     (direction!=RAYA_MANAGED_PIPE_READ && direction!=RAYA_MANAGED_PIPE_WRITE) ||
     (direction==RAYA_MANAGED_PIPE_WRITE && !write_data)) return result;
 e=(raya_adapter_envelope *)calloc(1,sizeof(*e));
 if (!e) return result;
 e->operation.buffer=malloc(length);
 if (!e->operation.buffer) { free(e); return result; }
 if (direction==RAYA_MANAGED_PIPE_WRITE)
  memcpy(e->operation.buffer,write_data,length);
 e->adapter=a;
 if (!adapter_allocate_operation_id(a,&e->operation_id)) {
  free(e->operation.buffer); free(e); return result;
 }
 e->callback=callback; e->callback_context=callback_context;
 e->read_operation=(direction==RAYA_MANAGED_PIPE_READ);
 e->tracked=TRUE;
 EnterCriticalSection(&a->lock); ++a->active_count; LeaveCriticalSection(&a->lock);

 event_handle=a->api.create_event(NULL,TRUE,FALSE,NULL,a->api.context);
 if (!event_handle) { adapter_free(e); return result; }
 if (!a->api.duplicate_handle(GetCurrentProcess(),pipe,GetCurrentProcess(),
       &duplicate,0,FALSE,DUPLICATE_SAME_ACCESS,a->api.context)) {
  if (!a->api.close_handle(event_handle,a->api.context)) {
   e->operation.issuing_pipe=INVALID_HANDLE_VALUE;
   e->operation.integrated.completion_event=event_handle;
   e->operation.overlapped.hEvent=event_handle;
   e->operation.release=adapter_worker_release;
   e->operation.release_context=e;
   retain_envelope(e,RAYA_RECOVERY_CAUSE_HANDLE_CLOSE,
      RAYA_ADAPTER_RETAIN_STAGE_HANDLE_CLOSE_FAILURE);
   return RAYA_ADAPTER_RETAINED;
  }
  adapter_free(e); return result;
 }
 if (!raya_managed_operation_init(&e->operation,duplicate,event_handle,
       e->operation.buffer,length,adapter_worker_release,e)) {
  e->operation.issuing_pipe=duplicate;
  e->operation.integrated.completion_event=event_handle;
  e->operation.overlapped.hEvent=event_handle;
  if (raya_pipe_finalizer_init(&e->operation.finalizer,duplicate,event_handle))
   retain_envelope(e,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,
      RAYA_ADAPTER_RETAIN_STAGE_OWNERSHIP_INVARIANT);
  else
   retain_envelope(e,RAYA_RECOVERY_CAUSE_HANDLE_CLOSE,
      RAYA_ADAPTER_RETAIN_STAGE_HANDLE_CLOSE_FAILURE);
  return RAYA_ADAPTER_RETAINED;
 }
 if (!raya_managed_worker_register_operation(a->worker,&e->operation,
       RAYA_WORKER_INVENTORY_ADAPTER_ASSOCIATED,a->instance_id,e->operation_id)) {
  retain_envelope(e,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,
      RAYA_ADAPTER_RETAIN_STAGE_OWNERSHIP_INVARIANT);
  return RAYA_ADAPTER_RETAINED;
 }
 e->operation.read_operation=e->read_operation;

 if (!raya_managed_worker_admit(a->worker)) {
  raya_finalizer_result fr;
  if (!raya_pipe_owner_caller_submission_failed(&e->operation.owner)) {
   retain_envelope(e,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,
      RAYA_ADAPTER_RETAIN_STAGE_OWNERSHIP_INVARIANT);
   return RAYA_ADAPTER_RETAINED;
  }
  fr=raya_pipe_finalizer_close_handles(&e->operation.finalizer,
       adapter_close_handle,a);
  if (fr!=RAYA_FINALIZER_OK ||
      !raya_pipe_finalizer_commit_release(&e->operation.finalizer) ||
      !raya_pipe_owner_release(&e->operation.owner)) {
   retain_envelope(e,fr==RAYA_FINALIZER_CLOSE_FAILED ?
       RAYA_RECOVERY_CAUSE_HANDLE_CLOSE : RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,
        RAYA_ADAPTER_RETAIN_STAGE_HANDLE_CLOSE_FAILURE);
   return RAYA_ADAPTER_RETAINED;
  }
  adapter_free(e); return result;
 }
 e->worker_admitted=TRUE;
 if (!raya_submitter_admit(a->submitter,&e->request,e)) {
  if (!raya_pipe_owner_caller_submission_failed(&e->operation.owner)) {
   retain_envelope(e,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,
      RAYA_ADAPTER_RETAIN_STAGE_OWNERSHIP_INVARIANT);
   return RAYA_ADAPTER_RETAINED;
  }
  if (!raya_pipe_finalizer_close_handles(&e->operation.finalizer,
       adapter_close_handle,a) ||
      !raya_pipe_finalizer_commit_release(&e->operation.finalizer) ||
      !raya_pipe_owner_release(&e->operation.owner)) {
   retain_envelope(e,RAYA_RECOVERY_CAUSE_HANDLE_CLOSE,
      RAYA_ADAPTER_RETAIN_STAGE_HANDLE_CLOSE_FAILURE);
   return RAYA_ADAPTER_RETAINED;
  }
  (void)worker_finish_once(e);
  adapter_free(e); return result;
 }
 if (!raya_managed_worker_reserve(a->worker,&e->operation)) {
  if (!raya_submitter_abort_admission(a->submitter,&e->request)) {
   retain_envelope(e,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,
      RAYA_ADAPTER_RETAIN_STAGE_OWNERSHIP_INVARIANT);
   return RAYA_ADAPTER_RETAINED;
  }
  if (!raya_pipe_owner_caller_submission_failed(&e->operation.owner) ||
      !raya_pipe_finalizer_close_handles(&e->operation.finalizer,
       adapter_close_handle,a) ||
      !raya_pipe_finalizer_commit_release(&e->operation.finalizer) ||
      !raya_pipe_owner_release(&e->operation.owner)) {
   retain_envelope(e,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,
      RAYA_ADAPTER_RETAIN_STAGE_OWNERSHIP_INVARIANT);
   return RAYA_ADAPTER_RETAINED;
  }
  (void)worker_finish_once(e);
  adapter_free(e); return result;
 }
 {
  raya_submitter_result sr=raya_submitter_enqueue(a->submitter,&e->request);
  if (sr==RAYA_SUBMITTER_OK) return RAYA_ADAPTER_QUEUED;
  if (sr==RAYA_SUBMITTER_RETAINED) {
   retain_envelope(e,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,
      RAYA_ADAPTER_RETAIN_STAGE_OWNERSHIP_INVARIANT);
   return RAYA_ADAPTER_RETAINED;
  }
  if (!raya_submitter_abort_admission(a->submitter,&e->request)) {
   retain_envelope(e,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,
      RAYA_ADAPTER_RETAIN_STAGE_OWNERSHIP_INVARIANT);
   return RAYA_ADAPTER_RETAINED;
  }
 }
 /* Enqueue rejection/backpressure: caller still owns an unissued reservation. */
 if (!raya_pipe_owner_caller_submission_failed(&e->operation.owner)) {
  retain_envelope(e,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,
      RAYA_ADAPTER_RETAIN_STAGE_OWNERSHIP_INVARIANT);
  return RAYA_ADAPTER_RETAINED;
 }
 EnterCriticalSection(&a->worker->lock);
 if (!raya_reaper_integrated_caller_terminal(&a->worker->integrated,
       &e->operation.integrated)) {
  (void)raya_reaper_mark_degraded_cause(&a->worker->integrated.core,
      RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
  LeaveCriticalSection(&a->worker->lock);
  retain_envelope(e,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,
      RAYA_ADAPTER_RETAIN_STAGE_OWNERSHIP_INVARIANT);
  return RAYA_ADAPTER_RETAINED;
 }
 LeaveCriticalSection(&a->worker->lock);
 if (!raya_pipe_finalizer_close_handles(&e->operation.finalizer,
       adapter_close_handle,a) ||
     !raya_pipe_finalizer_commit_release(&e->operation.finalizer) ||
     !raya_pipe_owner_release(&e->operation.owner)) {
  retain_envelope(e,RAYA_RECOVERY_CAUSE_HANDLE_CLOSE,
      RAYA_ADAPTER_RETAIN_STAGE_HANDLE_CLOSE_FAILURE);
  return RAYA_ADAPTER_RETAINED;
 }
 EnterCriticalSection(&a->worker->lock);
 if (!raya_reaper_integrated_unreserve(&a->worker->integrated,
       &e->operation.integrated)) {
  (void)raya_reaper_mark_degraded_cause(&a->worker->integrated.core,
      RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
  LeaveCriticalSection(&a->worker->lock);
  retain_envelope(e,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,
      RAYA_ADAPTER_RETAIN_STAGE_OWNERSHIP_INVARIANT);
  return RAYA_ADAPTER_RETAINED;
 }
 LeaveCriticalSection(&a->worker->lock);
 if (!worker_finish_once(e)) {
  retain_envelope(e,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,
      RAYA_ADAPTER_RETAIN_STAGE_OWNERSHIP_INVARIANT);
  return RAYA_ADAPTER_RETAINED;
 }
 adapter_free(e);
 return result;
}

raya_adapter_result raya_integrated_adapter_submit(
 raya_integrated_submit_adapter *a, HANDLE pipe,
 raya_managed_pipe_direction direction, const void *write_data, DWORD length,
 raya_managed_pipe_completion_fn callback, void *callback_context) {
 raya_adapter_result result;
 if (!a || !adapter_enter(a)) return RAYA_ADAPTER_REJECTED;
 if (!a->initialized) { adapter_leave(a); return RAYA_ADAPTER_REJECTED; }
 result=adapter_submit_impl(a,pipe,direction,write_data,length,callback,callback_context);
 adapter_leave(a);
 return result;
}

size_t raya_integrated_adapter_retained_count(raya_integrated_submit_adapter *a) {
 size_t count;
 if (!a || !adapter_enter(a)) return 0;
 if (!a->initialized) { adapter_leave(a); return 0; }
 EnterCriticalSection(&a->lock); count=a->retained_count;
 LeaveCriticalSection(&a->lock);
 adapter_leave(a);
 return count;
}
void raya_adapter_destroy_result_init(raya_adapter_destroy_result *r) {
 if (!r) return;
 ZeroMemory(r,sizeof(*r));
 r->struct_size=(DWORD)sizeof(*r);
 r->status=RAYA_ADAPTER_DESTROY_INVALID;
 r->retry=RAYA_ADAPTER_DESTROY_RETRY_NONE;
}

raya_adapter_destroy_status raya_integrated_adapter_destroy_ex(
 raya_integrated_submit_adapter *a,raya_adapter_destroy_result *r) {
 LONG state,desired;
 size_t active,retained;
 if (r) raya_adapter_destroy_result_init(r);
 if (!a || !a->initialized || !r) return RAYA_ADAPTER_DESTROY_INVALID;
 for (;;) {
  state=InterlockedCompareExchange(&a->lifecycle_state,0,0);
  if ((state & RAYA_ADAPTER_CLOSING_BIT)!=0) break;
  desired=state | RAYA_ADAPTER_CLOSING_BIT;
  if (InterlockedCompareExchange(&a->lifecycle_state,desired,state)==state) {
   state=desired;
   break;
  }
 }
 r->admission_closed=TRUE;
 r->effect=RAYA_ADAPTER_DESTROY_EFFECT_ADMISSION_CLOSED;
 r->entrants=(size_t)(state & RAYA_ADAPTER_ENTRY_MASK);
 if (r->entrants!=0) {
  r->status=RAYA_ADAPTER_DESTROY_BUSY;
  r->retry=RAYA_ADAPTER_DESTROY_RETRY_RECONCILE;
  r->step=1;
  return r->status;
 }
 EnterCriticalSection(&a->lock);
 active=a->active_count;
 retained=a->retained_count;
 if ((retained==0)!=(a->retained_head==NULL)) {
  LeaveCriticalSection(&a->lock);
  r->status=RAYA_ADAPTER_DESTROY_INVALID;
  r->retry=RAYA_ADAPTER_DESTROY_RETRY_RECONCILE;
  r->step=2;
  return r->status;
 }
 LeaveCriticalSection(&a->lock);
 r->active=active;
 r->retained=retained;
 if (active!=0 || retained!=0) {
  r->status=RAYA_ADAPTER_DESTROY_BUSY;
  r->retry=RAYA_ADAPTER_DESTROY_RETRY_RECONCILE;
  r->step=2;
  return r->status;
 }
 /* Caller serializes destroy calls and keeps storage alive through this point. */
 DeleteCriticalSection(&a->lock);
 memset(a,0,sizeof(*a));
 r->status=RAYA_ADAPTER_DESTROY_SUCCESS;
 r->effect=RAYA_ADAPTER_DESTROY_EFFECT_DESTROYED;
 r->step=3;
 r->admission_closed=TRUE;
 r->lock_present=FALSE;
 return r->status;
}

int raya_integrated_adapter_destroy(raya_integrated_submit_adapter *a) {
 raya_adapter_destroy_result result;
 return raya_integrated_adapter_destroy_ex(a,&result)==RAYA_ADAPTER_DESTROY_SUCCESS;
}
#ifdef RAYA_ADAPTER_TESTING
#include <stdint.h>
#endif
raya_retained_inventory_status raya_integrated_adapter_copy_retained_inventory_v1(
 raya_integrated_submit_adapter *a, DWORD requested_version,
 raya_adapter_retained_record_v1 *records, size_t capacity,
 size_t *required_count) {
 raya_adapter_envelope *item,*slow,*fast;
 raya_managed_worker *w;
 raya_dedicated_submitter *s;
 LONG adapter_state,submitter_gate;
 size_t count=0,index=0;
 int submitter_quiescent;
 if (required_count==NULL) return RAYA_RETAINED_INVENTORY_INVALID_ARGUMENT;
 *required_count=0;
 if (a==NULL || (records==NULL && capacity!=0))
  return RAYA_RETAINED_INVENTORY_INVALID_ARGUMENT;
 if (requested_version!=RAYA_ADAPTER_RETAINED_RECORD_VERSION_1)
  return RAYA_RETAINED_INVENTORY_UNSUPPORTED_VERSION;
 if (!a->initialized || a->worker==NULL || a->submitter==NULL)
  return RAYA_RETAINED_INVENTORY_NOT_INITIALIZED;
 adapter_state=InterlockedCompareExchange(&a->lifecycle_state,0,0);
 if ((adapter_state & RAYA_ADAPTER_CLOSING_BIT)==0 ||
     (adapter_state & RAYA_ADAPTER_ENTRY_MASK)!=0)
  return RAYA_RETAINED_INVENTORY_NOT_QUIESCENT;
 w=a->worker;
 if (!w->initialized) return RAYA_RETAINED_INVENTORY_NOT_INITIALIZED;
 EnterCriticalSection(&w->lock);
 submitter_quiescent=w->joined && w->thread==NULL &&
     w->integrated.core.caller_active==0;
 LeaveCriticalSection(&w->lock);
 if (!submitter_quiescent) return RAYA_RETAINED_INVENTORY_NOT_QUIESCENT;
 s=a->submitter;
 if (!s->initialized) return RAYA_RETAINED_INVENTORY_NOT_INITIALIZED;
 submitter_gate=InterlockedCompareExchange(&s->api_gate,0,0);
 if (((ULONG)submitter_gate & 0x80000000u)==0 ||
     ((ULONG)submitter_gate & 0x7fffffffu)!=0)
  return RAYA_RETAINED_INVENTORY_NOT_QUIESCENT;
 EnterCriticalSection(&s->lock);
 submitter_quiescent=s->queue_count==0 &&
     s->active_producers==0 && s->active_issue==0;
 LeaveCriticalSection(&s->lock);
 if (!submitter_quiescent) return RAYA_RETAINED_INVENTORY_NOT_QUIESCENT;
 EnterCriticalSection(&a->lock);
 if (a->active_count!=a->retained_count) {
  LeaveCriticalSection(&a->lock);
  return RAYA_RETAINED_INVENTORY_NOT_QUIESCENT;
 }
 slow=a->retained_head;
 fast=a->retained_head;
 while (fast!=NULL && fast->retained_next!=NULL) {
  slow=slow->retained_next;
  fast=fast->retained_next->retained_next;
  if (slow==fast) {
   LeaveCriticalSection(&a->lock);
   return RAYA_RETAINED_INVENTORY_INCONSISTENT;
  }
 }
 for (item=a->retained_head;item!=NULL;item=item->retained_next) {
  if (count==(size_t)-1) {
   LeaveCriticalSection(&a->lock);
   return RAYA_RETAINED_INVENTORY_INCONSISTENT;
  }
  ++count;
 }
 if (count!=a->retained_count) {
  LeaveCriticalSection(&a->lock);
  return RAYA_RETAINED_INVENTORY_INCONSISTENT;
 }
 *required_count=count;
 if (capacity<count) {
  LeaveCriticalSection(&a->lock);
  return RAYA_RETAINED_INVENTORY_INSUFFICIENT_CAPACITY;
 }
 for (item=a->retained_head;item!=NULL;item=item->retained_next) {
  if (!item->retained || a->instance_id==0 || item->operation_id==0 ||
      item->first_retain_cause==0 ||
      item->first_retain_stage<RAYA_ADAPTER_RETAIN_STAGE_PRE_ISSUE_SETUP ||
      item->first_retain_stage>RAYA_ADAPTER_RETAIN_STAGE_OWNERSHIP_INVARIANT) {
   *required_count=0;
   LeaveCriticalSection(&a->lock);
   return RAYA_RETAINED_INVENTORY_INCONSISTENT;
  }
 }
 for (item=a->retained_head;item!=NULL;item=item->retained_next) {
  records[index].struct_size=(DWORD)sizeof(records[index]);
  records[index].version=RAYA_ADAPTER_RETAINED_RECORD_VERSION_1;
  records[index].adapter_instance_id=a->instance_id;
  records[index].adapter_operation_id=item->operation_id;
  records[index].first_retain_cause=item->first_retain_cause;
  records[index].first_retain_stage=item->first_retain_stage;
  ++index;
 }
 LeaveCriticalSection(&a->lock);
 return RAYA_RETAINED_INVENTORY_OK;
}

#ifdef RAYA_ADAPTER_TESTING
int raya_adapter_test_retained_snapshot(raya_integrated_submit_adapter *a,
 raya_adapter_test_retained_item *ai,size_t ac,size_t *an,
 raya_worker_test_retained_item *wi,size_t wc,size_t *wn) {
 raya_adapter_envelope *e,*eslow,*efast;
 raya_managed_operation *op,*oslow,*ofast;
 size_t na=0,nw=0,i=0,j;
 LONG state;
 int submitter_quiescent;
 if (an) *an=0;
 if (wn) *wn=0;
 if (!a || !a->initialized || !a->submitter || !a->worker || !an || !wn)
  return 0;
 state=InterlockedCompareExchange(&a->lifecycle_state,0,0);
 if ((state & RAYA_ADAPTER_CLOSING_BIT)==0 ||
     (state & RAYA_ADAPTER_ENTRY_MASK)!=0 ||
     !a->submitter->initialized || !a->worker->joined ||
     a->worker->thread!=NULL) return 0;
 EnterCriticalSection(&a->submitter->lock);
 submitter_quiescent=a->submitter->queue_count==0 &&
     a->submitter->active_producers==0 && a->submitter->active_issue==0;
 LeaveCriticalSection(&a->submitter->lock);
 if (!submitter_quiescent) return 0;
 EnterCriticalSection(&a->lock);
 eslow=a->retained_head; efast=a->retained_head;
 while (efast && efast->retained_next) {
  eslow=eslow->retained_next; efast=efast->retained_next->retained_next;
  if (eslow==efast) { LeaveCriticalSection(&a->lock); return -1; }
 }
 for (e=a->retained_head;e;e=e->retained_next) ++na;
 if (na!=a->retained_count) { LeaveCriticalSection(&a->lock); return -1; }
 *an=na;
 if (na>ac || (na && !ai)) { LeaveCriticalSection(&a->lock); return -1; }
 for (e=a->retained_head;e;e=e->retained_next) {
  ai[i].envelope_id=(uintptr_t)e;
  ai[i].adapter_instance_id=a->instance_id;
  ai[i].stable_operation_id=e->operation_id;
  ai[i].first_retain_cause=e->first_retain_cause;
  ai[i].first_retain_stage=e->first_retain_stage;
  ai[i].operation_id=(uintptr_t)&e->operation;
  ai[i].request_id=(uintptr_t)&e->request;
  ai[i].flags=(e->retained?1u:0u)|(e->worker_owns_operation?2u:0u)|
      (e->worker_terminal_released?4u:0u)|(e->worker_admitted?8u:0u)|
      (e->worker_finished?16u:0u)
#ifdef RAYA_ADAPTER_TESTING
      |(e->transferred_degraded_quarantined?32u:0u)
#endif
      ;
  ai[i].owner_state=(unsigned)e->operation.owner.state;
  ai[i].owner_collection_attempts=e->operation.owner.collection_attempts;
  ai[i].owner_terminal_collections=e->operation.owner.terminal_collections;
  ai[i].owner_release_commits=e->operation.owner.release_commits;
  ai[i].finalizer_state=(unsigned)e->operation.finalizer.state;
  ai[i].finalizer_close_attempts=e->operation.finalizer.close_attempts;
  ai[i].finalizer_successful_closes=e->operation.finalizer.successful_closes;
  ai[i].finalizer_release_commits=e->operation.finalizer.release_commits;
  ai[i].operation_state=(unsigned)e->operation.integrated.base.state;
  ai[i].collect_state=(unsigned)e->operation.integrated.collect_state;
  ai[i].reservation_state=(unsigned)e->operation.integrated.reservation_state;
  ai[i].terminal_collection_recorded=(e->operation.owner.terminal_collections==1u);
  ai[i].completion_error=e->operation.completion_error;
  ai[i].transferred_bytes=e->operation.transferred;
  ++i;
 }
 LeaveCriticalSection(&a->lock);
 EnterCriticalSection(&a->worker->lock);
 oslow=a->worker->retained_head; ofast=a->worker->retained_head;
 while (ofast && ofast->retained_next) {
  oslow=oslow->retained_next; ofast=ofast->retained_next->retained_next;
  if (oslow==ofast) { LeaveCriticalSection(&a->worker->lock); return -1; }
 }
 for (op=a->worker->retained_head;op;op=op->retained_next) ++nw;
 if (nw!=a->worker->retained_count) { LeaveCriticalSection(&a->worker->lock); return -1; }
 *wn=nw;
 if (nw>wc || (nw && !wi)) { LeaveCriticalSection(&a->worker->lock); return -1; }
 i=0;
  for (op=a->worker->retained_head;op;op=op->retained_next) {
   wi[i].operation_id=(uintptr_t)op;
   wi[i].operation_address=(uintptr_t)op;
   wi[i].identity_valid=op->identity_valid;
   wi[i].worker_epoch=op->worker_epoch;
   wi[i].worker_operation_id=op->worker_operation_id;
   wi[i].inventory_kind=op->inventory_kind;
   wi[i].adapter_identity_valid=op->adapter_identity_valid;
   wi[i].adapter_instance_id=op->adapter_instance_id;
   wi[i].adapter_operation_id=op->adapter_operation_id;
   wi[i].first_retain_cause=op->first_retain_cause;
   wi[i].first_retain_stage=op->first_retain_stage;
   ++i;
  }
 for (i=0;i<na;++i) {
  raya_managed_operation *item=(raya_managed_operation *)ai[i].operation_id;
  ai[i].registry_matches=0;
  for (j=0;j<a->worker->integrated.registry.count;++j)
   if (a->worker->integrated.registry.entries[j].op==&item->integrated.base)
    ++ai[i].registry_matches;
 }
 for (i=0;i<na;++i) {
  ai[i].worker_lifecycle=(unsigned)a->worker->integrated.core.lifecycle;
  ai[i].worker_reserved_slots=(unsigned)a->worker->integrated.reserved_slots;
  ai[i].worker_outstanding=(unsigned)a->worker->integrated.core.outstanding;
  ai[i].worker_queued=(unsigned)a->worker->integrated.core.queued;
  ai[i].worker_caller_active=(unsigned)a->worker->integrated.core.caller_active;
  ai[i].worker_registry_count=(unsigned)a->worker->integrated.registry.count;
  ai[i].worker_degradation_causes=(unsigned)a->worker->integrated.core.degradation_causes;
 }
 LeaveCriticalSection(&a->worker->lock);
 EnterCriticalSection(&a->submitter->lock);
 for (i=0;i<na;++i) {
  raya_submitter_request *request=(raya_submitter_request *)ai[i].request_id;
  ai[i].issue_effect=request->issue_effect;
  ai[i].publish_effect=request->publish_effect;
  ai[i].submitter_request_state=(unsigned)request->state;
  ai[i].submitter_lifecycle=(unsigned)a->submitter->lifecycle;
  ai[i].submitter_issuer_leases=(unsigned)a->submitter->issuer_leases;
  ai[i].submitter_active_issue=(unsigned)a->submitter->active_issue;
  ai[i].submitter_active_producers=(unsigned)a->submitter->active_producers;
  ai[i].submitter_queue_count=(unsigned)a->submitter->queue_count;
 }
 LeaveCriticalSection(&a->submitter->lock);
 return 1;
}
#endif
