#include "raya-orchestrator-v1.h"
#include <stdio.h>
#include <string.h>

static int failures;
static int passed;
#define CHECK(x,n) do { if (x) ++passed; else { ++failures; printf("FAIL: %s\n",n); } } while(0)

static void test_adapter_destroy_cursor_mapping(void) {
    raya_orch_registry registry;
    raya_orchestrator_context context;
    raya_integrated_submit_adapter adapter;
    raya_dedicated_submitter submitter;
    raya_managed_worker worker;
    raya_orch_handle handle=0;
    raya_orch_owner_token owner=0;
    raya_adapter_destroy_result result;
    raya_orch_cleanup_step cursor;
    memset(&adapter,0,sizeof(adapter));
    memset(&submitter,0,sizeof(submitter));
    memset(&worker,0,sizeof(worker));
    raya_orchestrator_context_init(&context);
    CHECK(raya_orch_registry_init(&registry),"Adapter cursor test Registry initializes");
    InitializeCriticalSection(&adapter.lock);
    adapter.initialized=TRUE;
    adapter.active_count=1;
    CHECK(raya_orchestrator_context_register(&registry,&context,&adapter,
        &submitter,&worker,RAYA_ORCH_WORKER_BORROWED,&handle)==RAYA_ORCH_OK,
        "Adapter cursor test Context registers");
    CHECK(raya_orch_begin_close(&registry,handle,&owner)==RAYA_ORCH_OK,
        "Adapter cursor test obtains exclusive cleanup owner");
    CHECK(raya_orch_wait_drained(&registry,handle,owner,0)==RAYA_ORCH_OK,
        "Adapter cursor test Registry leases are drained");
    CHECK(raya_orchestrator_destroy_adapter_owned(&registry,handle,owner,&result)==RAYA_ORCH_BUSY,
        "Adapter active work returns BUSY through owner facade");
    CHECK(result.effect==RAYA_ADAPTER_DESTROY_EFFECT_ADMISSION_CLOSED &&
        result.active==1 && result.admission_closed,
        "Adapter busy result preserves admission-close effect and active count");
    CHECK(raya_orch_cursor_read(&registry,handle,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_ADAPTER_DESTROY,&cursor)==RAYA_ORCH_OK &&
        cursor.effect==RAYA_ORCH_EFFECT_BLOCKED &&
        cursor.outcome==RAYA_ORCH_OUTCOME_UNCERTAIN && cursor.attempts==1,
        "Adapter busy result is recorded BLOCKED and UNCERTAIN");
    CHECK(raya_orchestrator_destroy_adapter_owned(&registry,handle,owner,&result)==RAYA_ORCH_BUSY &&
        result.status==RAYA_ADAPTER_DESTROY_BUSY,
        "Blocked Adapter step cannot be blindly retried");
    EnterCriticalSection(&adapter.lock);
    adapter.active_count=0;
    LeaveCriticalSection(&adapter.lock);
    CHECK(raya_orch_cursor_reconcile(&registry,handle,owner,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_ADAPTER_DESTROY,
        RAYA_ORCH_EFFECT_NOT_ATTEMPTED,RAYA_ORCH_OUTCOME_RETRYABLE_FAILURE,0,0)==RAYA_ORCH_OK,
        "Fresh zero-active evidence explicitly reconciles Adapter step for retry");
    CHECK(raya_orchestrator_destroy_adapter_owned(&registry,handle,owner,&result)==RAYA_ORCH_OK &&
        result.status==RAYA_ADAPTER_DESTROY_SUCCESS &&
        result.effect==RAYA_ADAPTER_DESTROY_EFFECT_DESTROYED,
        "Owner facade completes Adapter Destroy after reconciliation");
    CHECK(raya_orch_cursor_read(&registry,handle,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_ADAPTER_DESTROY,&cursor)==RAYA_ORCH_OK &&
        cursor.effect==RAYA_ORCH_EFFECT_COMMITTED &&
        cursor.outcome==RAYA_ORCH_OUTCOME_SUCCESS && cursor.attempts==2,
        "Adapter destruction is committed in cursor exactly once per attempt");
    CHECK(raya_orchestrator_destroy_adapter_owned(&registry,handle,owner,&result)==RAYA_ORCH_OK &&
        result.status==RAYA_ADAPTER_DESTROY_SUCCESS &&
        result.effect==RAYA_ADAPTER_DESTROY_EFFECT_DESTROYED,
        "Committed Adapter step reports terminal success idempotently without re-destroy");
    CHECK(raya_orchestrator_complete_close_owned(&registry,handle,owner)==RAYA_ORCH_BUSY,
        "Context close rejects incomplete required Submitter cleanup steps");
}

typedef struct { raya_dedicated_submitter *submitter; int result; } submitter_gate_thread_arg;
static raya_submitter_issue_result orchestrator_test_issue(
    raya_submitter_request *request, void *context) {
    (void)request; (void)context; return RAYA_SUBMITTER_ISSUE_NOT_ACCEPTED;
}
static int orchestrator_test_publish(raya_submitter_request *request, void *context) {
    (void)request; (void)context; return 1;
}
static void orchestrator_test_not_issued(raya_submitter_request *request, void *context) {
    (void)request; (void)context;
}
static DWORD WINAPI submitter_gate_snapshot_thread(void *parameter) {
    submitter_gate_thread_arg *arg=(submitter_gate_thread_arg *)parameter;
    raya_submitter_lifecycle life;
    size_t queued=0,producers=0,active=0,leases=0;
    arg->result=raya_submitter_snapshot(arg->submitter,&life,&queued,
        &producers,&active,&leases);
    return 0;
}
static void test_submitter_admission_cursor_integration(void) {
    raya_orch_registry registry;
    raya_orchestrator_context context;
    raya_integrated_submit_adapter adapter;
    raya_dedicated_submitter submitter;
    raya_managed_worker worker;
    raya_orch_handle handle=0;
    raya_orch_owner_token owner=0;
    raya_adapter_destroy_result adapter_result;
    raya_orch_cleanup_step cursor;
    submitter_gate_thread_arg arg;
    HANDLE thread=NULL;
    LONG active_count=-1;
    DWORD spin;
    memset(&adapter,0,sizeof(adapter));
    memset(&submitter,0,sizeof(submitter));
    memset(&worker,0,sizeof(worker));
    memset(&arg,0,sizeof(arg));
    raya_orchestrator_context_init(&context);
    CHECK(raya_orch_registry_init(&registry),"Submitter gate Registry initializes");
    InitializeCriticalSection(&adapter.lock);
    adapter.initialized=TRUE;
    CHECK(raya_submitter_start(&submitter,8,orchestrator_test_issue,
        orchestrator_test_publish,orchestrator_test_not_issued,NULL),
        "Submitter gate test starts real Submitter");
    CHECK(raya_orchestrator_context_register(&registry,&context,&adapter,
        &submitter,&worker,RAYA_ORCH_WORKER_BORROWED,&handle)==RAYA_ORCH_OK,
        "Submitter gate Context registers");
    CHECK(raya_orch_begin_close(&registry,handle,&owner)==RAYA_ORCH_OK &&
        raya_orch_wait_drained(&registry,handle,owner,0)==RAYA_ORCH_OK,
        "Submitter gate test obtains drained cleanup ownership");
    CHECK(raya_orchestrator_destroy_adapter_owned(&registry,handle,owner,
        &adapter_result)==RAYA_ORCH_OK &&
        adapter_result.status==RAYA_ADAPTER_DESTROY_SUCCESS,
        "Adapter Destroy commits before Submitter admission close");
    arg.submitter=&submitter;
    EnterCriticalSection(&submitter.lock);
    thread=CreateThread(NULL,0,submitter_gate_snapshot_thread,&arg,0,NULL);
    CHECK(thread!=NULL,"Submitter snapshot entrant thread starts");
    for (spin=0;spin<5000;++spin) {
        if (((ULONG)InterlockedCompareExchange(&submitter.api_gate,0,0)&
            0x7fffffffu)==1u) break;
        Sleep(1);
    }
    CHECK(spin<5000,"Snapshot caller is inside Submitter API gate");
    CHECK(raya_orchestrator_close_submitter_admission_owned(&registry,handle,
        owner,&active_count)==RAYA_ORCH_BUSY && active_count==1,
        "Orchestrator closes Submitter gate and reports one draining entrant");
    CHECK(raya_orch_cursor_read(&registry,handle,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_ADMISSION_CLOSE,&cursor)==RAYA_ORCH_OK &&
        cursor.effect==RAYA_ORCH_EFFECT_NOT_ATTEMPTED &&
        cursor.outcome==RAYA_ORCH_OUTCOME_RETRYABLE_FAILURE && cursor.attempts==1,
        "Submitter gate drain wait is recorded as retryable without reopening admission");
    LeaveCriticalSection(&submitter.lock);
    CHECK(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0,
        "Pre-close Submitter caller drains");
    CHECK(CloseHandle(thread),"Submitter entrant thread handle closes");
    CHECK(arg.result==1,"Pre-close Submitter snapshot completes");
    CHECK(raya_orchestrator_close_submitter_admission_owned(&registry,handle,
        owner,&active_count)==RAYA_ORCH_OK && active_count==0,
        "Orchestrator commits Submitter admission close after drain");
    CHECK(raya_orch_cursor_read(&registry,handle,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_ADMISSION_CLOSE,&cursor)==RAYA_ORCH_OK &&
        cursor.effect==RAYA_ORCH_EFFECT_COMMITTED &&
        cursor.outcome==RAYA_ORCH_OUTCOME_SUCCESS && cursor.attempts==2,
        "Submitter admission close is durably represented in cleanup cursor");
    CHECK(!raya_submitter_snapshot(&submitter,&submitter.lifecycle,
        &submitter.queue_count,&submitter.active_producers,
        &submitter.active_issue,&submitter.issuer_leases),
        "New Submitter snapshot calls are denied after gate closure");
    {
        raya_submitter_request request;
        memset(&request,0,sizeof(request));
        request.state=RAYA_SUBMITTER_REQ_BUILDING;
        CHECK(!raya_submitter_admit(&submitter,&request,NULL),
            "New Submitter admit calls are denied after gate closure");
        CHECK(raya_submitter_enqueue(&submitter,&request)==RAYA_SUBMITTER_REJECTED,
            "New Submitter enqueue calls are denied after gate closure");
        CHECK(!raya_submitter_abort_admission(&submitter,&request),
            "New Submitter abort calls are denied after gate closure");
        CHECK(!raya_submitter_acknowledge(&submitter,&request),
            "New Submitter acknowledge calls are denied after gate closure");
    }
    {
        raya_submitter_op_result lifecycle_result;
        CHECK(raya_orchestrator_submitter_shutdown_owned(&registry,handle,owner,
            &lifecycle_result)==RAYA_ORCH_OK &&
            lifecycle_result.status==RAYA_SUBMITTER_OP_SUCCESS &&
            lifecycle_result.effect==RAYA_SUBMITTER_EFFECT_SIGNALLED,
            "Owner facade executes Submitter Shutdown and preserves structured effect");
        CHECK(raya_orchestrator_submitter_join_owned(&registry,handle,owner,5000,
            &lifecycle_result)==RAYA_ORCH_OK &&
            lifecycle_result.status==RAYA_SUBMITTER_OP_SUCCESS &&
            lifecycle_result.effect==RAYA_SUBMITTER_EFFECT_THREAD_JOINED,
            "Owner facade executes Submitter Join and preserves thread-joined effect");
        CHECK(raya_orchestrator_submitter_destroy_owned(&registry,handle,owner,
            &lifecycle_result)==RAYA_ORCH_OK &&
            lifecycle_result.status==RAYA_SUBMITTER_OP_SUCCESS &&
            lifecycle_result.effect==RAYA_SUBMITTER_EFFECT_DESTROYED,
            "Owner facade executes Submitter Destroy and preserves destroyed effect");
        CHECK(raya_orchestrator_submitter_destroy_owned(&registry,handle,owner,
            &lifecycle_result)==RAYA_ORCH_OK &&
            lifecycle_result.status==RAYA_SUBMITTER_OP_ALREADY_DONE,
            "Committed Submitter Destroy reports idempotent success without re-execution");
    }
    CHECK(raya_orch_cursor_read(&registry,handle,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_SHUTDOWN,&cursor)==RAYA_ORCH_OK &&
        cursor.effect==RAYA_ORCH_EFFECT_COMMITTED && cursor.outcome==RAYA_ORCH_OUTCOME_SUCCESS,
        "Submitter Shutdown cursor commits independently");
    CHECK(raya_orch_cursor_read(&registry,handle,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_JOIN,&cursor)==RAYA_ORCH_OK &&
        cursor.effect==RAYA_ORCH_EFFECT_COMMITTED && cursor.outcome==RAYA_ORCH_OUTCOME_SUCCESS,
        "Submitter Join cursor commits independently");
    CHECK(raya_orch_cursor_read(&registry,handle,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_DESTROY,&cursor)==RAYA_ORCH_OK &&
        cursor.effect==RAYA_ORCH_EFFECT_COMMITTED && cursor.outcome==RAYA_ORCH_OUTCOME_SUCCESS,
        "Submitter Destroy cursor commits independently");
    CHECK(raya_orchestrator_complete_close_owned(&registry,handle,owner)==RAYA_ORCH_OK,
        "Borrowed Worker policy permits close after common Submitter lifecycle steps");
}

typedef struct {
    volatile LONG wait_calls;
    volatile LONG close_calls;
    volatile LONG reset_calls;
    volatile LONG fail_wait_at;
    volatile LONG fail_reset_at;
    PVOID volatile fail_close_handle_once;
} orchestrator_submitter_faults;
static orchestrator_submitter_faults orch_faults;
static HANDLE WINAPI orch_create_event(LPSECURITY_ATTRIBUTES a,BOOL m,BOOL i,LPCWSTR n) {
    return CreateEventW(a,m,i,n);
}
static HANDLE WINAPI orch_create_thread(LPSECURITY_ATTRIBUTES a,SIZE_T z,
    LPTHREAD_START_ROUTINE fn,LPVOID p,DWORD flags,LPDWORD id) {
    return CreateThread(a,z,fn,p,flags,id);
}
static BOOL WINAPI orch_set_event(HANDLE h) { return SetEvent(h); }
static BOOL WINAPI orch_reset_event(HANDLE h) {
    LONG call=InterlockedIncrement(&orch_faults.reset_calls);
    if (call==InterlockedCompareExchange(&orch_faults.fail_reset_at,0,0)) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return ResetEvent(h);
}
static DWORD WINAPI orch_wait(HANDLE h,DWORD ms) {
    LONG call=InterlockedIncrement(&orch_faults.wait_calls);
    if (call==InterlockedCompareExchange(&orch_faults.fail_wait_at,0,0)) {
        SetLastError(ERROR_INVALID_HANDLE);
        return WAIT_FAILED;
    }
    return WaitForSingleObject(h,ms);
}
static BOOL WINAPI orch_get_exit(HANDLE h,LPDWORD code) {
    return GetExitCodeThread(h,code);
}
static BOOL WINAPI orch_close(HANDLE h) {
    (void)InterlockedIncrement(&orch_faults.close_calls);
    if (InterlockedCompareExchangePointer(&orch_faults.fail_close_handle_once,
        NULL,h)==h) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return CloseHandle(h);
}
static const raya_submitter_win32_api orch_injected_api={
    orch_create_event,orch_create_thread,orch_set_event,orch_reset_event,
    orch_wait,orch_get_exit,orch_close
};
static void test_submitter_lifecycle_cursor_failure_chain(void) {
    raya_orch_registry registry;
    raya_orchestrator_context context;
    raya_integrated_submit_adapter adapter;
    raya_dedicated_submitter submitter;
    raya_managed_worker worker;
    raya_orch_handle handle=0;
    raya_orch_owner_token owner=0;
    raya_adapter_destroy_result adapter_result;
    raya_submitter_op_result operation_result;
    raya_orch_cleanup_step cursor;
    LONG active_count=-1, next_wait;
    memset(&orch_faults,0,sizeof(orch_faults));
    memset(&adapter,0,sizeof(adapter));
    memset(&submitter,0,sizeof(submitter));
    memset(&worker,0,sizeof(worker));
    raya_orchestrator_context_init(&context);
    CHECK(raya_orch_registry_init(&registry)==RAYA_ORCH_OK,
        "Lifecycle failure chain Registry initializes");
    InitializeCriticalSection(&adapter.lock);
    adapter.initialized=TRUE;
    CHECK(raya_submitter_start_with_api(&submitter,8,&orch_injected_api,
        orchestrator_test_issue,orchestrator_test_publish,
        orchestrator_test_not_issued,NULL),
        "Lifecycle failure chain starts Submitter with injected Win32 API");
    CHECK(raya_orchestrator_context_register(&registry,&context,&adapter,
        &submitter,&worker,RAYA_ORCH_WORKER_BORROWED,&handle)==RAYA_ORCH_OK,
        "Lifecycle failure chain Context registers");
    CHECK(raya_orch_begin_close(&registry,handle,&owner)==RAYA_ORCH_OK &&
        raya_orch_wait_drained(&registry,handle,owner,0)==RAYA_ORCH_OK,
        "Lifecycle failure chain obtains exclusive drained cleanup owner");
    CHECK(raya_orchestrator_destroy_adapter_owned(&registry,handle,owner,
        &adapter_result)==RAYA_ORCH_OK &&
        adapter_result.status==RAYA_ADAPTER_DESTROY_SUCCESS,
        "Lifecycle failure chain commits Adapter Destroy");
    CHECK(raya_orchestrator_close_submitter_admission_owned(&registry,handle,
        owner,&active_count)==RAYA_ORCH_OK && active_count==0,
        "Lifecycle failure chain closes and drains Submitter admission");
    CHECK(raya_orchestrator_submitter_shutdown_owned(&registry,handle,owner,
        &operation_result)==RAYA_ORCH_OK &&
        operation_result.status==RAYA_SUBMITTER_OP_SUCCESS,
        "Lifecycle failure chain commits Submitter Shutdown");
    CHECK(WaitForSingleObject(submitter.thread,5000)==WAIT_OBJECT_0,
        "Submitter worker thread exits before injected Join failure");
    next_wait=InterlockedCompareExchange(&orch_faults.wait_calls,0,0)+1;
    InterlockedExchange(&orch_faults.fail_wait_at,next_wait);
    CHECK(raya_orchestrator_submitter_join_owned(&registry,handle,owner,5000,
        &operation_result)==RAYA_ORCH_OK &&
        operation_result.status==RAYA_SUBMITTER_OP_RETRYABLE_FAILURE &&
        operation_result.effect==RAYA_SUBMITTER_EFFECT_NONE,
        "Injected Join wait failure is reported as retryable with no effect");
    CHECK(raya_orch_cursor_read(&registry,handle,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_JOIN,&cursor)==RAYA_ORCH_OK &&
        cursor.effect==RAYA_ORCH_EFFECT_NOT_ATTEMPTED &&
        cursor.outcome==RAYA_ORCH_OUTCOME_RETRYABLE_FAILURE &&
        cursor.attempts==1 && cursor.last_error==ERROR_INVALID_HANDLE,
        "Join cursor preserves retryable no-effect failure and native error");
    CHECK(raya_orchestrator_submitter_join_owned(&registry,handle,owner,5000,
        &operation_result)==RAYA_ORCH_OK &&
        operation_result.status==RAYA_SUBMITTER_OP_SUCCESS &&
        operation_result.effect==RAYA_SUBMITTER_EFFECT_THREAD_JOINED,
        "Join retries successfully after transient wait failure");
    CHECK(raya_orch_cursor_read(&registry,handle,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_JOIN,&cursor)==RAYA_ORCH_OK &&
        cursor.effect==RAYA_ORCH_EFFECT_COMMITTED &&
        cursor.outcome==RAYA_ORCH_OUTCOME_SUCCESS && cursor.attempts==2,
        "Join cursor commits the second attempt");
    InterlockedExchangePointer(&orch_faults.fail_close_handle_once,
        submitter.changed_event);
    CHECK(raya_orchestrator_submitter_destroy_owned(&registry,handle,owner,
        &operation_result)==RAYA_ORCH_OK &&
        operation_result.status==RAYA_SUBMITTER_OP_RETRYABLE_FAILURE &&
        operation_result.effect==RAYA_SUBMITTER_EFFECT_PARTIAL_DESTROY,
        "Injected second Event close failure reports known partial Destroy");
    CHECK(submitter.initialized && submitter.destroy_progress==1 &&
        submitter.wake_event==NULL && submitter.changed_event!=NULL,
        "Partial Destroy retains exact remaining resource state");
    CHECK(raya_orch_cursor_read(&registry,handle,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_DESTROY,&cursor)==RAYA_ORCH_OK &&
        cursor.effect==RAYA_ORCH_EFFECT_PARTIAL &&
        cursor.outcome==RAYA_ORCH_OUTCOME_RETRYABLE_FAILURE &&
        cursor.attempts==1 && cursor.last_error==ERROR_INVALID_HANDLE,
        "Destroy cursor records partial effect and retry classification");
    CHECK(raya_orchestrator_submitter_destroy_owned(&registry,handle,owner,
        &operation_result)==RAYA_ORCH_OK &&
        operation_result.status==RAYA_SUBMITTER_OP_SUCCESS &&
        operation_result.effect==RAYA_SUBMITTER_EFFECT_DESTROYED,
        "Destroy resumes from component progress and closes remaining Event");
    CHECK(raya_orch_cursor_read(&registry,handle,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_DESTROY,&cursor)==RAYA_ORCH_OK &&
        cursor.effect==RAYA_ORCH_EFFECT_COMMITTED &&
        cursor.outcome==RAYA_ORCH_OUTCOME_SUCCESS && cursor.attempts==2,
        "Destroy cursor commits after safe partial-progress retry");
    CHECK(raya_orchestrator_complete_close_owned(&registry,handle,owner)==RAYA_ORCH_OK,
        "Borrowed Context closes after all required Adapter and Submitter steps succeed");
}

static void test_submitter_abnormal_exit_cursor_reconciliation(void) {
    raya_orch_registry registry;
    raya_orchestrator_context context;
    raya_integrated_submit_adapter adapter;
    raya_dedicated_submitter submitter;
    raya_managed_worker worker;
    raya_orch_handle handle=0;
    raya_orch_owner_token owner=0;
    raya_adapter_destroy_result adapter_result;
    raya_submitter_op_result operation_result;
    raya_orch_cleanup_step cursor;
    LONG active_count=-1;
    memset(&orch_faults,0,sizeof(orch_faults));
    memset(&adapter,0,sizeof(adapter));
    memset(&submitter,0,sizeof(submitter));
    memset(&worker,0,sizeof(worker));
    raya_orchestrator_context_init(&context);
    InterlockedExchange(&orch_faults.fail_reset_at,1);
    CHECK(raya_orch_registry_init(&registry)==RAYA_ORCH_OK,
        "Abnormal-exit Registry initializes");
    InitializeCriticalSection(&adapter.lock);
    adapter.initialized=TRUE;
    CHECK(raya_submitter_start_with_api(&submitter,8,&orch_injected_api,
        orchestrator_test_issue,orchestrator_test_publish,
        orchestrator_test_not_issued,NULL),
        "Abnormal-exit Submitter starts with injected reset failure");
    CHECK(WaitForSingleObject(submitter.thread,5000)==WAIT_OBJECT_0 &&
        submitter.lifecycle==RAYA_SUBMITTER_DEGRADED,
        "Injected reset failure causes a signalled degraded Submitter thread");
    CHECK(raya_orchestrator_context_register(&registry,&context,&adapter,
        &submitter,&worker,RAYA_ORCH_WORKER_BORROWED,&handle)==RAYA_ORCH_OK,
        "Abnormal-exit Context registers");
    CHECK(raya_orch_begin_close(&registry,handle,&owner)==RAYA_ORCH_OK &&
        raya_orch_wait_drained(&registry,handle,owner,0)==RAYA_ORCH_OK,
        "Abnormal-exit cleanup owner is exclusive and drained");
    CHECK(raya_orchestrator_destroy_adapter_owned(&registry,handle,owner,
        &adapter_result)==RAYA_ORCH_OK &&
        adapter_result.status==RAYA_ADAPTER_DESTROY_SUCCESS,
        "Abnormal-exit path commits Adapter Destroy");
    CHECK(raya_orchestrator_close_submitter_admission_owned(&registry,handle,
        owner,&active_count)==RAYA_ORCH_OK && active_count==0,
        "Abnormal-exit path closes Submitter operational admission");
    CHECK(raya_orchestrator_submitter_shutdown_owned(&registry,handle,owner,
        &operation_result)==RAYA_ORCH_INVALID &&
        operation_result.status==RAYA_SUBMITTER_OP_TERMINAL_FAILURE,
        "Shutdown failure is surfaced instead of being treated as success");
    CHECK(raya_orch_cursor_read(&registry,handle,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_SHUTDOWN,&cursor)==RAYA_ORCH_OK &&
        cursor.effect==RAYA_ORCH_EFFECT_BLOCKED &&
        cursor.outcome==RAYA_ORCH_OUTCOME_UNCERTAIN,
        "Failed Shutdown is blocked pending explicit evidence reconciliation");
    CHECK(raya_orch_cursor_reconcile(&registry,handle,owner,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_SHUTDOWN,
        RAYA_ORCH_EFFECT_COMMITTED,RAYA_ORCH_OUTCOME_TERMINAL_FAILURE,
        ERROR_INVALID_STATE,ERROR_INVALID_HANDLE)==RAYA_ORCH_OK,
        "Owner reconciles Shutdown only after observing terminated thread and degraded state");
    CHECK(raya_orchestrator_submitter_join_owned(&registry,handle,owner,5000,
        &operation_result)==RAYA_ORCH_OK &&
        operation_result.status==RAYA_SUBMITTER_OP_TERMINAL_FAILURE &&
        operation_result.effect==RAYA_SUBMITTER_EFFECT_THREAD_JOINED,
        "Join records abnormal exit after successfully reaping thread handle");
    CHECK(raya_orch_cursor_read(&registry,handle,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_JOIN,&cursor)==RAYA_ORCH_OK &&
        cursor.effect==RAYA_ORCH_EFFECT_COMMITTED &&
        cursor.outcome==RAYA_ORCH_OUTCOME_ABNORMAL_EXIT &&
        cursor.exit_code==ERROR_INVALID_HANDLE,
        "Join cursor preserves abnormal exit code and committed join effect");
    CHECK(raya_orchestrator_submitter_destroy_owned(&registry,handle,owner,
        &operation_result)==RAYA_ORCH_INVALID &&
        operation_result.status==RAYA_SUBMITTER_OP_INVALID_STATE,
        "Destroy refuses abnormal-exit DEGRADED state despite joined thread");
    CHECK(raya_orch_cursor_read(&registry,handle,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_DESTROY,&cursor)==RAYA_ORCH_OK &&
        cursor.effect==RAYA_ORCH_EFFECT_BLOCKED &&
        cursor.outcome==RAYA_ORCH_OUTCOME_UNCERTAIN,
        "Unsafe Destroy is retained as blocked rather than bypassing component invariant");
    CHECK(raya_orch_release_cleanup_owner(&registry,handle,owner)==RAYA_ORCH_BUSY,
        "Cleanup ownership cannot be released while abnormal Destroy remains blocked");
    /* Test-harness-only reclamation: the supported Destroy API correctly refuses
       this DEGRADED state. Thread is joined and this stack-owned fixture is ending. */
    CHECK(CloseHandle(submitter.wake_event) && CloseHandle(submitter.changed_event),
        "Test harness closes fixture Events after asserting production Destroy refusal");
    DeleteCriticalSection(&submitter.lock);
    submitter.wake_event=NULL; submitter.changed_event=NULL;
    submitter.initialized=FALSE;
}

static HANDLE WINAPI orchestrator_suspended_worker_factory(
    LPTHREAD_START_ROUTINE entry, LPVOID parameter, void *context) {
    (void)context;
    return CreateThread(NULL,0,entry,parameter,CREATE_SUSPENDED,NULL);
}
static void test_owned_worker_lifecycle_cursor(void) {
    raya_orch_registry registry;
    raya_orchestrator_context context;
    raya_integrated_submit_adapter adapter;
    raya_dedicated_submitter submitter;
    raya_managed_worker worker;
    raya_orch_handle handle=0;
    raya_orch_owner_token owner=0;
    raya_worker_op_result result;
    uint32_t step;
    memset(&adapter,0,sizeof(adapter));
    memset(&submitter,0,sizeof(submitter));
    memset(&worker,0,sizeof(worker));
    raya_orchestrator_context_init(&context);
    CHECK(raya_orch_registry_init(&registry),"Owned Worker Registry initializes");
    CHECK(raya_managed_worker_start_with_factory(&worker,
        orchestrator_suspended_worker_factory,NULL),
        "Owned Worker starts with a suspended real thread for deterministic Join timeout");
    CHECK(raya_orchestrator_context_register(&registry,&context,&adapter,
        &submitter,&worker,RAYA_ORCH_WORKER_OWNED,&handle)==RAYA_ORCH_OK,
        "Owned Worker Context registers strict eight-step policy");
    CHECK(raya_orch_begin_close(&registry,handle,&owner)==RAYA_ORCH_OK &&
        raya_orch_wait_drained(&registry,handle,owner,0)==RAYA_ORCH_OK,
        "Owned Worker Context enters owner-controlled drained close");
    for(step=0;step<5u;++step) {
        CHECK(raya_orch_cursor_update(&registry,handle,owner,step,
            RAYA_ORCH_EFFECT_IN_PROGRESS,RAYA_ORCH_OUTCOME_NOT_EVALUATED,0,0)==RAYA_ORCH_OK &&
            raya_orch_cursor_update(&registry,handle,owner,step,
            RAYA_ORCH_EFFECT_COMMITTED,RAYA_ORCH_OUTCOME_SUCCESS,0,0)==RAYA_ORCH_OK,
            "Owned Worker predecessor cleanup step is committed");
    }
    CHECK(raya_orchestrator_worker_shutdown_owned(&registry,handle,owner,&result)==RAYA_ORCH_OK &&
        result.status==RAYA_WORKER_OP_SUCCESS &&
        result.effect==RAYA_WORKER_EFFECT_SHUTDOWN_SIGNALLED,
        "Orchestrator executes Worker Shutdown only after common cleanup");
    CHECK(raya_orchestrator_worker_join_owned(&registry,handle,owner,0,&result)==RAYA_ORCH_OK &&
        result.status==RAYA_WORKER_OP_RETRYABLE_FAILURE &&
        result.retry==RAYA_WORKER_RETRY_SAME_OPERATION &&
        result.win32_error==ERROR_TIMEOUT,
        "Worker Join timeout is recorded as a no-effect retryable cursor result");    CHECK(ResumeThread(worker.thread)!=(DWORD)-1,
        "Test releases suspended Worker thread after deterministic Join timeout");
    CHECK(raya_orchestrator_worker_join_owned(&registry,handle,owner,5000,&result)==RAYA_ORCH_OK &&
        result.status==RAYA_WORKER_OP_SUCCESS &&
        result.effect==RAYA_WORKER_EFFECT_THREAD_REAPED &&
        result.thread_exit_code==ERROR_SUCCESS,
        "Orchestrator retries Join and reaps Worker after normal thread exit");
    CHECK(raya_orchestrator_worker_destroy_owned(&registry,handle,owner,&result)==RAYA_ORCH_OK &&
        result.status==RAYA_WORKER_OP_SUCCESS &&
        result.effect==RAYA_WORKER_EFFECT_DESTROYED,
        "Orchestrator destroys Worker only after successful Join");
    CHECK(raya_orchestrator_complete_close_owned(&registry,handle,owner)==RAYA_ORCH_OK,
        "Owned Context reclaims only after all eight lifecycle steps succeed");
}
static DWORD WINAPI orchestrator_abnormal_worker_thread(void *parameter) {
    (void)parameter;
    return ERROR_ACCESS_DENIED;
}
static HANDLE WINAPI orchestrator_abnormal_worker_factory(
    LPTHREAD_START_ROUTINE entry, LPVOID parameter, void *context) {
    (void)entry; (void)parameter; (void)context;
    return CreateThread(NULL,0,orchestrator_abnormal_worker_thread,NULL,0,NULL);
}
static void test_owned_worker_abnormal_join_blocks_reclaim(void) {
    raya_orch_registry registry;
    raya_orchestrator_context context;
    raya_integrated_submit_adapter adapter;
    raya_dedicated_submitter submitter;
    raya_managed_worker worker;
    raya_orch_handle handle=0;
    raya_orch_owner_token owner=0;
    raya_worker_op_result result;
    raya_orch_cleanup_step cursor;
    uint32_t step;
    memset(&adapter,0,sizeof(adapter));
    memset(&submitter,0,sizeof(submitter));
    memset(&worker,0,sizeof(worker));
    raya_orchestrator_context_init(&context);
    CHECK(raya_orch_registry_init(&registry),"Abnormal Worker Registry initializes");
    CHECK(raya_managed_worker_start_with_factory(&worker,
        orchestrator_abnormal_worker_factory,NULL),
        "Abnormal Worker fixture starts a deterministic failing thread");
    CHECK(raya_orchestrator_context_register(&registry,&context,&adapter,
        &submitter,&worker,RAYA_ORCH_WORKER_OWNED,&handle)==RAYA_ORCH_OK,
        "Abnormal Worker Context registers");
    CHECK(raya_orch_begin_close(&registry,handle,&owner)==RAYA_ORCH_OK &&
        raya_orch_wait_drained(&registry,handle,owner,0)==RAYA_ORCH_OK,
        "Abnormal Worker Context enters drained close");
    for(step=0;step<5u;++step) {
        CHECK(raya_orch_cursor_update(&registry,handle,owner,step,
            RAYA_ORCH_EFFECT_IN_PROGRESS,RAYA_ORCH_OUTCOME_NOT_EVALUATED,0,0)==RAYA_ORCH_OK &&
            raya_orch_cursor_update(&registry,handle,owner,step,
            RAYA_ORCH_EFFECT_COMMITTED,RAYA_ORCH_OUTCOME_SUCCESS,0,0)==RAYA_ORCH_OK,
            "Abnormal Worker predecessor step is committed");
    }
    CHECK(raya_orchestrator_worker_shutdown_owned(&registry,handle,owner,&result)==RAYA_ORCH_OK,
        "Abnormal Worker Shutdown is requested");
    CHECK(raya_orchestrator_worker_join_owned(&registry,handle,owner,5000,&result)==RAYA_ORCH_OK &&
        result.status==RAYA_WORKER_OP_TERMINAL_FAILURE &&
        result.effect==RAYA_WORKER_EFFECT_THREAD_REAPED &&
        result.thread_exit_code==ERROR_ACCESS_DENIED,
        "Abnormal Join records reaped handle without claiming healthy exit");
    CHECK(raya_orch_cursor_read(&registry,handle,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_WORKER_JOIN,&cursor)==RAYA_ORCH_OK &&
        cursor.effect==RAYA_ORCH_EFFECT_COMMITTED &&
        cursor.outcome==RAYA_ORCH_OUTCOME_ABNORMAL_EXIT,
        "Worker Join cursor preserves ABNORMAL_EXIT evidence");
    CHECK(raya_orchestrator_worker_destroy_owned(&registry,handle,owner,&result)==RAYA_ORCH_BUSY,
        "Worker Destroy is blocked after abnormal Join");
    CHECK(raya_orch_cursor_read(&registry,handle,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_WORKER_DESTROY,&cursor)==RAYA_ORCH_OK &&
        cursor.effect==RAYA_ORCH_EFFECT_NOT_ATTEMPTED,
        "Blocked Destroy does not falsely enter the component API");
    CHECK(raya_orchestrator_complete_close_owned(&registry,handle,owner)==RAYA_ORCH_BUSY,
        "Abnormal Worker prevents Context reclamation");
    /* Test-only fixture cleanup: production Destroy correctly refuses STOPPING. */
    CHECK(CloseHandle(worker.wake_event),
        "Test harness closes wake Event after production Destroy refusal");
    DeleteCriticalSection(&worker.lock);
    worker.wake_event=NULL; worker.initialized=FALSE;
}
typedef struct {
    HANDLE done;
    volatile LONG calls;
    DWORD error, transferred;
    char data[32];
} orch_real_io_result;

static void orch_real_io_callback(DWORD error,DWORD transferred,
    const void *data,void *context) {
    orch_real_io_result *r=(orch_real_io_result *)context;
    r->error=error; r->transferred=transferred;
    if (data!=NULL && transferred<=sizeof(r->data))
        memcpy(r->data,data,transferred);
    InterlockedIncrement(&r->calls);
    SetEvent(r->done);
}

static void test_real_io_orchestrator_owned_lifecycle(void) {
    raya_orch_registry registry;
    raya_orchestrator_context context;
    raya_integrated_submit_adapter adapter;
    raya_dedicated_submitter submitter;
    raya_managed_worker worker;
    raya_orch_handle handle=0;
    raya_orch_owner_token owner=0;
    raya_orchestrator_lease lease;
    raya_adapter_destroy_result ar;
    raya_submitter_op_result sr;
    raya_worker_op_result wr;
    raya_submitter_lifecycle life=(raya_submitter_lifecycle)0;
    orch_real_io_result io;
    HANDLE server=INVALID_HANDLE_VALUE,client=INVALID_HANDLE_VALUE;
    const char payload[]="orch-real-io";
    char received[32];
    char name[96];
    DWORD written=0,deadline,wait_result;
    size_t queued=0,producers=0,active_issue=0,issuer_leases=0;
    LONG entrants=0;
    uint32_t step;
    BOOL adapter_empty=FALSE,worker_empty=FALSE,snapshot_ok;
    raya_adapter_result submit_result;
    memset(&adapter,0,sizeof(adapter)); memset(&submitter,0,sizeof(submitter));
    memset(&worker,0,sizeof(worker)); memset(&io,0,sizeof(io));
    memset(&lease,0,sizeof(lease));
    io.done=CreateEventW(NULL,TRUE,FALSE,NULL);
    CHECK(io.done!=NULL,"E2E callback event created");
    if (!io.done) return;
    _snprintf_s(name,sizeof(name),_TRUNCATE,
        "\\\\.\\pipe\\raya-orch-e2e-%lu",GetCurrentProcessId());
    server=CreateNamedPipeA(name,PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,4096,4096,0,NULL);
    CHECK(server!=INVALID_HANDLE_VALUE,"E2E named-pipe server created");
    if (server==INVALID_HANDLE_VALUE) { CloseHandle(io.done); return; }
    client=CreateFileA(name,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
    CHECK(client!=INVALID_HANDLE_VALUE,"E2E named-pipe client connected");
    if (client==INVALID_HANDLE_VALUE) {
        CloseHandle(server); CloseHandle(io.done); return;
    }
    CHECK(raya_orch_registry_init(&registry),"E2E Registry initialized");
    CHECK(raya_managed_worker_start(&worker),"E2E real Worker started");
    if (!worker.initialized || !worker.started) ExitProcess(2);
    CHECK(raya_integrated_adapter_start(&adapter,&submitter,&worker,4),
        "E2E Adapter started and Submitter initialized");
    if (!adapter.initialized || !submitter.initialized) ExitProcess(2);
    raya_orchestrator_context_init(&context);
    CHECK(raya_orchestrator_context_register(&registry,&context,&adapter,
        &submitter,&worker,RAYA_ORCH_WORKER_OWNED,&handle)==RAYA_ORCH_OK && handle!=0,
        "E2E Context registered with strict OWNED policy");
    if (handle==0) ExitProcess(2);
    CHECK(raya_orchestrator_acquire(&registry,handle,&lease)==RAYA_ORCH_OK && lease.active,
        "E2E Context lease acquired");
    if (!lease.active) ExitProcess(2);
    CHECK(raya_orchestrator_release(&lease)==RAYA_ORCH_OK,"E2E Context lease released");
    CHECK(raya_orch_begin_close(&registry,handle,&owner)==RAYA_ORCH_OK && owner!=0,
        "E2E Context enters closing with exclusive owner");
    if (owner==0 || raya_orch_wait_drained(&registry,handle,owner,0)!=RAYA_ORCH_OK)
        ExitProcess(2);

    submit_result=raya_integrated_adapter_submit(&adapter,server,
        RAYA_MANAGED_PIPE_READ,NULL,(DWORD)(sizeof(payload)-1),
        orch_real_io_callback,&io);
    CHECK(submit_result==RAYA_ADAPTER_QUEUED,"E2E READ admitted through Adapter/Submitter");
    if (submit_result!=RAYA_ADAPTER_QUEUED) ExitProcess(2);
    CHECK(WriteFile(client,payload,(DWORD)(sizeof(payload)-1),&written,NULL) &&
        written==sizeof(payload)-1,"E2E client writes payload after READ submission");
    if (written!=sizeof(payload)-1) ExitProcess(2);
    wait_result=WaitForSingleObject(io.done,5000);
    CHECK(wait_result==WAIT_OBJECT_0,"E2E Worker completion callback arrives within deadline");
    if (wait_result!=WAIT_OBJECT_0) ExitProcess(2);
    CHECK(InterlockedCompareExchange(&io.calls,0,0)==1 &&
        io.error==ERROR_SUCCESS && io.transferred==sizeof(payload)-1 &&
        memcmp(io.data,payload,sizeof(payload)-1)==0,
        "E2E callback reports one successful transfer with exact payload");
    if (io.error!=ERROR_SUCCESS || io.transferred!=sizeof(payload)-1 ||
        memcmp(io.data,payload,sizeof(payload)-1)!=0) ExitProcess(2);

    deadline=GetTickCount()+5000;
    adapter_empty=FALSE;
    while ((LONG)(deadline-GetTickCount())>0) {
        EnterCriticalSection(&adapter.lock);
        adapter_empty=(adapter.active_count==0);
        LeaveCriticalSection(&adapter.lock);
        if (adapter_empty) break;
        Sleep(1);
    }
    CHECK(adapter_empty,"E2E completed READ leaves Adapter envelope drained before next request");
    if (!adapter_empty) ExitProcess(2);
    ResetEvent(io.done);
    InterlockedExchange(&io.calls,0);
    io.error=0; io.transferred=0; memset(io.data,0,sizeof(io.data));
    submit_result=raya_integrated_adapter_submit(&adapter,server,
        RAYA_MANAGED_PIPE_WRITE,payload,(DWORD)(sizeof(payload)-1),
        orch_real_io_callback,&io);
    CHECK(submit_result==RAYA_ADAPTER_QUEUED,"E2E WRITE admitted through Adapter/Submitter");
    if (submit_result!=RAYA_ADAPTER_QUEUED) ExitProcess(2);
    written=0;
    CHECK(ReadFile(client,received,sizeof(received),&written,NULL) &&
        written==sizeof(payload)-1 &&
        memcmp(received,payload,sizeof(payload)-1)==0,
        "E2E client receives exact payload from Adapter WRITE");
    if (written!=sizeof(payload)-1 ||
        memcmp(received,payload,sizeof(payload)-1)!=0) ExitProcess(2);
    wait_result=WaitForSingleObject(io.done,5000);
    CHECK(wait_result==WAIT_OBJECT_0,"E2E WRITE completion callback arrives within deadline");
    if (wait_result!=WAIT_OBJECT_0) ExitProcess(2);
    CHECK(InterlockedCompareExchange(&io.calls,0,0)==1 &&
        io.error==ERROR_SUCCESS && io.transferred==sizeof(payload)-1,
        "E2E WRITE callback reports exactly one successful transfer");
    if (io.error!=ERROR_SUCCESS || io.transferred!=sizeof(payload)-1) ExitProcess(2);

    deadline=GetTickCount()+5000;
    while ((LONG)(deadline-GetTickCount())>0) {
        EnterCriticalSection(&adapter.lock);
        adapter_empty=(adapter.active_count==0 && adapter.retained_count==0 &&
            adapter.retained_head==NULL);
        LeaveCriticalSection(&adapter.lock);
        EnterCriticalSection(&worker.lock);
        worker_empty=(worker.integrated.core.caller_active==0 &&
            worker.integrated.core.outstanding==0 && worker.integrated.core.queued==0 &&
            worker.integrated.core.head==NULL && worker.integrated.core.tail==NULL &&
            worker.integrated.reserved_slots==0 && worker.integrated.registry.count==0 &&
            worker.retained_count==0 && worker.retained_head==NULL);
        LeaveCriticalSection(&worker.lock);
        if (adapter_empty && worker_empty) break;
        Sleep(1);
    }
    CHECK(adapter_empty,"E2E Adapter active/retained inventory is empty");
    CHECK(worker_empty,"E2E Worker core/reservation/registry/retained inventory is empty");
    if (!adapter_empty || !worker_empty) ExitProcess(2);
    snapshot_ok=raya_submitter_snapshot(&submitter,&life,&queued,&producers,
        &active_issue,&issuer_leases);
    CHECK(snapshot_ok && life==RAYA_SUBMITTER_RUNNING && queued==0 &&
        producers==0 && active_issue==0 && issuer_leases==0,
        "E2E Submitter snapshot confirms drained live state");
    if (!snapshot_ok || queued || producers || active_issue || issuer_leases)
        ExitProcess(2);

    CHECK(raya_orchestrator_destroy_adapter_owned(&registry,handle,owner,&ar)==RAYA_ORCH_OK &&
        ar.status==RAYA_ADAPTER_DESTROY_SUCCESS &&
        ar.effect==RAYA_ADAPTER_DESTROY_EFFECT_DESTROYED,"E2E Adapter Destroy committed");
    if (ar.status!=RAYA_ADAPTER_DESTROY_SUCCESS) ExitProcess(2);
    CHECK(raya_orchestrator_close_submitter_admission_owned(&registry,handle,owner,
        &entrants)==RAYA_ORCH_OK && entrants==0,"E2E Submitter admission closed and drained");
    if (entrants!=0) ExitProcess(2);
    CHECK(raya_orchestrator_submitter_shutdown_owned(&registry,handle,owner,&sr)==RAYA_ORCH_OK &&
        sr.status==RAYA_SUBMITTER_OP_SUCCESS,"E2E Submitter Shutdown committed");
    if (sr.status!=RAYA_SUBMITTER_OP_SUCCESS) ExitProcess(2);
    CHECK(raya_orchestrator_submitter_join_owned(&registry,handle,owner,5000,&sr)==RAYA_ORCH_OK &&
        sr.status==RAYA_SUBMITTER_OP_SUCCESS,"E2E Submitter Join succeeded");
    if (sr.status!=RAYA_SUBMITTER_OP_SUCCESS) ExitProcess(2);
    CHECK(raya_orchestrator_submitter_destroy_owned(&registry,handle,owner,&sr)==RAYA_ORCH_OK &&
        sr.status==RAYA_SUBMITTER_OP_SUCCESS,"E2E Submitter Destroy succeeded");
    if (sr.status!=RAYA_SUBMITTER_OP_SUCCESS) ExitProcess(2);
    CHECK(raya_orchestrator_worker_shutdown_owned(&registry,handle,owner,&wr)==RAYA_ORCH_OK &&
        wr.status==RAYA_WORKER_OP_SUCCESS,"E2E OWNED Worker Shutdown succeeded");
    if (wr.status!=RAYA_WORKER_OP_SUCCESS) ExitProcess(2);
    CHECK(raya_orchestrator_worker_join_owned(&registry,handle,owner,5000,&wr)==RAYA_ORCH_OK &&
        wr.status==RAYA_WORKER_OP_SUCCESS && wr.thread_exit_code==ERROR_SUCCESS,
        "E2E Worker Join observed normal exit");
    if (wr.status!=RAYA_WORKER_OP_SUCCESS) ExitProcess(2);
    CHECK(raya_orchestrator_worker_destroy_owned(&registry,handle,owner,&wr)==RAYA_ORCH_OK &&
        wr.status==RAYA_WORKER_OP_SUCCESS,"E2E Worker Destroy succeeded");
    if (wr.status!=RAYA_WORKER_OP_SUCCESS) ExitProcess(2);
    for (step=0;step<RAYA_ORCH_CLEANUP_STEPS;++step) {
        raya_orch_cleanup_step cursor;
        CHECK(raya_orch_cursor_read(&registry,handle,step,&cursor)==RAYA_ORCH_OK &&
            cursor.effect==RAYA_ORCH_EFFECT_COMMITTED &&
            cursor.outcome==RAYA_ORCH_OUTCOME_SUCCESS,
            "E2E each required cleanup cursor step is committed SUCCESS");
    }
    CHECK(raya_orchestrator_complete_close_owned(&registry,handle,owner)==RAYA_ORCH_OK,
        "E2E strict Context reclamation succeeds after all eight steps");
    CHECK(CloseHandle(client),"E2E client handle closes");
    CHECK(CloseHandle(server),"E2E server handle closes");
    CHECK(CloseHandle(io.done),"E2E completion event closes");
}

static void test_borrowed_worker_lifecycle_boundary(void) {
    raya_orch_registry registry;
    raya_orchestrator_context context;
    raya_integrated_submit_adapter adapter;
    raya_dedicated_submitter submitter;
    raya_managed_worker worker;
    raya_orch_handle handle=0;
    raya_orch_owner_token owner=0;
    raya_adapter_destroy_result ar;
    raya_submitter_op_result sr;
    raya_worker_op_result wr;
    LONG entrants=0;
    uint32_t step;
    memset(&adapter,0,sizeof(adapter)); memset(&submitter,0,sizeof(submitter));
    memset(&worker,0,sizeof(worker));
    CHECK(raya_orch_registry_init(&registry),"Borrowed E2E Registry initializes");
    CHECK(raya_managed_worker_start(&worker),"Borrowed E2E external Worker starts");
    if (!worker.initialized || !worker.started) ExitProcess(2);
    CHECK(raya_integrated_adapter_start(&adapter,&submitter,&worker,4),
        "Borrowed E2E Adapter starts and initializes Submitter");
    if (!adapter.initialized || !submitter.initialized) ExitProcess(2);
    raya_orchestrator_context_init(&context);
    CHECK(raya_orchestrator_context_register(&registry,&context,&adapter,
        &submitter,&worker,RAYA_ORCH_WORKER_BORROWED,&handle)==RAYA_ORCH_OK && handle!=0,
        "Borrowed E2E Context registers with five-step strict policy");
    if (handle==0) ExitProcess(2);
    CHECK(raya_orch_begin_close(&registry,handle,&owner)==RAYA_ORCH_OK && owner!=0,
        "Borrowed E2E Context enters owner-controlled close");
    if (owner==0 || raya_orch_wait_drained(&registry,handle,owner,0)!=RAYA_ORCH_OK)
        ExitProcess(2);
    CHECK(raya_orchestrator_destroy_adapter_owned(&registry,handle,owner,&ar)==RAYA_ORCH_OK &&
        ar.status==RAYA_ADAPTER_DESTROY_SUCCESS,
        "Borrowed E2E Orchestrator destroys Adapter only");
    if (ar.status!=RAYA_ADAPTER_DESTROY_SUCCESS) ExitProcess(2);
    CHECK(raya_orchestrator_close_submitter_admission_owned(&registry,handle,owner,
        &entrants)==RAYA_ORCH_OK && entrants==0,
        "Borrowed E2E Submitter admission closes and drains");
    if (entrants!=0) ExitProcess(2);
    CHECK(raya_orchestrator_submitter_shutdown_owned(&registry,handle,owner,&sr)==RAYA_ORCH_OK &&
        sr.status==RAYA_SUBMITTER_OP_SUCCESS,"Borrowed E2E Submitter Shutdown succeeds");
    if (sr.status!=RAYA_SUBMITTER_OP_SUCCESS) ExitProcess(2);
    CHECK(raya_orchestrator_submitter_join_owned(&registry,handle,owner,5000,&sr)==RAYA_ORCH_OK &&
        sr.status==RAYA_SUBMITTER_OP_SUCCESS,"Borrowed E2E Submitter Join succeeds");
    if (sr.status!=RAYA_SUBMITTER_OP_SUCCESS) ExitProcess(2);
    CHECK(raya_orchestrator_submitter_destroy_owned(&registry,handle,owner,&sr)==RAYA_ORCH_OK &&
        sr.status==RAYA_SUBMITTER_OP_SUCCESS,"Borrowed E2E Submitter Destroy succeeds");
    if (sr.status!=RAYA_SUBMITTER_OP_SUCCESS) ExitProcess(2);
    for (step=0;step<5u;++step) {
        raya_orch_cleanup_step cursor;
        CHECK(raya_orch_cursor_read(&registry,handle,step,&cursor)==RAYA_ORCH_OK &&
            cursor.effect==RAYA_ORCH_EFFECT_COMMITTED &&
            cursor.outcome==RAYA_ORCH_OUTCOME_SUCCESS,
            "Borrowed E2E common cleanup cursor step is committed SUCCESS");
    }
    CHECK(raya_orchestrator_complete_close_owned(&registry,handle,owner)==RAYA_ORCH_OK,
        "Borrowed Context reclaims without Orchestrator Worker teardown");
    CHECK(worker.initialized && worker.started && !worker.joined && worker.thread!=NULL &&
        WaitForSingleObject(worker.thread,0)==WAIT_TIMEOUT &&
        worker.integrated.core.lifecycle==RAYA_REAPER_ACCEPTING,
        "External Worker remains alive and accepting after Borrowed Context reclamation");
    CHECK(raya_managed_worker_admit(&worker),
        "External owner can still admit work through the Borrowed Worker");
    CHECK(raya_managed_worker_finish(&worker),
        "External owner can finish its Borrowed Worker admission");
    CHECK(raya_managed_worker_shutdown_ex(&worker,&wr)==RAYA_WORKER_OP_SUCCESS &&
        wr.status==RAYA_WORKER_OP_SUCCESS,
        "External lifecycle owner performs Worker Shutdown");
    CHECK(raya_managed_worker_join_ex(&worker,5000,&wr)==RAYA_WORKER_OP_SUCCESS &&
        wr.status==RAYA_WORKER_OP_SUCCESS && wr.thread_exit_code==ERROR_SUCCESS,
        "External lifecycle owner joins Worker with normal exit");
    CHECK(raya_managed_worker_destroy_ex(&worker,&wr)==RAYA_WORKER_OP_SUCCESS &&
        wr.status==RAYA_WORKER_OP_SUCCESS,
        "External lifecycle owner destroys Worker after Context reclamation");
}

int main(void) {
    test_adapter_destroy_cursor_mapping();
    test_submitter_abnormal_exit_cursor_reconciliation();
    test_submitter_lifecycle_cursor_failure_chain();
    test_submitter_admission_cursor_integration();
    test_owned_worker_lifecycle_cursor();
    test_owned_worker_abnormal_join_blocks_reclaim();
    test_real_io_orchestrator_owned_lifecycle();
    test_borrowed_worker_lifecycle_boundary();
    raya_orch_registry registry;
    raya_orchestrator_context context;
    raya_orchestrator_lease lease, copied;
    raya_integrated_submit_adapter adapter, wrong_adapter;
    raya_dedicated_submitter submitter, wrong_submitter;
    raya_managed_worker worker, wrong_worker;
    raya_orch_handle handle=0, duplicate_handle=99;
    raya_orch_owner_token owner=0;
    raya_orchestrator_context *owner_context=NULL;
    raya_worker_op_result borrowed_worker_result;
    raya_orchestrator_context_init(&context);
    memset(&adapter,0,sizeof(adapter));
    memset(&wrong_adapter,0,sizeof(wrong_adapter));
    memset(&submitter,0,sizeof(submitter));
    memset(&wrong_submitter,0,sizeof(wrong_submitter));
    memset(&worker,0,sizeof(worker));
    memset(&wrong_worker,0,sizeof(wrong_worker));

    CHECK(raya_orch_registry_init(&registry),"registry initializes");
    CHECK(raya_orchestrator_context_register(&registry,&context,&adapter,
        &submitter,&worker,RAYA_ORCH_WORKER_BORROWED,&handle)==RAYA_ORCH_OK &&
        handle!=0,"borrowed-worker Context registers with generation handle");
    CHECK(raya_orchestrator_context_register(&registry,&context,&adapter,
        &submitter,&worker,RAYA_ORCH_WORKER_BORROWED,&duplicate_handle)==RAYA_ORCH_INVALID &&
        duplicate_handle==0,
        "already-registered Context cannot be registered twice");
    CHECK(raya_orchestrator_acquire(&registry,handle,&lease)==RAYA_ORCH_OK,
        "valid handle acquires typed Context lease");
    CHECK(raya_orchestrator_lease_adapter(&lease)==&adapter &&
        raya_orchestrator_lease_submitter(&lease)==&submitter &&
        raya_orchestrator_lease_worker(&lease)==&worker,
        "component pointers exposed only through active lease");
    CHECK(raya_orchestrator_lease_worker_ownership(&lease)==RAYA_ORCH_WORKER_BORROWED,
        "Borrowed ownership policy preserved");
    CHECK(raya_orchestrator_lease_adapter(NULL)==NULL &&
        raya_orchestrator_lease_submitter(NULL)==NULL &&
        raya_orchestrator_lease_worker(NULL)==NULL,
        "null lease never exposes component pointer");
    copied=lease;
    CHECK(raya_orchestrator_release(&lease)==RAYA_ORCH_OK,
        "original lease releases");
    CHECK(raya_orchestrator_lease_adapter(&lease)==NULL,
        "released lease no longer exposes Context");
    CHECK(raya_orchestrator_lease_adapter(&copied)==NULL,
        "stale copied lease cannot expose Context after original release");
    CHECK(raya_orchestrator_release(&copied)==RAYA_ORCH_INVALID,
        "copied lease cannot double-release registry identity");
    CHECK(raya_orchestrator_acquire(&registry,handle,&lease)==RAYA_ORCH_OK,
        "new lease can be acquired after prior release");
    CHECK(raya_orchestrator_owner_get_context(&registry,handle,1,&owner_context)==RAYA_ORCH_BUSY &&
        owner_context==NULL,"owner Context retrieval denied before BeginClose");
    CHECK(raya_orch_begin_close(&registry,handle,&owner)==RAYA_ORCH_OK && owner!=0,
        "BeginClose blocks subsequent acquisitions");
    CHECK(raya_orchestrator_acquire(&registry,handle,&copied)==RAYA_ORCH_INVALID,
        "closing handle rejected before Context dereference");
    CHECK(raya_orchestrator_owner_get_context(&registry,handle,owner+1,&owner_context)==RAYA_ORCH_BUSY &&
        owner_context==NULL,"wrong cleanup owner cannot retrieve Context");
    CHECK(raya_orchestrator_owner_get_context(&registry,handle,owner,&owner_context)==RAYA_ORCH_BUSY &&
        owner_context==NULL,"Context retrieval waits for outstanding API leases");
    CHECK(raya_orchestrator_release(&lease)==RAYA_ORCH_OK,
        "pre-close lease releases after admission closes");
    CHECK(raya_orch_wait_drained(&registry,handle,owner,0)==RAYA_ORCH_OK,
        "pre-close API leases fully drain");
    CHECK(raya_orchestrator_owner_get_context(&registry,handle,owner,&owner_context)==RAYA_ORCH_OK &&
        owner_context==&context && owner_context->worker==&worker,
        "exact cleanup owner retrieves Context only after lease drain");
    CHECK(raya_orchestrator_worker_shutdown_owned(&registry,handle,owner,
        &borrowed_worker_result)==RAYA_ORCH_INVALID,
        "BORROWED Worker rejects Orchestrator-owned teardown without invoking Worker API");
    CHECK(raya_orch_complete_close(&registry,handle,owner)==RAYA_ORCH_BUSY,
        "direct generic Registry close cannot bypass Context policy");
    CHECK(raya_orch_complete_close_required(&registry,handle,owner,
        RAYA_ORCHESTRATOR_CONTEXT_KIND)==RAYA_ORCH_BUSY,
        "direct strict close cannot reclaim Context before required cleanup");
    {
        uint32_t step;
        for (step=0;step<5u;++step) {
            CHECK(raya_orch_cursor_update(&registry,handle,owner,step,
                RAYA_ORCH_EFFECT_IN_PROGRESS,RAYA_ORCH_OUTCOME_NOT_EVALUATED,0,0)==RAYA_ORCH_OK &&
                raya_orch_cursor_update(&registry,handle,owner,step,
                RAYA_ORCH_EFFECT_COMMITTED,RAYA_ORCH_OUTCOME_SUCCESS,0,0)==RAYA_ORCH_OK,
                "Borrowed Context required lifecycle step commits");
        }
    }
    CHECK(raya_orchestrator_complete_close_owned(&registry,handle,owner)==RAYA_ORCH_OK,
        "Borrowed Context reclaims after common steps without Worker teardown");
    CHECK(raya_orchestrator_acquire(&registry,handle,&copied)==RAYA_ORCH_INVALID,
        "stale generation handle rejected after Context reclamation");

    {
        raya_orchestrator_context context2;
        raya_orch_handle handle2=0;
        raya_orchestrator_context_init(&context2);
        CHECK(raya_orchestrator_context_register(&registry,&context2,&wrong_adapter,
            &wrong_submitter,&wrong_worker,RAYA_ORCH_WORKER_OWNED,&handle2)==RAYA_ORCH_OK,
            "owned-worker Context registers");
        CHECK(raya_orchestrator_acquire(&registry,handle2,&lease)==RAYA_ORCH_OK &&
            raya_orchestrator_lease_worker_ownership(&lease)==RAYA_ORCH_WORKER_OWNED,
            "Owned ownership policy preserved");
        CHECK(raya_orchestrator_release(&lease)==RAYA_ORCH_OK,
            "Owned Context lease releases");
        {
            owner=0;
            CHECK(raya_orch_begin_close(&registry,handle2,&owner)==RAYA_ORCH_OK,
                "Owned Context enters closing");
            CHECK(raya_orch_wait_drained(&registry,handle2,owner,0)==RAYA_ORCH_OK,
                "Owned Context has no remaining leases");
            CHECK(raya_orchestrator_owner_get_context(&registry,handle2,owner,&owner_context)==RAYA_ORCH_OK &&
                owner_context==&context2 && owner_context->worker_ownership==RAYA_ORCH_WORKER_OWNED,
                "cleanup owner retrieves Owned Context after drain");
            CHECK(raya_orch_complete_close(&registry,handle2,owner)==RAYA_ORCH_BUSY,
                "generic Registry close cannot bypass Owned Worker teardown policy");
            CHECK(raya_orchestrator_complete_close_owned(&registry,handle2,owner)==RAYA_ORCH_BUSY,
                "Owned Context remains registered until Worker cleanup steps succeed");
            {
                uint32_t step;
                for (step=0;step<RAYA_ORCH_CLEANUP_STEPS;++step) {
                    CHECK(raya_orch_cursor_update(&registry,handle2,owner,step,
                        RAYA_ORCH_EFFECT_IN_PROGRESS,RAYA_ORCH_OUTCOME_NOT_EVALUATED,0,0)==RAYA_ORCH_OK &&
                        raya_orch_cursor_update(&registry,handle2,owner,step,
                        RAYA_ORCH_EFFECT_COMMITTED,RAYA_ORCH_OUTCOME_SUCCESS,0,0)==RAYA_ORCH_OK,
                        "Owned Context lifecycle step commits");
                }
            }
            CHECK(raya_orchestrator_complete_close_owned(&registry,handle2,owner)==RAYA_ORCH_OK,
                "Owned Context reclaims only after all eight cleanup steps succeed");
        }
    }
    printf("SUMMARY: %d passed; %d failed\n",passed,failures);
    return failures ? 1 : 0;
}
