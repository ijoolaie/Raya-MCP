#include "reaper-sync-v2.h"
#include <string.h>

int raya_reaper_sync_init(raya_reaper_sync *s) {
    if (s == NULL) return 0;
    memset(s,0,sizeof(*s));
    if (!InitializeCriticalSectionEx(&s->lock,0,0)) return 0;
    s->wake_event=CreateEventW(NULL,TRUE,FALSE,NULL);
    if (s->wake_event==NULL) {
        DeleteCriticalSection(&s->lock);
        return 0;
    }
    if (!raya_reaper_core_start(&s->core)) {
        CloseHandle(s->wake_event);
        s->wake_event=NULL;
        DeleteCriticalSection(&s->lock);
        return 0;
    }
    s->initialized=TRUE;
    return 1;
}
int raya_reaper_sync_admit(raya_reaper_sync *s) {
    int ok;
    if (s==NULL || !s->initialized) return 0;
    EnterCriticalSection(&s->lock);
    ok=raya_reaper_admit(&s->core);
    LeaveCriticalSection(&s->lock);
    return ok;
}
int raya_reaper_sync_finish(raya_reaper_sync *s) {
    int ok, signal_ok=1;
    if (s==NULL || !s->initialized) return 0;
    EnterCriticalSection(&s->lock);
    ok=raya_reaper_caller_finish(&s->core);
    if (ok && s->core.lifecycle==RAYA_REAPER_STOPPING &&
        raya_reaper_can_exit(&s->core)) signal_ok=SetEvent(s->wake_event)!=0;
    if (!signal_ok) raya_reaper_mark_degraded_cause(&s->core,RAYA_RECOVERY_CAUSE_WAKE_SIGNAL);
    LeaveCriticalSection(&s->lock);
    return ok && signal_ok;
}
raya_reaper_handoff_result raya_reaper_sync_handoff_ex(
    raya_reaper_sync *s, raya_reaper_op *op) {
    raya_reaper_handoff_result result;
    int transferred, signal_ok;
    if (s==NULL || !s->initialized) return RAYA_REAPER_HANDOFF_NOT_TRANSFERRED;
    EnterCriticalSection(&s->lock);
    transferred=raya_reaper_handoff(&s->core,op);
    if (!transferred) {
        result=RAYA_REAPER_HANDOFF_NOT_TRANSFERRED;
    } else {
        signal_ok=SetEvent(s->wake_event)!=0;
        if (!signal_ok) {
            raya_reaper_mark_degraded_cause(&s->core,RAYA_RECOVERY_CAUSE_WAKE_SIGNAL);
            result=RAYA_REAPER_HANDOFF_TRANSFERRED_DEGRADED;
        } else {
            result=RAYA_REAPER_HANDOFF_TRANSFERRED;
        }
    }
    LeaveCriticalSection(&s->lock);
    return result;
}
int raya_reaper_sync_handoff(raya_reaper_sync *s, raya_reaper_op *op) {
    return raya_reaper_sync_handoff_ex(s,op)!=RAYA_REAPER_HANDOFF_NOT_TRANSFERRED;
}
raya_reaper_take_result raya_reaper_sync_take(raya_reaper_sync *s,
                                               DWORD wait_ms,
                                               raya_reaper_op **out_op) {
    DWORD wr;
    raya_reaper_op *op;
    int reset_ok, signal_ok=1, exit_now;
    if (out_op!=NULL) *out_op=NULL;
    if (s==NULL || !s->initialized || out_op==NULL)
        return RAYA_REAPER_TAKE_ERROR;
    wr=WaitForSingleObject(s->wake_event,wait_ms);
    if (wr==WAIT_TIMEOUT) return RAYA_REAPER_TAKE_TIMEOUT;
    if (wr!=WAIT_OBJECT_0) {
        EnterCriticalSection(&s->lock);
        raya_reaper_mark_degraded_cause(&s->core,RAYA_RECOVERY_CAUSE_WAIT_FAILED);
        LeaveCriticalSection(&s->lock);
        return RAYA_REAPER_TAKE_ERROR;
    }
    EnterCriticalSection(&s->lock);
    reset_ok=ResetEvent(s->wake_event)!=0;
    if (!reset_ok) {
        raya_reaper_mark_degraded_cause(&s->core,RAYA_RECOVERY_CAUSE_WAITSET_INVALID);
        LeaveCriticalSection(&s->lock);
        return RAYA_REAPER_TAKE_ERROR;
    }
    op=raya_reaper_dequeue(&s->core);
    if (op!=NULL && s->core.head!=NULL)
        signal_ok=SetEvent(s->wake_event)!=0;
    exit_now=raya_reaper_can_exit(&s->core);
    if (!signal_ok) raya_reaper_mark_degraded_cause(&s->core,RAYA_RECOVERY_CAUSE_WAKE_SIGNAL);
    LeaveCriticalSection(&s->lock);
    if (!signal_ok) return RAYA_REAPER_TAKE_ERROR;
    if (op!=NULL) { *out_op=op; return RAYA_REAPER_TAKE_OPERATION; }
    return exit_now ? RAYA_REAPER_TAKE_EXIT : RAYA_REAPER_TAKE_TIMEOUT;
}
int raya_reaper_sync_terminal(raya_reaper_sync *s, raya_reaper_op *op) {
    int ok, signal_ok=1;
    if (s==NULL || !s->initialized) return 0;
    EnterCriticalSection(&s->lock);
    ok=raya_reaper_worker_terminal(&s->core,op);
    if (!ok) raya_reaper_mark_degraded_cause(&s->core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
    if (ok && raya_reaper_can_exit(&s->core))
        signal_ok=SetEvent(s->wake_event)!=0;
    if (!signal_ok) raya_reaper_mark_degraded_cause(&s->core,RAYA_RECOVERY_CAUSE_WAKE_SIGNAL);
    LeaveCriticalSection(&s->lock);
    return ok && signal_ok;
}
int raya_reaper_sync_shutdown(raya_reaper_sync *s) {
    int ok, signal_ok;
    if (s==NULL || !s->initialized) return 0;
    EnterCriticalSection(&s->lock);
    ok=raya_reaper_begin_shutdown(&s->core);
    signal_ok=SetEvent(s->wake_event)!=0;
    if (!signal_ok) raya_reaper_mark_degraded_cause(&s->core,RAYA_RECOVERY_CAUSE_WAKE_SIGNAL);
    LeaveCriticalSection(&s->lock);
    return ok && signal_ok;
}
int raya_reaper_sync_mark_stopped(raya_reaper_sync *s) {
    int ok;
    if (s==NULL || !s->initialized) return 0;
    EnterCriticalSection(&s->lock);
    ok=raya_reaper_mark_stopped(&s->core);
    LeaveCriticalSection(&s->lock);
    return ok;
}
int raya_reaper_sync_destroy(raya_reaper_sync *s) {
    int safe;
    if (s==NULL || !s->initialized) return 0;
    EnterCriticalSection(&s->lock);
    safe=s->core.lifecycle==RAYA_REAPER_STOPPED &&
         s->core.caller_active==0 && s->core.outstanding==0 &&
         s->core.queued==0 && s->core.head==NULL && s->core.tail==NULL;
    LeaveCriticalSection(&s->lock);
    if (!safe) return 0;
    if (!CloseHandle(s->wake_event)) return 0;
    s->wake_event=NULL;
    DeleteCriticalSection(&s->lock);
    s->initialized=FALSE;
    return 1;
}
