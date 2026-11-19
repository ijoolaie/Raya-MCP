#include "dedicated-io-submitter-v2.h"
#include <string.h>

static HANDLE WINAPI default_create_event(LPSECURITY_ATTRIBUTES a, BOOL manual,
 BOOL initial, LPCWSTR name) { return CreateEventW(a,manual,initial,name); }
static HANDLE WINAPI default_create_thread(LPSECURITY_ATTRIBUTES a, SIZE_T stack,
 LPTHREAD_START_ROUTINE entry, LPVOID parameter, DWORD flags, LPDWORD id) {
 return CreateThread(a,stack,entry,parameter,flags,id);
}
static BOOL WINAPI default_set_event(HANDLE event_handle) {
 return SetEvent(event_handle);
}
static BOOL WINAPI default_reset_event(HANDLE event_handle) {
 return ResetEvent(event_handle);
}
static DWORD WINAPI default_wait(HANDLE handle, DWORD timeout) {
 return WaitForSingleObject(handle,timeout);
}
static BOOL WINAPI default_get_exit_code(HANDLE thread, LPDWORD code) {
 return GetExitCodeThread(thread,code);
}
static BOOL WINAPI default_close_handle(HANDLE handle) {
 return CloseHandle(handle);
}
static const raya_submitter_win32_api default_api = {
 default_create_event, default_create_thread, default_set_event,
 default_reset_event, default_wait, default_get_exit_code, default_close_handle
};

static int submitter_api_enter(raya_dedicated_submitter *s) {
 LONG observed, desired;
 if (s==NULL) return 0;
 for (;;) {
  observed=InterlockedCompareExchange(&s->api_gate,0,0);
  if (((ULONG)observed & 0x80000000u)!=0 ||
      ((ULONG)observed & 0x7fffffffu)==0x7fffffffu)
   return 0;
  desired=(LONG)((ULONG)observed+1u);
  if (InterlockedCompareExchange(&s->api_gate,desired,observed)==observed)
   break;
 }
 if (!s->initialized) {
  (void)InterlockedDecrement(&s->api_gate);
  return 0;
 }
 return 1;
}
static void submitter_api_leave(raya_dedicated_submitter *s) {
 (void)InterlockedDecrement(&s->api_gate);
}

static void capture_degraded_locked(raya_dedicated_submitter *s,
 raya_submitter_degraded_reason reason, DWORD error) {
 if (s->degraded_reason==RAYA_SUBMITTER_DEGRADED_REASON_NONE) {
  s->degraded_reason=reason;
  s->degraded_from_lifecycle=s->lifecycle;
  s->degraded_first_error=error;
  s->degraded_queue_count=s->queue_count;
  s->degraded_active_producers=s->active_producers;
  s->degraded_active_issue=s->active_issue;
  s->degraded_issuer_leases=s->issuer_leases;
 }
 s->lifecycle=RAYA_SUBMITTER_DEGRADED;
}
static void degrade_request_locked(raya_dedicated_submitter *s,
 raya_submitter_degraded_reason reason, DWORD error,
 const raya_submitter_request *request) {
 int first=(s->degraded_reason==RAYA_SUBMITTER_DEGRADED_REASON_NONE);
 capture_degraded_locked(s,reason,error);
 if (first && request!=NULL) {
  s->degraded_request_id=request->request_id;
  s->degraded_issue_effect=request->issue_effect;
  s->degraded_publish_effect=request->publish_effect;
 }
 (void)s->api.set_event(s->changed_event);
 (void)s->api.set_event(s->wake_event);
}
static void degrade_reason_locked(raya_dedicated_submitter *s,
 raya_submitter_degraded_reason reason, DWORD error) {
 degrade_request_locked(s,reason,error,NULL);
}
static int exit_ready_locked(const raya_dedicated_submitter *s) {
 return s->lifecycle==RAYA_SUBMITTER_STOPPING &&
  s->active_producers==0 && s->queue_count==0 &&
  s->active_issue==0 && s->issuer_leases==0;
}
static DWORD WINAPI submitter_main(void *parameter) {
 raya_dedicated_submitter *s=(raya_dedicated_submitter*)parameter;
 for (;;) {
  raya_submitter_request *r=NULL;
  raya_submitter_issue_result ir;
  int published;
  EnterCriticalSection(&s->lock);
  if (s->lifecycle==RAYA_SUBMITTER_DEGRADED) {
   LeaveCriticalSection(&s->lock); return ERROR_INVALID_STATE;
  }
  if (s->queue_count==0) {
   if (exit_ready_locked(s)) {
    s->lifecycle=RAYA_SUBMITTER_STOPPED;
    if (!s->api.set_event(s->changed_event)) {
     DWORD error=GetLastError();
     degrade_reason_locked(s,RAYA_SUBMITTER_DEGRADED_REASON_NOTIFICATION,error);
     LeaveCriticalSection(&s->lock); return ERROR_INVALID_HANDLE;
    }
    LeaveCriticalSection(&s->lock); return ERROR_SUCCESS;
   }
   if (!s->api.reset_event(s->wake_event)) {
    { DWORD e=GetLastError(); degrade_reason_locked(s,RAYA_SUBMITTER_DEGRADED_REASON_WAKE_RESET,e); } LeaveCriticalSection(&s->lock);
    return ERROR_INVALID_HANDLE;
   }
   if (s->queue_count!=0 || exit_ready_locked(s)) {
    (void)s->api.set_event(s->wake_event);
    LeaveCriticalSection(&s->lock); continue;
   }
   LeaveCriticalSection(&s->lock);
   if (s->api.wait_for_single_object(s->wake_event,INFINITE)!=WAIT_OBJECT_0) {
    { DWORD e=GetLastError();
     EnterCriticalSection(&s->lock);
     degrade_reason_locked(s,RAYA_SUBMITTER_DEGRADED_REASON_WAKE_WAIT,e);
    }
    LeaveCriticalSection(&s->lock); return ERROR_INVALID_HANDLE;
   }
   continue;
  }
  r=s->head;
  if (r==NULL || r->state!=RAYA_SUBMITTER_REQ_QUEUED ||
      s->active_issue!=0) {
   degrade_reason_locked(s,RAYA_SUBMITTER_DEGRADED_REASON_QUEUE_INVARIANT,ERROR_INVALID_DATA); LeaveCriticalSection(&s->lock);
   return ERROR_INVALID_DATA;
  }
  s->head=r->next;
  if (s->head==NULL) s->tail=NULL;
  r->next=NULL; --s->queue_count;
  ++s->active_issue; ++s->issuer_leases;
  r->state=RAYA_SUBMITTER_REQ_ISSUING;
  r->issue_effect=RAYA_SUBMITTER_EFFECT_IN_PROGRESS;
  r->publish_effect=RAYA_SUBMITTER_EFFECT_NOT_ATTEMPTED;
  LeaveCriticalSection(&s->lock);

  ir=s->issue(r,s->callback_context);
  if (ir==RAYA_SUBMITTER_ISSUE_NOT_ACCEPTED) {
   EnterCriticalSection(&s->lock);
   if (r->state!=RAYA_SUBMITTER_REQ_ISSUING ||
       s->active_issue!=1 || s->issuer_leases==0) {
    r->issue_effect=RAYA_SUBMITTER_EFFECT_UNKNOWN;
     r->state=RAYA_SUBMITTER_REQ_UNCERTAIN;
     degrade_request_locked(s,RAYA_SUBMITTER_DEGRADED_REASON_ISSUE_UNCERTAIN,ERROR_INVALID_DATA,r); LeaveCriticalSection(&s->lock);
    return ERROR_INVALID_DATA;
   }
   --s->active_issue; --s->issuer_leases;
   r->issue_effect=RAYA_SUBMITTER_EFFECT_NOT_ATTEMPTED;
   r->state=RAYA_SUBMITTER_REQ_NOT_ISSUED;
    if (!s->api.set_event(s->changed_event)) {
    { DWORD e=GetLastError(); degrade_request_locked(s,RAYA_SUBMITTER_DEGRADED_REASON_NOTIFICATION,e,r); }
    LeaveCriticalSection(&s->lock);
    s->not_issued(r,s->callback_context);
    return ERROR_INVALID_HANDLE;
   }
   LeaveCriticalSection(&s->lock);
   s->not_issued(r,s->callback_context);
   continue;
  }
  if (ir!=RAYA_SUBMITTER_ISSUE_ACCEPTED) {
   EnterCriticalSection(&s->lock);
   r->issue_effect=RAYA_SUBMITTER_EFFECT_UNKNOWN;
   r->state=RAYA_SUBMITTER_REQ_UNCERTAIN;
   if (s->active_issue==1) --s->active_issue;
   degrade_request_locked(s,RAYA_SUBMITTER_DEGRADED_REASON_ISSUE_UNCERTAIN,ERROR_INVALID_DATA,r);
   LeaveCriticalSection(&s->lock);
   return ERROR_INVALID_DATA;
  }

  EnterCriticalSection(&s->lock);
  r->issue_effect=RAYA_SUBMITTER_EFFECT_COMMITTED;
  r->publish_effect=RAYA_SUBMITTER_EFFECT_IN_PROGRESS;
  LeaveCriticalSection(&s->lock);
  published=s->publish(r,s->callback_context);
  EnterCriticalSection(&s->lock);
  if (s->active_issue!=1 || r->state!=RAYA_SUBMITTER_REQ_ISSUING) {
   degrade_request_locked(s,RAYA_SUBMITTER_DEGRADED_REASON_ISSUE_UNCERTAIN,ERROR_INVALID_DATA,r);
   LeaveCriticalSection(&s->lock);
   return ERROR_INVALID_DATA;
  }
  --s->active_issue;
  if (!published) {
   r->publish_effect=RAYA_SUBMITTER_EFFECT_UNKNOWN;
   r->state=RAYA_SUBMITTER_REQ_UNCERTAIN;
   degrade_request_locked(s,RAYA_SUBMITTER_DEGRADED_REASON_PUBLISH_UNCERTAIN,ERROR_INVALID_DATA,r);
   LeaveCriticalSection(&s->lock);
   return ERROR_INVALID_DATA;
  }
  r->publish_effect=RAYA_SUBMITTER_EFFECT_COMMITTED;
  r->state=RAYA_SUBMITTER_REQ_ISSUED;
  if (!s->api.set_event(s->changed_event)) {
   { DWORD e=GetLastError(); degrade_request_locked(s,RAYA_SUBMITTER_DEGRADED_REASON_NOTIFICATION,e,r); }
   LeaveCriticalSection(&s->lock);
   return ERROR_INVALID_HANDLE;
  }
  LeaveCriticalSection(&s->lock);
 }
}

int raya_submitter_start_with_api(raya_dedicated_submitter *s, size_t capacity,
 const raya_submitter_win32_api *api, raya_submitter_issue_fn issue,
 raya_submitter_publish_fn publish, raya_submitter_not_issued_fn not_issued,
 void *context) {
 int cleanup_ok=1;
 DWORD cleanup_error=ERROR_SUCCESS;
 const raya_submitter_win32_api *selected=api ? api : &default_api;
 if (s==NULL || capacity==0 || capacity>4096 || issue==NULL ||
     publish==NULL || not_issued==NULL ||
     selected->create_event==NULL || selected->create_thread==NULL ||
     selected->set_event==NULL || selected->reset_event==NULL ||
     selected->wait_for_single_object==NULL ||
     selected->get_exit_code_thread==NULL || selected->close_handle==NULL)
  return 0;
 memset(s,0,sizeof(*s));
 s->api=*selected;
 if (!InitializeCriticalSectionEx(&s->lock,0,0)) return 0;
 s->wake_event=s->api.create_event(NULL,TRUE,FALSE,NULL);
 if (s->wake_event==NULL) {
  DeleteCriticalSection(&s->lock); memset(s,0,sizeof(*s)); return 0;
 }
 s->changed_event=s->api.create_event(NULL,TRUE,FALSE,NULL);
 if (s->changed_event==NULL) {
  if (s->api.close_handle(s->wake_event)) s->wake_event=NULL;
  else { cleanup_ok=0; cleanup_error=GetLastError(); }
  if (cleanup_ok) {
   DeleteCriticalSection(&s->lock); memset(s,0,sizeof(*s));
  } else {
   capture_degraded_locked(s,RAYA_SUBMITTER_DEGRADED_REASON_STARTUP_ROLLBACK,
                           cleanup_error);
   s->initialized=TRUE;
  }
  return 0;
 }
 s->next_request_id=1;
 s->queue_capacity=capacity; s->issue=issue; s->publish=publish;
 s->not_issued=not_issued; s->callback_context=context;
 s->lifecycle=RAYA_SUBMITTER_RUNNING; s->initialized=TRUE;
 s->thread=s->api.create_thread(NULL,0,submitter_main,s,0,NULL);
 if (s->thread==NULL) {
  if (s->api.close_handle(s->wake_event)) s->wake_event=NULL;
  else { cleanup_ok=0; if (cleanup_error==ERROR_SUCCESS) cleanup_error=GetLastError(); }
  if (s->api.close_handle(s->changed_event)) s->changed_event=NULL;
  else { cleanup_ok=0; if (cleanup_error==ERROR_SUCCESS) cleanup_error=GetLastError(); }
  if (cleanup_ok) {
   DeleteCriticalSection(&s->lock); memset(s,0,sizeof(*s));
  } else {
   capture_degraded_locked(s,RAYA_SUBMITTER_DEGRADED_REASON_STARTUP_ROLLBACK,
                           cleanup_error);
  }
  return 0;
 }
 s->started=TRUE; return 1;
}
int raya_submitter_start(raya_dedicated_submitter *s, size_t capacity,
 raya_submitter_issue_fn issue, raya_submitter_publish_fn publish,
 raya_submitter_not_issued_fn not_issued, void *context) {
 return raya_submitter_start_with_api(s,capacity,NULL,issue,publish,
                                      not_issued,context);
}

static int submitter_admit_unchecked(raya_dedicated_submitter *s,
 raya_submitter_request *r, void *item) {
 int ok=0;
 if (s==NULL || r==NULL || !s->initialized) return 0;
 EnterCriticalSection(&s->lock);
 if (s->lifecycle==RAYA_SUBMITTER_RUNNING &&
     r->state==RAYA_SUBMITTER_REQ_BUILDING && r->next==NULL &&
     r->request_id==0 && s->next_request_id!=0) {
  r->request_id=s->next_request_id;
  if (s->next_request_id==~(ULONGLONG)0) s->next_request_id=0;
  else ++s->next_request_id;
  r->issue_effect=RAYA_SUBMITTER_EFFECT_NOT_ATTEMPTED;
  r->publish_effect=RAYA_SUBMITTER_EFFECT_NOT_ATTEMPTED;
  r->item=item; r->state=RAYA_SUBMITTER_REQ_ADMITTED;
  ++s->active_producers; ok=1;
 }
 LeaveCriticalSection(&s->lock); return ok;
}
static raya_submitter_result submitter_enqueue_unchecked(raya_dedicated_submitter *s,
 raya_submitter_request *r) {
 raya_submitter_result result=RAYA_SUBMITTER_REJECTED;
 if (s==NULL || r==NULL || !s->initialized) return result;
 EnterCriticalSection(&s->lock);
 if ((s->lifecycle==RAYA_SUBMITTER_RUNNING ||
      s->lifecycle==RAYA_SUBMITTER_STOPPING) &&
     r->state==RAYA_SUBMITTER_REQ_ADMITTED &&
     s->active_producers>0) {
  if (s->queue_count>=s->queue_capacity) result=RAYA_SUBMITTER_BACKPRESSURE;
  else {
   r->state=RAYA_SUBMITTER_REQ_QUEUED; r->next=NULL;
   if (s->tail) s->tail->next=r; else s->head=r;
   s->tail=r; ++s->queue_count; --s->active_producers;
   if (!s->api.set_event(s->wake_event)) {
    DWORD error=GetLastError();
    degrade_request_locked(s,RAYA_SUBMITTER_DEGRADED_REASON_NOTIFICATION,error,r);
    result=RAYA_SUBMITTER_RETAINED;
   } else if (!s->api.set_event(s->changed_event)) {
    DWORD error=GetLastError();
    degrade_request_locked(s,RAYA_SUBMITTER_DEGRADED_REASON_NOTIFICATION,error,r);
    result=RAYA_SUBMITTER_RETAINED;
   } else result=RAYA_SUBMITTER_OK;
  }
 }
 LeaveCriticalSection(&s->lock); return result;
}
static int submitter_abort_unchecked(raya_dedicated_submitter *s,
 raya_submitter_request *r) {
 int ok=0;
 if (s==NULL || r==NULL || !s->initialized) return 0;
 EnterCriticalSection(&s->lock);
 if (r->state==RAYA_SUBMITTER_REQ_ADMITTED && s->active_producers>0) {
  --s->active_producers; r->state=RAYA_SUBMITTER_REQ_NOT_ISSUED;
  if (!s->api.set_event(s->changed_event)) {
   DWORD error=GetLastError();
   degrade_request_locked(s,RAYA_SUBMITTER_DEGRADED_REASON_NOTIFICATION,error,r);
  } else if (!s->api.set_event(s->wake_event)) {
   DWORD error=GetLastError();
   degrade_request_locked(s,RAYA_SUBMITTER_DEGRADED_REASON_NOTIFICATION,error,r);
  } else ok=1;
 }
 LeaveCriticalSection(&s->lock); return ok;
}
static int submitter_acknowledge_unchecked(raya_dedicated_submitter *s,
 raya_submitter_request *r) {
 if (s==NULL || r==NULL || !s->initialized) return 0;
 for (;;) {
  EnterCriticalSection(&s->lock);
  if (r->state==RAYA_SUBMITTER_REQ_ISSUED) {
   if (s->issuer_leases==0) {
    degrade_request_locked(s,RAYA_SUBMITTER_DEGRADED_REASON_ACK_INVARIANT,ERROR_INVALID_DATA,r);
    LeaveCriticalSection(&s->lock); return 0;
   }
   --s->issuer_leases; r->state=RAYA_SUBMITTER_REQ_ACKED;
   if (!s->api.set_event(s->changed_event)) {
    DWORD error=GetLastError();
    degrade_request_locked(s,RAYA_SUBMITTER_DEGRADED_REASON_NOTIFICATION,error,r);
    LeaveCriticalSection(&s->lock); return 0;
   }
   if (!s->api.set_event(s->wake_event)) {
    DWORD error=GetLastError();
    degrade_request_locked(s,RAYA_SUBMITTER_DEGRADED_REASON_NOTIFICATION,error,r);
    LeaveCriticalSection(&s->lock); return 0;
   }
   LeaveCriticalSection(&s->lock); return 1;
  }
  if (r->state!=RAYA_SUBMITTER_REQ_ISSUING) {
   LeaveCriticalSection(&s->lock); return 0;
  }
  if (!s->api.reset_event(s->changed_event)) {
   DWORD error=GetLastError();
   degrade_request_locked(s,RAYA_SUBMITTER_DEGRADED_REASON_CHANGED_RESET,error,r);
   LeaveCriticalSection(&s->lock); return 0;
  }
  if (r->state!=RAYA_SUBMITTER_REQ_ISSUING) {
   LeaveCriticalSection(&s->lock); continue;
  }
  LeaveCriticalSection(&s->lock);
  if (s->api.wait_for_single_object(s->changed_event,INFINITE)!=WAIT_OBJECT_0) {
   DWORD error=GetLastError();
   EnterCriticalSection(&s->lock);
   degrade_request_locked(s,RAYA_SUBMITTER_DEGRADED_REASON_CHANGED_WAIT,error,r);
   LeaveCriticalSection(&s->lock); return 0;
  }
 }
}
void raya_submitter_result_init(raya_submitter_op_result *r) {
 if (r==NULL) return;
 ZeroMemory(r,sizeof(*r)); r->struct_size=(DWORD)sizeof(*r);
 r->status=RAYA_SUBMITTER_OP_INVALID_STATE;
}
static void fill_result(raya_dedicated_submitter *s, raya_submitter_op_result *r,
 raya_submitter_op_status status, raya_submitter_effect effect,
 raya_submitter_retry_class retry, DWORD step, DWORD error) {
 if (r==NULL) return;
 raya_submitter_result_init(r); r->status=status; r->effect=effect;
 r->retry=retry; r->step=step; r->win32_error=error;
 if (s!=NULL) {
  r->thread_exit_code=s->thread_exit_code;
  r->wake_event_present=s->wake_event!=NULL;
  r->changed_event_present=s->changed_event!=NULL;
  r->thread_present=s->thread!=NULL;
 }
}
raya_submitter_op_status raya_submitter_shutdown_ex(raya_dedicated_submitter *s,
 raya_submitter_op_result *r) {
 BOOL ok=FALSE, wake_signalled=FALSE; DWORD error=ERROR_SUCCESS;
 if (s==NULL || !s->initialized) {
  fill_result(s,r,RAYA_SUBMITTER_OP_INVALID_STATE,RAYA_SUBMITTER_EFFECT_NONE,
              RAYA_SUBMITTER_RETRY_NONE,1,ERROR_INVALID_STATE);
  return RAYA_SUBMITTER_OP_INVALID_STATE;
 }
 EnterCriticalSection(&s->lock);
 if (s->lifecycle==RAYA_SUBMITTER_STOPPED) {
  LeaveCriticalSection(&s->lock);
  fill_result(s,r,RAYA_SUBMITTER_OP_ALREADY_DONE,RAYA_SUBMITTER_EFFECT_SIGNALLED,
              RAYA_SUBMITTER_RETRY_NONE,0,ERROR_SUCCESS);
  return RAYA_SUBMITTER_OP_ALREADY_DONE;
 }
 if (s->lifecycle==RAYA_SUBMITTER_RUNNING) s->lifecycle=RAYA_SUBMITTER_STOPPING;
 if (s->lifecycle==RAYA_SUBMITTER_STOPPING) {
  if (!s->api.set_event(s->wake_event)) error=GetLastError();
  else {
   wake_signalled=TRUE;
   if (!s->api.set_event(s->changed_event)) error=GetLastError();
   else ok=TRUE;
  }
 }
 if (!ok && s->lifecycle!=RAYA_SUBMITTER_DEGRADED) degrade_reason_locked(s,RAYA_SUBMITTER_DEGRADED_REASON_NOTIFICATION,error ? error : ERROR_INVALID_STATE);
 LeaveCriticalSection(&s->lock);
 if (ok) {
  fill_result(s,r,RAYA_SUBMITTER_OP_SUCCESS,RAYA_SUBMITTER_EFFECT_SIGNALLED,
              RAYA_SUBMITTER_RETRY_NONE,0,ERROR_SUCCESS);
  return RAYA_SUBMITTER_OP_SUCCESS;
 }
 fill_result(s,r,RAYA_SUBMITTER_OP_TERMINAL_FAILURE,
             wake_signalled?RAYA_SUBMITTER_EFFECT_SIGNALLED:RAYA_SUBMITTER_EFFECT_NONE,
             RAYA_SUBMITTER_RETRY_RECONCILE,2,error ? error : ERROR_INVALID_STATE);
 return RAYA_SUBMITTER_OP_TERMINAL_FAILURE;
}
int raya_submitter_shutdown(raya_dedicated_submitter *s) {
 raya_submitter_op_result r;
 raya_submitter_op_status st=raya_submitter_shutdown_ex(s,&r);
 return st==RAYA_SUBMITTER_OP_SUCCESS || st==RAYA_SUBMITTER_OP_ALREADY_DONE;
}
raya_submitter_op_status raya_submitter_join_ex(raya_dedicated_submitter *s,
 DWORD timeout_ms, raya_submitter_op_result *r) {
 DWORD wr,code=0,error=ERROR_SUCCESS;
 if (s==NULL || !s->initialized || !s->started) {
  fill_result(s,r,RAYA_SUBMITTER_OP_INVALID_STATE,RAYA_SUBMITTER_EFFECT_NONE,
              RAYA_SUBMITTER_RETRY_NONE,1,ERROR_INVALID_STATE);
  return RAYA_SUBMITTER_OP_INVALID_STATE;
 }
 if (s->joined) {
  raya_submitter_op_status st=s->thread_exit_code==ERROR_SUCCESS ?
   RAYA_SUBMITTER_OP_ALREADY_DONE : RAYA_SUBMITTER_OP_TERMINAL_FAILURE;
  fill_result(s,r,st,RAYA_SUBMITTER_EFFECT_THREAD_JOINED,
   RAYA_SUBMITTER_RETRY_NONE,0,s->thread_exit_code==ERROR_SUCCESS?ERROR_SUCCESS:s->thread_exit_code);
  return st;
 }
 wr=s->api.wait_for_single_object(s->thread,timeout_ms);
 if (wr!=WAIT_OBJECT_0) {
  error=(wr==WAIT_TIMEOUT)?ERROR_TIMEOUT:GetLastError();
  fill_result(s,r,RAYA_SUBMITTER_OP_RETRYABLE_FAILURE,RAYA_SUBMITTER_EFFECT_NONE,
              RAYA_SUBMITTER_RETRY_SAME_OPERATION,2,error);
  return RAYA_SUBMITTER_OP_RETRYABLE_FAILURE;
 }
 if (!s->api.get_exit_code_thread(s->thread,&code)) {
  error=GetLastError();
  fill_result(s,r,RAYA_SUBMITTER_OP_RETRYABLE_FAILURE,RAYA_SUBMITTER_EFFECT_NONE,
              RAYA_SUBMITTER_RETRY_SAME_OPERATION,3,error);
  return RAYA_SUBMITTER_OP_RETRYABLE_FAILURE;
 }
 s->thread_exit_code=code;
 if (!s->api.close_handle(s->thread)) {
  error=GetLastError();
  fill_result(s,r,RAYA_SUBMITTER_OP_RETRYABLE_FAILURE,RAYA_SUBMITTER_EFFECT_NONE,
              RAYA_SUBMITTER_RETRY_SAME_OPERATION,4,error);
  return RAYA_SUBMITTER_OP_RETRYABLE_FAILURE;
 }
 s->thread=NULL; s->joined=TRUE;
 if (code==ERROR_SUCCESS) {
  fill_result(s,r,RAYA_SUBMITTER_OP_SUCCESS,RAYA_SUBMITTER_EFFECT_THREAD_JOINED,
              RAYA_SUBMITTER_RETRY_NONE,0,ERROR_SUCCESS);
  return RAYA_SUBMITTER_OP_SUCCESS;
 }
 fill_result(s,r,RAYA_SUBMITTER_OP_TERMINAL_FAILURE,RAYA_SUBMITTER_EFFECT_THREAD_JOINED,
             RAYA_SUBMITTER_RETRY_NONE,5,code);
 return RAYA_SUBMITTER_OP_TERMINAL_FAILURE;
}
int raya_submitter_join(raya_dedicated_submitter *s, DWORD timeout_ms) {
 raya_submitter_op_result r;
 raya_submitter_op_status st=raya_submitter_join_ex(s,timeout_ms,&r);
 return st==RAYA_SUBMITTER_OP_SUCCESS || st==RAYA_SUBMITTER_OP_ALREADY_DONE;
}
raya_submitter_op_status raya_submitter_destroy_ex(raya_dedicated_submitter *s,
 raya_submitter_op_result *r) {
 int safe;
 DWORD error=ERROR_SUCCESS;
 if (s==NULL || !s->initialized || !s->joined) {
  fill_result(s,r,RAYA_SUBMITTER_OP_INVALID_STATE,RAYA_SUBMITTER_EFFECT_NONE,
              RAYA_SUBMITTER_RETRY_NONE,1,ERROR_INVALID_STATE);
  return RAYA_SUBMITTER_OP_INVALID_STATE;
 }
 /* Caller serializes Destroy and quiesces every other Submitter API caller. */
 EnterCriticalSection(&s->lock);
 safe=s->head==NULL && s->tail==NULL && s->queue_count==0 &&
  s->active_producers==0 && s->active_issue==0 && s->issuer_leases==0 &&
  ((s->destroy_progress==0 && s->lifecycle==RAYA_SUBMITTER_STOPPED &&
    s->wake_event!=NULL && s->changed_event!=NULL) ||
   (s->destroy_progress==3 && s->lifecycle==RAYA_SUBMITTER_DEGRADED &&
    s->wake_event!=NULL && s->changed_event!=NULL) ||
   (s->destroy_progress==1 && s->lifecycle==RAYA_SUBMITTER_DEGRADED &&
    s->wake_event==NULL && s->changed_event!=NULL));
 LeaveCriticalSection(&s->lock);
 if (!safe) {
  fill_result(s,r,RAYA_SUBMITTER_OP_INVALID_STATE,RAYA_SUBMITTER_EFFECT_NONE,
              RAYA_SUBMITTER_RETRY_NONE,2,ERROR_INVALID_STATE);
  return RAYA_SUBMITTER_OP_INVALID_STATE;
 }
 if (s->destroy_progress==0 || s->destroy_progress==3) {
  if (!s->api.close_handle(s->wake_event)) {
   error=GetLastError(); s->destroy_progress=3;
   EnterCriticalSection(&s->lock);
   capture_degraded_locked(s,RAYA_SUBMITTER_DEGRADED_REASON_DESTROY_CLOSE,error);
   LeaveCriticalSection(&s->lock);
   fill_result(s,r,RAYA_SUBMITTER_OP_RETRYABLE_FAILURE,RAYA_SUBMITTER_EFFECT_NONE,
               RAYA_SUBMITTER_RETRY_SAME_OPERATION,3,error);
   return RAYA_SUBMITTER_OP_RETRYABLE_FAILURE;
  }
  s->wake_event=NULL; s->destroy_progress=1;
 }
 if (!s->api.close_handle(s->changed_event)) {
  error=GetLastError();
  EnterCriticalSection(&s->lock);
  capture_degraded_locked(s,RAYA_SUBMITTER_DEGRADED_REASON_DESTROY_CLOSE,error);
  LeaveCriticalSection(&s->lock);
  fill_result(s,r,RAYA_SUBMITTER_OP_RETRYABLE_FAILURE,RAYA_SUBMITTER_EFFECT_PARTIAL_DESTROY,
              RAYA_SUBMITTER_RETRY_SAME_OPERATION,4,error);
  return RAYA_SUBMITTER_OP_RETRYABLE_FAILURE;
 }
 s->changed_event=NULL; DeleteCriticalSection(&s->lock);
 s->initialized=FALSE; s->destroy_progress=2;
 fill_result(s,r,RAYA_SUBMITTER_OP_SUCCESS,RAYA_SUBMITTER_EFFECT_DESTROYED,
             RAYA_SUBMITTER_RETRY_NONE,0,ERROR_SUCCESS);
 return RAYA_SUBMITTER_OP_SUCCESS;
}
int raya_submitter_destroy(raya_dedicated_submitter *s) {
 raya_submitter_op_result r;
 return raya_submitter_destroy_ex(s,&r)==RAYA_SUBMITTER_OP_SUCCESS;
}
static int submitter_snapshot_unchecked(raya_dedicated_submitter *s,
 raya_submitter_lifecycle *life, size_t *queued, size_t *producers,
 size_t *active, size_t *leases) {
 if (s==NULL || !s->initialized || life==NULL || queued==NULL ||
     producers==NULL || active==NULL || leases==NULL) return 0;
 EnterCriticalSection(&s->lock); *life=s->lifecycle;
 *queued=s->queue_count; *producers=s->active_producers;
 *active=s->active_issue; *leases=s->issuer_leases;
 LeaveCriticalSection(&s->lock); return 1;
}

int raya_submitter_admission_close(raya_dedicated_submitter *s, LONG *active_count) {
 LONG observed, desired, count;
 if (s==NULL || !s->initialized || active_count==NULL) return 0;
 for (;;) {
  observed=InterlockedCompareExchange(&s->api_gate,0,0);
  desired=(LONG)((ULONG)observed | 0x80000000u);
  if (desired==observed ||
      InterlockedCompareExchange(&s->api_gate,desired,observed)==observed)
   break;
 }
 count=(LONG)((ULONG)InterlockedCompareExchange(&s->api_gate,0,0) & 0x7fffffffu);
 *active_count=count;
 return count==0;
}

int raya_submitter_admit(raya_dedicated_submitter *s,
 raya_submitter_request *r, void *item) {
 int result;
 if (!submitter_api_enter(s)) return 0;
 result=submitter_admit_unchecked(s,r,item);
 submitter_api_leave(s);
 return result;
}
raya_submitter_result raya_submitter_enqueue(raya_dedicated_submitter *s,
 raya_submitter_request *r) {
 raya_submitter_result result;
 if (!submitter_api_enter(s)) return RAYA_SUBMITTER_REJECTED;
 result=submitter_enqueue_unchecked(s,r);
 submitter_api_leave(s);
 return result;
}
int raya_submitter_abort_admission(raya_dedicated_submitter *s,
 raya_submitter_request *r) {
 int result;
 if (!submitter_api_enter(s)) return 0;
 result=submitter_abort_unchecked(s,r);
 submitter_api_leave(s);
 return result;
}
int raya_submitter_acknowledge(raya_dedicated_submitter *s,
 raya_submitter_request *r) {
 int result;
 if (!submitter_api_enter(s)) return 0;
 result=submitter_acknowledge_unchecked(s,r);
 submitter_api_leave(s);
 return result;
}
int raya_submitter_snapshot(raya_dedicated_submitter *s,
 raya_submitter_lifecycle *life, size_t *queued, size_t *producers,
 size_t *active, size_t *leases) {
 int result;
 if (!submitter_api_enter(s)) return 0;
 result=submitter_snapshot_unchecked(s,life,queued,producers,active,leases);
 submitter_api_leave(s);
 return result;
}
