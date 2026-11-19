#include "reaper-managed-worker-v2.h"
#include <stdio.h>
typedef struct {
    HANDLE thread_handle, wake_handle;
    LONG wait_timeout_once, wait_failed_once, exit_failure_once;
    LONG thread_close_failure_once, wake_close_failure_once;
    LONG wait_calls, exit_calls, close_calls;
} lifecycle_test_state;
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    (void)fprintf(stderr, "FAIL line %d: %s\\n", __LINE__, #x); return 1; \
} } while (0)
static DWORD WINAPI test_wait(HANDLE h, DWORD ms, void *ctx) {
    lifecycle_test_state *s=(lifecycle_test_state *)ctx;
    InterlockedIncrement(&s->wait_calls);
    if (InterlockedCompareExchange(&s->wait_timeout_once,0,1)==1) {
        SetLastError(ERROR_TIMEOUT); return WAIT_TIMEOUT;
    }
    if (InterlockedCompareExchange(&s->wait_failed_once,0,1)==1) {
        SetLastError(ERROR_INVALID_HANDLE); return WAIT_FAILED;
    }
    return WaitForSingleObject(h,ms);
}
static BOOL WINAPI test_exit(HANDLE h, LPDWORD code, void *ctx) {
    lifecycle_test_state *s=(lifecycle_test_state *)ctx;
    InterlockedIncrement(&s->exit_calls);
    if (InterlockedCompareExchange(&s->exit_failure_once,0,1)==1) {
        SetLastError(ERROR_ACCESS_DENIED); return FALSE;
    }
    if (!GetExitCodeThread(h,code)) return FALSE;
    *code=ERROR_ACCESS_DENIED; return TRUE;
}
static BOOL WINAPI test_close(HANDLE h, void *ctx) {
    lifecycle_test_state *s=(lifecycle_test_state *)ctx;
    InterlockedIncrement(&s->close_calls);
    if (h==s->thread_handle && InterlockedCompareExchange(&s->thread_close_failure_once,0,1)==1) {
        SetLastError(ERROR_INVALID_HANDLE); return FALSE;
    }
    if (h==s->wake_handle && InterlockedCompareExchange(&s->wake_close_failure_once,0,1)==1) {
        SetLastError(ERROR_INVALID_HANDLE); return FALSE;
    }
    return CloseHandle(h);
}
int main(void) {
    raya_managed_worker w; lifecycle_test_state s; raya_worker_op_result result;
    ZeroMemory(&w,sizeof(w)); ZeroMemory(&s,sizeof(s));
    CHECK(raya_managed_worker_start(&w));
    s.thread_handle=w.thread; s.wake_handle=w.wake_event;
    s.wait_timeout_once=1; s.wait_failed_once=1; s.exit_failure_once=1;
    s.thread_close_failure_once=1; s.wake_close_failure_once=1;
    CHECK(raya_managed_worker_set_lifecycle_api(&w,test_wait,test_exit,test_close,&s));
    CHECK(raya_managed_worker_shutdown_ex(&w,&result)==RAYA_WORKER_OP_SUCCESS);
    CHECK(result.effect==RAYA_WORKER_EFFECT_SHUTDOWN_SIGNALLED && result.retry==RAYA_WORKER_RETRY_NONE);
    CHECK(raya_managed_worker_join_ex(&w,5000,&result)==RAYA_WORKER_OP_RETRYABLE_FAILURE);
    CHECK(result.retry==RAYA_WORKER_RETRY_SAME_OPERATION && result.step==2 && result.win32_error==ERROR_TIMEOUT);
    CHECK(!raya_managed_worker_join(&w,5000)); /* WAIT_FAILED */
    CHECK(!w.joined && w.thread==s.thread_handle);
    CHECK(InterlockedCompareExchange(&s.exit_calls,0,0)==0);
    CHECK(raya_managed_worker_join_ex(&w,5000,&result)==RAYA_WORKER_OP_RETRYABLE_FAILURE); /* GetExitCodeThread fails */
    CHECK(result.step==3 && result.win32_error==ERROR_ACCESS_DENIED);
    CHECK(!w.joined && w.thread==s.thread_handle);
    CHECK(raya_managed_worker_join_ex(&w,5000,&result)==RAYA_WORKER_OP_RETRYABLE_FAILURE); /* thread HANDLE close fails */
    CHECK(result.step==4 && result.win32_error==ERROR_INVALID_HANDLE);
    CHECK(!w.joined && w.thread==s.thread_handle);
    CHECK(w.thread_exit_code==ERROR_ACCESS_DENIED);
    CHECK(raya_managed_worker_join_ex(&w,5000,&result)==RAYA_WORKER_OP_TERMINAL_FAILURE); /* abnormal exit, handle reaped */
    CHECK(result.effect==RAYA_WORKER_EFFECT_THREAD_REAPED && result.thread_exit_code==ERROR_ACCESS_DENIED && !result.thread_present);
    CHECK(w.joined && w.thread==NULL);
    CHECK(w.thread_exit_code==ERROR_ACCESS_DENIED);
    CHECK(InterlockedCompareExchange(&s.exit_calls,0,0)==3);
    CHECK(raya_managed_worker_destroy_ex(&w,&result)==RAYA_WORKER_OP_RETRYABLE_FAILURE);
    CHECK(result.retry==RAYA_WORKER_RETRY_SAME_OPERATION && result.step==3 && result.wake_event_present);
    CHECK(w.initialized && w.wake_event==s.wake_handle);
    CHECK(raya_managed_worker_destroy_ex(&w,&result)==RAYA_WORKER_OP_SUCCESS);
    CHECK(result.effect==RAYA_WORKER_EFFECT_DESTROYED && !result.wake_event_present);
    CHECK(!w.initialized);
    CHECK(InterlockedCompareExchange(&s.wait_calls,0,0)==5);
    CHECK(InterlockedCompareExchange(&s.close_calls,0,0)==4);
    (void)printf("PASS: %u lifecycle seam assertions\n",checks);
    return 0;
}
