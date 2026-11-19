#include "reaper-worker-v2.h"
#include <string.h>

static DWORD WINAPI raya_reaper_worker_main(LPVOID parameter) {
    raya_reaper_worker *w=(raya_reaper_worker *)parameter;
    for (;;) {
        raya_reaper_op *op=NULL;
        raya_reaper_take_result result=
            raya_reaper_sync_take(&w->sync,100,&op);
        if (result==RAYA_REAPER_TAKE_TIMEOUT) continue;
        if (result==RAYA_REAPER_TAKE_EXIT) {
            w->worker_exit_code=0;
            return 0;
        }
        if (result==RAYA_REAPER_TAKE_ERROR) {
            w->worker_exit_code=1;
            return 1;
        }
        if (result==RAYA_REAPER_TAKE_OPERATION) {
            int terminal=w->collect(op,w->context);
            if (!terminal) {
                EnterCriticalSection(&w->sync.lock);
                raya_reaper_mark_degraded_cause(
                    &w->sync.core,RAYA_RECOVERY_CAUSE_COMPLETION_UNCERTAIN);
                LeaveCriticalSection(&w->sync.lock);
                w->worker_exit_code=2;
                return 2;
            }
            if (!raya_reaper_sync_terminal(&w->sync,op)) {
                w->worker_exit_code=3;
                return 3;
            }
        }
    }
}
int raya_reaper_worker_start(raya_reaper_worker *w,
                             raya_reaper_collect_fn collect, void *context) {
    if (w==NULL || collect==NULL) return 0;
    memset(w,0,sizeof(*w));
    if (!raya_reaper_sync_init(&w->sync)) return 0;
    w->collect=collect;
    w->context=context;
    w->thread=CreateThread(NULL,0,raya_reaper_worker_main,w,0,NULL);
    if (w->thread==NULL) {
        EnterCriticalSection(&w->sync.lock);
        raya_reaper_mark_degraded_cause(&w->sync.core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
        LeaveCriticalSection(&w->sync.lock);
        return 0;
    }
    w->started=TRUE;
    return 1;
}
int raya_reaper_worker_shutdown(raya_reaper_worker *w, DWORD timeout_ms) {
    DWORD wr;
    if (w==NULL || !w->started || w->thread==NULL || w->joined) return 0;
    {
        int lifecycle;
        EnterCriticalSection(&w->sync.lock);
        lifecycle=(int)w->sync.core.lifecycle;
        LeaveCriticalSection(&w->sync.lock);
        if (lifecycle==RAYA_REAPER_ACCEPTING) {
            if (!raya_reaper_sync_shutdown(&w->sync)) return 0;
        } else if (lifecycle!=RAYA_REAPER_STOPPING) {
            return 0;
        }
    }
    wr=WaitForSingleObject(w->thread,timeout_ms);
    if (wr!=WAIT_OBJECT_0) return 0;
    if (w->worker_exit_code!=0) return 0;
    if (!raya_reaper_sync_mark_stopped(&w->sync)) return 0;
    w->joined=TRUE;
    return 1;
}
int raya_reaper_worker_destroy(raya_reaper_worker *w) {
    if (w==NULL || !w->started || !w->joined || w->thread==NULL) return 0;
    if (!raya_reaper_sync_destroy(&w->sync)) return 0;
    if (!CloseHandle(w->thread)) return 0;
    w->thread=NULL;
    w->started=FALSE;
    return 1;
}
int raya_reaper_worker_admit(raya_reaper_worker *w) {
    return w!=NULL && w->started && !w->joined ?
           raya_reaper_sync_admit(&w->sync) : 0;
}
int raya_reaper_worker_finish(raya_reaper_worker *w) {
    return w!=NULL && w->started && !w->joined ?
           raya_reaper_sync_finish(&w->sync) : 0;
}
raya_reaper_handoff_result raya_reaper_worker_handoff_ex(
    raya_reaper_worker *w, raya_reaper_op *op) {
    return w!=NULL && w->started && !w->joined ?
           raya_reaper_sync_handoff_ex(&w->sync,op) :
           RAYA_REAPER_HANDOFF_NOT_TRANSFERRED;
}
int raya_reaper_worker_handoff(raya_reaper_worker *w, raya_reaper_op *op) {
    return raya_reaper_worker_handoff_ex(w,op)!=
           RAYA_REAPER_HANDOFF_NOT_TRANSFERRED;
}
