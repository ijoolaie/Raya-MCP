#include "managed-pipe-io-v2.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
 raya_managed_pipe_completion_fn callback;
 void *context;
 BOOL read_operation;
} managed_pipe_callback_context;

static void managed_pipe_release(raya_managed_operation *op, void *context) {
 managed_pipe_callback_context *cb=(managed_pipe_callback_context *)context;
 const void *data=cb->read_operation ? op->buffer : NULL;
 cb->callback(op->completion_error,op->transferred,data,cb->context);
 free(op->buffer);
 free(cb);
 free(op);
}
static void retain_locked(raya_managed_worker *w,raya_managed_operation *op,
                          uint32_t cause,BOOL finish_lease) {
 raya_worker_retain_stage stage=(op->operation_initialized && op->identity_valid) ?
     RAYA_WORKER_RETAIN_STAGE_DIRECT : RAYA_WORKER_RETAIN_STAGE_PARTIAL_RECORD;
 if (!raya_managed_worker_publish_retained_locked(w,op,cause,stage))
  (void)raya_reaper_mark_degraded_cause(&w->integrated.core,
      RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
 (void)raya_reaper_mark_degraded_cause(&w->integrated.core,cause);
 if (finish_lease && !raya_reaper_caller_finish(&w->integrated.core))
  (void)raya_reaper_mark_degraded_cause(&w->integrated.core,
      RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
 if (!SetEvent(w->wake_event))
  (void)raya_reaper_mark_degraded_cause(&w->integrated.core,
      RAYA_RECOVERY_CAUSE_WAKE_SIGNAL);
}
static raya_managed_pipe_submit_result retain_operation(
 raya_managed_worker *w,raya_managed_operation *op,uint32_t cause,
 BOOL finish_lease) {
 EnterCriticalSection(&w->lock);
 retain_locked(w,op,cause,finish_lease);
 LeaveCriticalSection(&w->lock);
 return RAYA_MANAGED_PIPE_RETAINED;
}
static void discard_unissued(raya_managed_worker *w,
 raya_managed_operation *op,BOOL admitted) {
 raya_finalizer_result fr;
 raya_close_handle_fn close_fn;
 void *close_context;
 EnterCriticalSection(&w->lock);
 close_fn=w->close_handle;
 close_context=w->close_context;
 LeaveCriticalSection(&w->lock);
 fr=raya_pipe_finalizer_close_handles(&op->finalizer,close_fn,close_context);
 if (fr!=RAYA_FINALIZER_OK) {
  (void)retain_operation(w,op,
      fr==RAYA_FINALIZER_CLOSE_FAILED ? RAYA_RECOVERY_CAUSE_HANDLE_CLOSE :
      RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,admitted);
  return;
 }
 if (!raya_pipe_finalizer_commit_release(&op->finalizer)) {
  (void)retain_operation(w,op,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,admitted);
  return;
 }
 if (admitted) (void)raya_managed_worker_finish(w);
 free(op->buffer);
 free(op->release_context);
 free(op);
}
static raya_managed_pipe_submit_result finish_caller_terminal(
 raya_managed_worker *w,raya_managed_operation *op) {
 raya_finalizer_result fr;
 raya_close_handle_fn close_fn;
 void *close_context;
 if (!raya_pipe_owner_resources_may_be_freed(&op->owner))
  return retain_operation(w,op,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,TRUE);
 EnterCriticalSection(&w->lock);
 if (!raya_reaper_integrated_caller_terminal(&w->integrated,&op->integrated)) {
  (void)raya_reaper_mark_degraded_cause(&w->integrated.core,
      RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
  retain_locked(w,op,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,TRUE);
  LeaveCriticalSection(&w->lock);
  return RAYA_MANAGED_PIPE_RETAINED;
 }
 close_fn=w->close_handle;
 close_context=w->close_context;
 LeaveCriticalSection(&w->lock);
 fr=raya_pipe_finalizer_close_handles(&op->finalizer,close_fn,close_context);
 if (fr!=RAYA_FINALIZER_OK)
  return retain_operation(w,op,
      fr==RAYA_FINALIZER_CLOSE_FAILED ? RAYA_RECOVERY_CAUSE_HANDLE_CLOSE :
      RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,TRUE);
 if (!raya_pipe_finalizer_commit_release(&op->finalizer))
  return retain_operation(w,op,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,TRUE);
 if (!raya_pipe_owner_release(&op->owner))
  return retain_operation(w,op,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,TRUE);
 EnterCriticalSection(&w->lock);
 if (!raya_reaper_integrated_unreserve(&w->integrated,&op->integrated)) {
  (void)raya_reaper_mark_degraded_cause(&w->integrated.core,
      RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
  retain_locked(w,op,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,TRUE);
  LeaveCriticalSection(&w->lock);
  return RAYA_MANAGED_PIPE_RETAINED;
 }
 if (!raya_reaper_caller_finish(&w->integrated.core)) {
  (void)raya_reaper_mark_degraded_cause(&w->integrated.core,
      RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
  if (!raya_managed_worker_publish_retained_locked(w,op,
        RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,
        (op->operation_initialized && op->identity_valid) ?
         RAYA_WORKER_RETAIN_STAGE_DIRECT :
         RAYA_WORKER_RETAIN_STAGE_PARTIAL_RECORD))
   (void)raya_reaper_mark_degraded_cause(&w->integrated.core,
       RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
  if (!SetEvent(w->wake_event))
   (void)raya_reaper_mark_degraded_cause(&w->integrated.core,
       RAYA_RECOVERY_CAUSE_WAKE_SIGNAL);
  LeaveCriticalSection(&w->lock);
  return RAYA_MANAGED_PIPE_RETAINED;
 }
 if (w->integrated.core.lifecycle==RAYA_REAPER_STOPPING &&
     raya_reaper_integrated_can_exit(&w->integrated) &&
     !SetEvent(w->wake_event))
  (void)raya_reaper_mark_degraded_cause(&w->integrated.core,
      RAYA_RECOVERY_CAUSE_WAKE_SIGNAL);
 LeaveCriticalSection(&w->lock);
 {
  raya_managed_release_fn release=op->release;
  void *context=op->release_context;
  release(op,context);
 }
 return RAYA_MANAGED_PIPE_CALLBACK_INLINE;
}
static BOOL collect_caller(raya_managed_operation *op,BOOL signaled,
                           BOOL api_ok,DWORD error) {
 if (op->owner.state==RAYA_PIPE_OWNER_CALLER_UNCERTAIN &&
     !raya_pipe_owner_caller_retry(&op->owner)) return FALSE;
 return raya_pipe_owner_caller_collect(&op->owner,signaled,TRUE,api_ok,error);
}
static raya_managed_pipe_submit_result handoff_pending(
 raya_managed_worker *w,raya_managed_operation *op) {
 raya_managed_handoff_result hr=raya_managed_worker_handoff(w,op);
 if (hr==RAYA_MANAGED_HANDOFF_TRANSFERRED)
  return RAYA_MANAGED_PIPE_HANDED_OFF;
 if (hr==RAYA_MANAGED_HANDOFF_TRANSFERRED_DEGRADED)
  return RAYA_MANAGED_PIPE_RETAINED;
 return retain_operation(w,op,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,TRUE);
}
static BOOL cancel_default(HANDLE pipe,LPOVERLAPPED ov,void *context) {
 (void)context;
 return CancelIoEx(pipe,ov);
}
raya_managed_pipe_submit_result raya_managed_pipe_submit_with_cancel(
 raya_managed_worker *w,HANDLE pipe,raya_managed_pipe_direction direction,
 const void *write_data,DWORD length,DWORD timeout_ms,DWORD drain_ms,
 raya_managed_pipe_completion_fn callback,void *callback_context,
 raya_managed_cancel_fn cancel_fn,void *cancel_context) {
 raya_managed_operation *op;
 managed_pipe_callback_context *cb;
 HANDLE duplicate=INVALID_HANDLE_VALUE,event_handle=NULL;
 DWORD bytes=0,error=ERROR_SUCCESS;
 BOOL io_ok;
 DWORD wait_result;
 if (!w || !w->initialized || !pipe || pipe==INVALID_HANDLE_VALUE ||
     !callback || !cancel_fn || timeout_ms==INFINITE || drain_ms==INFINITE ||
     length==0 || length>RAYA_MANAGED_PIPE_MAX_IO ||
     (direction!=RAYA_MANAGED_PIPE_READ && direction!=RAYA_MANAGED_PIPE_WRITE) ||
     (direction==RAYA_MANAGED_PIPE_WRITE && !write_data))
  return RAYA_MANAGED_PIPE_REJECTED;
 op=(raya_managed_operation *)calloc(1,sizeof(*op));
 cb=(managed_pipe_callback_context *)calloc(1,sizeof(*cb));
 if (!op || !cb) { free(op);free(cb);return RAYA_MANAGED_PIPE_REJECTED; }
 op->buffer=malloc(length);
 if (!op->buffer) { free(cb);free(op);return RAYA_MANAGED_PIPE_REJECTED; }
 if (direction==RAYA_MANAGED_PIPE_WRITE) memcpy(op->buffer,write_data,length);
 cb->callback=callback;cb->context=callback_context;
 cb->read_operation=(direction==RAYA_MANAGED_PIPE_READ);
 if (!raya_managed_worker_admit(w)) {
  free(op->buffer);free(cb);free(op);
  return RAYA_MANAGED_PIPE_REJECTED;
 }
 event_handle=CreateEventW(NULL,TRUE,FALSE,NULL);
 if (!event_handle) {
  (void)raya_managed_worker_finish(w);
  free(op->buffer);free(cb);free(op);
  return RAYA_MANAGED_PIPE_REJECTED;
 }
 if (!DuplicateHandle(GetCurrentProcess(),pipe,GetCurrentProcess(),
       &duplicate,0,FALSE,DUPLICATE_SAME_ACCESS)) {
  raya_close_handle_fn close_fn;
  void *close_context;
  EnterCriticalSection(&w->lock);
  close_fn=w->close_handle;close_context=w->close_context;
  LeaveCriticalSection(&w->lock);
  if (!close_fn(event_handle,close_context)) {
   op->integrated.completion_event=event_handle;
   op->overlapped.hEvent=event_handle;
   op->issuing_pipe=INVALID_HANDLE_VALUE;
   op->release=managed_pipe_release;op->release_context=cb;
   return retain_operation(w,op,RAYA_RECOVERY_CAUSE_HANDLE_CLOSE,TRUE);
  }
  (void)raya_managed_worker_finish(w);
  free(op->buffer);free(cb);free(op);
  return RAYA_MANAGED_PIPE_REJECTED;
 }
 if (!raya_managed_operation_init(op,duplicate,event_handle,op->buffer,
       length,managed_pipe_release,cb)) {
  op->issuing_pipe=duplicate;
  op->integrated.completion_event=event_handle;
  op->overlapped.hEvent=event_handle;
  op->release=managed_pipe_release;op->release_context=cb;
  if (raya_pipe_finalizer_init(&op->finalizer,duplicate,event_handle))
   return retain_operation(w,op,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,TRUE);
  return retain_operation(w,op,RAYA_RECOVERY_CAUSE_HANDLE_CLOSE,TRUE);
 }
 if (!raya_managed_worker_register_operation(w,op,
       RAYA_WORKER_INVENTORY_DIRECT,0,0))
  return retain_operation(w,op,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,TRUE);
 op->read_operation=cb->read_operation;
 if (!raya_managed_worker_reserve(w,op)) {
  discard_unissued(w,op,TRUE);
  return RAYA_MANAGED_PIPE_REJECTED;
 }
 if (direction==RAYA_MANAGED_PIPE_READ)
  io_ok=ReadFile(op->issuing_pipe,op->buffer,length,&bytes,&op->overlapped);
 else
  io_ok=WriteFile(op->issuing_pipe,op->buffer,length,&bytes,&op->overlapped);
 if (io_ok) {
  op->transferred=bytes;op->completion_error=ERROR_SUCCESS;
  if (!raya_pipe_owner_caller_complete_immediate(&op->owner))
   return retain_operation(w,op,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,TRUE);
  return finish_caller_terminal(w,op);
 }
 error=GetLastError();
 if (error!=ERROR_IO_PENDING) {
  op->completion_error=error;
  if (!raya_pipe_owner_caller_submission_failed(&op->owner))
   return retain_operation(w,op,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,TRUE);
  return finish_caller_terminal(w,op);
 }
 wait_result=WaitForSingleObject(event_handle,timeout_ms);
 if (wait_result==WAIT_OBJECT_0) {
  io_ok=GetOverlappedResult(op->issuing_pipe,&op->overlapped,&bytes,FALSE);
  error=io_ok?ERROR_SUCCESS:GetLastError();
  {
   raya_pipe_completion_class cls=raya_classify_pipe_completion(
       TRUE,op->overlapped.hEvent==event_handle,io_ok,error);
   if (cls==RAYA_PIPE_COMPLETION_TERMINAL_SUCCESS ||
       cls==RAYA_PIPE_COMPLETION_TERMINAL_ABORTED) {
    op->transferred=bytes;op->completion_error=io_ok?ERROR_SUCCESS:error;
    if (!collect_caller(op,TRUE,io_ok,error))
     return retain_operation(w,op,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,TRUE);
    return finish_caller_terminal(w,op);
   }
   (void)collect_caller(op,TRUE,FALSE,error);
  }
 } else if (wait_result==WAIT_FAILED) {
  error=GetLastError();
  (void)collect_caller(op,FALSE,FALSE,error);
 }
 (void)cancel_fn(op->issuing_pipe,&op->overlapped,cancel_context);
 wait_result=WaitForSingleObject(event_handle,drain_ms);
 if (wait_result==WAIT_OBJECT_0) {
  io_ok=GetOverlappedResult(op->issuing_pipe,&op->overlapped,&bytes,FALSE);
  error=io_ok?ERROR_SUCCESS:GetLastError();
  {
   raya_pipe_completion_class cls=raya_classify_pipe_completion(
       TRUE,op->overlapped.hEvent==event_handle,io_ok,error);
   if (cls==RAYA_PIPE_COMPLETION_TERMINAL_SUCCESS ||
       cls==RAYA_PIPE_COMPLETION_TERMINAL_ABORTED) {
    op->transferred=bytes;op->completion_error=io_ok?ERROR_SUCCESS:error;
    if (!collect_caller(op,TRUE,io_ok,error))
     return retain_operation(w,op,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT,TRUE);
    return finish_caller_terminal(w,op);
   }
   (void)collect_caller(op,TRUE,FALSE,error);
  }
 } else if (wait_result==WAIT_FAILED) {
  error=GetLastError();
  (void)collect_caller(op,FALSE,FALSE,error);
 }
 return handoff_pending(w,op);
}
raya_managed_pipe_submit_result raya_managed_pipe_submit(
 raya_managed_worker *w,HANDLE pipe,raya_managed_pipe_direction direction,
 const void *write_data,DWORD length,DWORD timeout_ms,DWORD drain_ms,
 raya_managed_pipe_completion_fn callback,void *callback_context) {
 return raya_managed_pipe_submit_with_cancel(w,pipe,direction,write_data,
     length,timeout_ms,drain_ms,callback,callback_context,
     cancel_default,NULL);
}
