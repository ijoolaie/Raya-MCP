#include "reaper-managed-worker-v2.h"
#include <string.h>

static int valid_handle(HANDLE h) {
    return h != NULL && h != INVALID_HANDLE_VALUE;
}
static BOOL close_owned_handle(HANDLE h, void *context) {
    (void)context;
    return CloseHandle(h);
}
static void mark_cause_locked(raya_managed_worker *w, uint32_t cause) {
    (void)raya_reaper_mark_degraded_cause(&w->integrated.core, cause);
}
static DWORD WINAPI managed_worker_main(void *parameter);
static volatile LONG64 g_worker_epoch_sequence=0;

static int worker_allocate_epoch(ULONGLONG *epoch) {
    LONG64 value;
    if (!epoch) return 0;
    value=InterlockedIncrement64(&g_worker_epoch_sequence);
    if (value<=0) return 0;
    *epoch=(ULONGLONG)value;
    return 1;
}

int raya_managed_operation_init(raya_managed_operation *op,
    HANDLE pipe, HANDLE event_handle, void *buffer, DWORD buffer_length,
    raya_managed_release_fn release, void *release_context) {
    if (op == NULL || !valid_handle(pipe) || !valid_handle(event_handle) ||
        pipe == event_handle || release == NULL) return 0;
    memset(op, 0, sizeof(*op));
    op->integrated.base.state = RAYA_REAPER_OP_CALLER_PENDING;
    op->integrated.completion_event = event_handle;
    op->integrated.collect_state = RAYA_INTEGRATED_COLLECT_PENDING;
    op->integrated.reservation_state = RAYA_INTEGRATED_RESERVATION_NONE;
    op->overlapped.hEvent = event_handle;
    op->issuing_pipe = pipe;
    op->buffer = buffer;
    op->buffer_length = buffer_length;
    op->release = release;
    op->release_context = release_context;
    raya_pipe_owner_init(&op->owner);
    if (!raya_pipe_finalizer_init(&op->finalizer, pipe, event_handle)) {
        memset(op, 0, sizeof(*op));
        return 0;
    }
    op->operation_initialized=TRUE;
    return 1;
}

static HANDLE WINAPI default_thread_factory(LPTHREAD_START_ROUTINE entry,
    LPVOID parameter, void *context) {
    (void)context;
    return CreateThread(NULL, 0, entry, parameter, 0, NULL);
}

static BOOL WINAPI default_get_overlapped_result(HANDLE file,
    LPOVERLAPPED overlapped, LPDWORD transferred, BOOL wait, void *context) {
    (void)context;
    return GetOverlappedResult(file, overlapped, transferred, wait);
}
#ifdef RAYA_ADAPTER_TESTING
static BOOL WINAPI default_set_wake_event(HANDLE event_handle, void *context) {
    (void)context;
    return SetEvent(event_handle);
}
static DWORD WINAPI default_lifecycle_wait(HANDLE handle, DWORD timeout_ms, void *context) {
    (void)context;
    return WaitForSingleObject(handle, timeout_ms);
}
static BOOL WINAPI default_lifecycle_exit_code(HANDLE thread, LPDWORD exit_code, void *context) {
    (void)context;
    return GetExitCodeThread(thread, exit_code);
}
static BOOL WINAPI default_lifecycle_close(HANDLE handle, void *context) {
    (void)context;
    return CloseHandle(handle);
}
#endif

static BOOL signal_wake_locked(raya_managed_worker *w) {
#ifdef RAYA_ADAPTER_TESTING
    return w->set_wake_event(w->wake_event, w->wake_event_context);
#else
    return SetEvent(w->wake_event);
#endif
}
static int managed_worker_start_internal(raya_managed_worker *w,
    raya_managed_thread_factory_fn factory, void *factory_context,
    raya_managed_get_overlapped_result_fn get_result, void *completion_context
#ifdef RAYA_ADAPTER_TESTING
    , raya_managed_wait_failure_test_fn wait_failure_test, void *wait_failure_test_context
#endif
    ) {
    if (w == NULL || factory == NULL || get_result == NULL) return 0;
    memset(w, 0, sizeof(*w));
    if (!InitializeCriticalSectionEx(&w->lock, 0, 0)) return 0;
    w->wake_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (w->wake_event == NULL) {
        DeleteCriticalSection(&w->lock);
        return 0;
    }
    if (!raya_reaper_integrated_init(&w->integrated)) {
        CloseHandle(w->wake_event);
        w->wake_event = NULL;
        DeleteCriticalSection(&w->lock);
        return 0;
    }
    if (!worker_allocate_epoch(&w->worker_epoch)) {
        CloseHandle(w->wake_event);
        w->wake_event = NULL;
        DeleteCriticalSection(&w->lock);
        memset(w,0,sizeof(*w));
        return 0;
    }
    w->close_handle = close_owned_handle;
    w->close_context = NULL;
    w->get_overlapped_result = get_result;
    w->completion_context = completion_context;
#ifdef RAYA_ADAPTER_TESTING
    w->set_wake_event = default_set_wake_event;
    w->wake_event_context = NULL;
    w->wait_failure_test = wait_failure_test;
    w->wait_failure_test_context = wait_failure_test_context;
    w->lifecycle_wait = default_lifecycle_wait;
    w->lifecycle_exit_code = default_lifecycle_exit_code;
    w->lifecycle_close = default_lifecycle_close;
    w->lifecycle_api_context = NULL;
#endif
    w->initialized = TRUE;
    w->thread = factory(managed_worker_main, w, factory_context);
    if (w->thread == NULL) {
        DWORD create_error = GetLastError();
        w->thread_exit_code = create_error;
        if (CloseHandle(w->wake_event)) {
            w->wake_event = NULL;
            DeleteCriticalSection(&w->lock);
            memset(w, 0, sizeof(*w));
        } else {
            EnterCriticalSection(&w->lock);
            mark_cause_locked(w, RAYA_RECOVERY_CAUSE_HANDLE_CLOSE);
            LeaveCriticalSection(&w->lock);
        }
        return 0;
    }
    w->started = TRUE;
    return 1;
}

int raya_managed_worker_register_operation(raya_managed_worker *w,
    raya_managed_operation *op, raya_worker_inventory_kind kind,
    ULONGLONG adapter_instance_id, ULONGLONG adapter_operation_id) {
    int ok=0;
    if (!w || !op || !w->initialized) return 0;
    if (kind!=RAYA_WORKER_INVENTORY_DIRECT &&
        kind!=RAYA_WORKER_INVENTORY_ADAPTER_ASSOCIATED) return 0;
    if (kind==RAYA_WORKER_INVENTORY_DIRECT &&
        (adapter_instance_id!=0 || adapter_operation_id!=0)) return 0;
    if (kind==RAYA_WORKER_INVENTORY_ADAPTER_ASSOCIATED &&
        (adapter_instance_id==0 || adapter_operation_id==0)) return 0;
    EnterCriticalSection(&w->lock);
    if (w->initialized && w->worker_epoch!=0 && op->operation_initialized &&
        !op->identity_valid &&
        op->worker_epoch==0 && op->worker_operation_id==0 &&
        w->next_operation_id!=(ULONGLONG)~(ULONGLONG)0) {
        ++w->next_operation_id;
        if (w->next_operation_id!=0) {
            op->worker_epoch=w->worker_epoch;
            op->worker_operation_id=w->next_operation_id;
            op->inventory_kind=kind;
            op->adapter_identity_valid=(kind==RAYA_WORKER_INVENTORY_ADAPTER_ASSOCIATED);
            op->adapter_instance_id=adapter_instance_id;
            op->adapter_operation_id=adapter_operation_id;
            op->identity_valid=TRUE;
            ok=1;
        }
    }
    LeaveCriticalSection(&w->lock);
    return ok;
}

int raya_managed_worker_publish_retained_locked(raya_managed_worker *w,
    raya_managed_operation *op, DWORD cause, raya_worker_retain_stage stage) {
    raya_managed_operation *item;
    if (!w || !op || !w->initialized) return 0;
    for (item=w->retained_head;item;item=item->retained_next)
        if (item==op) return 0;
    if (cause==0 || stage<RAYA_WORKER_RETAIN_STAGE_DIRECT ||
        stage>RAYA_WORKER_RETAIN_STAGE_POST_TERMINAL) return 0;
    if (op->identity_valid) {
        if (!op->operation_initialized || op->worker_epoch==0 ||
            op->worker_epoch!=w->worker_epoch ||
            op->worker_operation_id==0 ||
            (op->inventory_kind!=RAYA_WORKER_INVENTORY_DIRECT &&
             op->inventory_kind!=RAYA_WORKER_INVENTORY_ADAPTER_ASSOCIATED) ||
            (op->inventory_kind==RAYA_WORKER_INVENTORY_DIRECT &&
             (op->adapter_identity_valid || op->adapter_instance_id!=0 ||
              op->adapter_operation_id!=0 ||
              (stage!=RAYA_WORKER_RETAIN_STAGE_DIRECT &&
               stage!=RAYA_WORKER_RETAIN_STAGE_POST_TERMINAL))) ||
            (op->inventory_kind==RAYA_WORKER_INVENTORY_ADAPTER_ASSOCIATED &&
             (!op->adapter_identity_valid || op->adapter_instance_id==0 ||
              op->adapter_operation_id==0 ||
              (stage!=RAYA_WORKER_RETAIN_STAGE_ADAPTER_ASSOCIATED &&
               stage!=RAYA_WORKER_RETAIN_STAGE_POST_TERMINAL)))) return 0;
    } else if (op->worker_epoch!=0 || op->worker_operation_id!=0 ||
               op->inventory_kind!=RAYA_WORKER_INVENTORY_UNSPECIFIED ||
               op->adapter_identity_valid || op->adapter_instance_id!=0 ||
               op->adapter_operation_id!=0 ||
               (stage!=RAYA_WORKER_RETAIN_STAGE_PARTIAL_RECORD &&
                stage!=RAYA_WORKER_RETAIN_STAGE_POST_TERMINAL)) {
        return 0;
    }
    if (op->first_retain_stage==RAYA_WORKER_RETAIN_STAGE_UNKNOWN) {
        op->first_retain_cause=cause;
        op->first_retain_stage=stage;
    } else if (op->first_retain_cause==0) {
        return 0;
    }
    op->retained_next=w->retained_head;
    w->retained_head=op;
    ++w->retained_count;
    return 1;
}

raya_retained_inventory_status raya_managed_worker_copy_retained_inventory_v1(
    raya_managed_worker *w, DWORD requested_version,
    raya_worker_retained_record_v1 *records, size_t capacity,
    size_t *required_count) {
    raya_managed_operation *item,*slow,*fast;
    size_t count=0,index=0;
    if (required_count==NULL) return RAYA_RETAINED_INVENTORY_INVALID_ARGUMENT;
    *required_count=0;
    if (w==NULL || (records==NULL && capacity!=0))
        return RAYA_RETAINED_INVENTORY_INVALID_ARGUMENT;
    if (requested_version!=RAYA_WORKER_RETAINED_RECORD_VERSION_1)
        return RAYA_RETAINED_INVENTORY_UNSUPPORTED_VERSION;
    if (!w->initialized) return RAYA_RETAINED_INVENTORY_NOT_INITIALIZED;

    EnterCriticalSection(&w->lock);
    if (!w->initialized) {
        LeaveCriticalSection(&w->lock);
        return RAYA_RETAINED_INVENTORY_NOT_INITIALIZED;
    }
    if (!w->joined || w->thread!=NULL || w->integrated.core.caller_active!=0) {
        LeaveCriticalSection(&w->lock);
        return RAYA_RETAINED_INVENTORY_NOT_QUIESCENT;
    }
    slow=w->retained_head;
    fast=w->retained_head;
    while (fast!=NULL && fast->retained_next!=NULL) {
        slow=slow->retained_next;
        fast=fast->retained_next->retained_next;
        if (slow==fast) {
            LeaveCriticalSection(&w->lock);
            return RAYA_RETAINED_INVENTORY_INCONSISTENT;
        }
    }
    for (item=w->retained_head;item!=NULL;item=item->retained_next) {
        if (count==(size_t)-1) {
            LeaveCriticalSection(&w->lock);
            return RAYA_RETAINED_INVENTORY_INCONSISTENT;
        }
        ++count;
    }
    if (count!=w->retained_count) {
        LeaveCriticalSection(&w->lock);
        return RAYA_RETAINED_INVENTORY_INCONSISTENT;
    }
    *required_count=count;
    if (capacity<count) {
        LeaveCriticalSection(&w->lock);
        return RAYA_RETAINED_INVENTORY_INSUFFICIENT_CAPACITY;
    }
    for (item=w->retained_head;item!=NULL;item=item->retained_next) {
        int valid_stage=(item->first_retain_stage>=RAYA_WORKER_RETAIN_STAGE_DIRECT &&
            item->first_retain_stage<=RAYA_WORKER_RETAIN_STAGE_POST_TERMINAL);
        int valid_shape=0;
        if (item->first_retain_cause!=0 && valid_stage) {
            if (item->identity_valid && item->operation_initialized &&
                item->worker_epoch!=0 && item->worker_epoch==w->worker_epoch &&
                item->worker_operation_id!=0) {
                if (item->inventory_kind==RAYA_WORKER_INVENTORY_DIRECT) {
                    valid_shape=!item->adapter_identity_valid &&
                        item->adapter_instance_id==0 && item->adapter_operation_id==0 &&
                        (item->first_retain_stage==RAYA_WORKER_RETAIN_STAGE_DIRECT ||
                         item->first_retain_stage==RAYA_WORKER_RETAIN_STAGE_POST_TERMINAL);
                } else if (item->inventory_kind==RAYA_WORKER_INVENTORY_ADAPTER_ASSOCIATED) {
                    valid_shape=item->adapter_identity_valid &&
                        item->adapter_instance_id!=0 && item->adapter_operation_id!=0 &&
                        (item->first_retain_stage==RAYA_WORKER_RETAIN_STAGE_ADAPTER_ASSOCIATED ||
                         item->first_retain_stage==RAYA_WORKER_RETAIN_STAGE_POST_TERMINAL);
                }
            } else if (!item->identity_valid && item->worker_epoch==0 &&
                item->worker_operation_id==0 &&
                item->inventory_kind==RAYA_WORKER_INVENTORY_UNSPECIFIED &&
                !item->adapter_identity_valid && item->adapter_instance_id==0 &&
                item->adapter_operation_id==0 &&
                (item->first_retain_stage==RAYA_WORKER_RETAIN_STAGE_PARTIAL_RECORD ||
                 item->first_retain_stage==RAYA_WORKER_RETAIN_STAGE_POST_TERMINAL)) {
                valid_shape=1;
            }
        }
        if (!valid_shape) {
            *required_count=0;
            LeaveCriticalSection(&w->lock);
            return RAYA_RETAINED_INVENTORY_INCONSISTENT;
        }
    }
    for (item=w->retained_head;item!=NULL;item=item->retained_next) {
        records[index].struct_size=(DWORD)sizeof(records[index]);
        records[index].version=RAYA_WORKER_RETAINED_RECORD_VERSION_1;
        records[index].identity_valid=item->identity_valid;
        records[index].inventory_kind=item->inventory_kind;
        records[index].worker_epoch=item->worker_epoch;
        records[index].worker_operation_id=item->worker_operation_id;
        records[index].adapter_identity_valid=item->adapter_identity_valid;
        records[index].adapter_instance_id=item->adapter_instance_id;
        records[index].adapter_operation_id=item->adapter_operation_id;
        records[index].first_retain_cause=item->first_retain_cause;
        records[index].first_retain_stage=item->first_retain_stage;
        ++index;
    }
    LeaveCriticalSection(&w->lock);
    return RAYA_RETAINED_INVENTORY_OK;
}

int raya_managed_worker_start(raya_managed_worker *w) {
#ifdef RAYA_ADAPTER_TESTING
    return managed_worker_start_internal(w, default_thread_factory, NULL,
        default_get_overlapped_result, NULL, NULL, NULL);
#else
    return managed_worker_start_internal(w, default_thread_factory, NULL,
        default_get_overlapped_result, NULL);
#endif
}

int raya_managed_worker_start_with_completion_api(raya_managed_worker *w,
    raya_managed_get_overlapped_result_fn get_result, void *context) {
#ifdef RAYA_ADAPTER_TESTING
    return managed_worker_start_internal(w, default_thread_factory, NULL,
        get_result, context, NULL, NULL);
#else
    return managed_worker_start_internal(w, default_thread_factory, NULL,
        get_result, context);
#endif
}

int raya_managed_worker_start_with_factory(raya_managed_worker *w,
    raya_managed_thread_factory_fn factory, void *context) {
#ifdef RAYA_ADAPTER_TESTING
    return managed_worker_start_internal(w, factory, context,
        default_get_overlapped_result, NULL, NULL, NULL);
#else
    return managed_worker_start_internal(w, factory, context,
        default_get_overlapped_result, NULL);
#endif
}

#ifdef RAYA_ADAPTER_TESTING
int raya_managed_worker_start_with_wait_failure_test_hook(raya_managed_worker *w,
    raya_managed_wait_failure_test_fn wait_failure_test, void *context) {
    if (wait_failure_test == NULL) return 0;
    return managed_worker_start_internal(w, default_thread_factory, NULL,
        default_get_overlapped_result, NULL, wait_failure_test, context);
}

int raya_managed_worker_set_wake_callback(raya_managed_worker *w,
    raya_managed_set_event_fn set_event, void *context) {
    int ok = 0;
    if (w == NULL || !w->initialized || set_event == NULL) return 0;
    EnterCriticalSection(&w->lock);
    if (w->integrated.core.lifecycle == RAYA_REAPER_ACCEPTING &&
        w->integrated.core.caller_active == 0 &&
        w->integrated.core.outstanding == 0 &&
        w->integrated.reserved_slots == 0 &&
        w->integrated.registry.count == 0 &&
        w->integrated.core.queued == 0) {
        w->set_wake_event = set_event;
        w->wake_event_context = context;
        ok = 1;
    }
    LeaveCriticalSection(&w->lock);
    return ok;
}
#endif

#ifdef RAYA_ADAPTER_TESTING
int raya_managed_worker_set_before_reserve_test_hook(raya_managed_worker *w,
    raya_managed_before_reserve_test_fn hook, void *context) {
    int ok=0;
    if (w==NULL || !w->initialized) return 0;
    EnterCriticalSection(&w->lock);
    if (w->integrated.core.lifecycle==RAYA_REAPER_ACCEPTING &&
        w->integrated.core.caller_active==0 && w->integrated.core.outstanding==0 &&
        w->integrated.reserved_slots==0 && w->integrated.registry.count==0) {
        w->before_reserve_test=hook; w->before_reserve_test_context=context; ok=1;
    }
    LeaveCriticalSection(&w->lock);
    return ok;
}

int raya_managed_worker_set_lifecycle_api(raya_managed_worker *w,
    raya_managed_wait_fn wait_fn, raya_managed_exit_code_fn exit_code_fn,
    raya_managed_lifecycle_close_fn close_fn, void *context) {
    int ok = 0;
    if (w == NULL || !w->initialized || !w->started ||
        wait_fn == NULL || exit_code_fn == NULL || close_fn == NULL) return 0;
    EnterCriticalSection(&w->lock);
    if (w->integrated.core.lifecycle == RAYA_REAPER_ACCEPTING &&
        w->integrated.core.caller_active == 0 &&
        w->integrated.core.outstanding == 0 &&
        w->integrated.core.queued == 0 &&
        w->integrated.reserved_slots == 0 &&
        w->integrated.registry.count == 0 &&
        w->retained_count == 0 && w->retained_head == NULL) {
        w->lifecycle_wait = wait_fn;
        w->lifecycle_exit_code = exit_code_fn;
        w->lifecycle_close = close_fn;
        w->lifecycle_api_context = context;
        ok = 1;
    }
    LeaveCriticalSection(&w->lock);
    return ok;
}
#endif

int raya_managed_worker_set_close_callback(raya_managed_worker *w,
    raya_close_handle_fn close_fn, void *context) {
    int ok = 0;
    if (w == NULL || !w->initialized || close_fn == NULL) return 0;
    EnterCriticalSection(&w->lock);
    if (w->integrated.core.lifecycle == RAYA_REAPER_ACCEPTING &&
        w->integrated.core.outstanding == 0 &&
        w->integrated.reserved_slots == 0 &&
        w->integrated.registry.count == 0) {
        w->close_handle = close_fn;
        w->close_context = context;
        ok = 1;
    }
    LeaveCriticalSection(&w->lock);
    return ok;
}

int raya_managed_worker_admit(raya_managed_worker *w) {
    int ok;
    if (w == NULL || !w->initialized || !w->started) return 0;
    EnterCriticalSection(&w->lock);
    ok = raya_reaper_integrated_admit(&w->integrated);
    LeaveCriticalSection(&w->lock);
    return ok;
}

int raya_managed_worker_finish(raya_managed_worker *w) {
    int ok;
    if (w == NULL || !w->initialized) return 0;
    EnterCriticalSection(&w->lock);
    ok = raya_reaper_caller_finish(&w->integrated.core);
    if (!ok) mark_cause_locked(w, RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
    else if (w->integrated.core.lifecycle == RAYA_REAPER_STOPPING &&
        raya_reaper_integrated_can_exit(&w->integrated) && !signal_wake_locked(w)) {
        mark_cause_locked(w, RAYA_RECOVERY_CAUSE_WAKE_SIGNAL);
        ok = 0;
    }
    LeaveCriticalSection(&w->lock);
    return ok;
}

int raya_managed_worker_reserve(raya_managed_worker *w,
    raya_managed_operation *op) {
    int ok;
    if (w == NULL || op == NULL || !w->initialized) return 0;
#ifdef RAYA_ADAPTER_TESTING
    if (w->before_reserve_test != NULL)
        w->before_reserve_test(w,op,w->before_reserve_test_context);
#endif
    EnterCriticalSection(&w->lock);
    ok = raya_reaper_integrated_reserve(&w->integrated, &op->integrated);
    LeaveCriticalSection(&w->lock);
    return ok;
}

static raya_managed_handoff_result managed_worker_handoff_internal(
    raya_managed_worker *w, raya_managed_operation *op
#ifdef RAYA_ADAPTER_TESTING
    , raya_managed_handoff_degraded_fn on_degraded, void *context
#endif
    ) {
    raya_managed_handoff_result result = RAYA_MANAGED_HANDOFF_NOT_TRANSFERRED;
    if (w == NULL || op == NULL || !w->initialized) return result;
    EnterCriticalSection(&w->lock);
    if (raya_reaper_integrated_handoff(&w->integrated, &op->integrated)) {
        if (!raya_pipe_owner_handoff(&op->owner)) {
            mark_cause_locked(w, RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
            result = RAYA_MANAGED_HANDOFF_TRANSFERRED_DEGRADED;
        } else if (!signal_wake_locked(w)) {
            mark_cause_locked(w, RAYA_RECOVERY_CAUSE_WAKE_SIGNAL);
            result = RAYA_MANAGED_HANDOFF_TRANSFERRED_DEGRADED;
        } else {
            result = RAYA_MANAGED_HANDOFF_TRANSFERRED;
        }
#ifdef RAYA_ADAPTER_TESTING
        /* Publish before unlocking: the Worker thread cannot pass its next
           Worker-lock boundary before the Adapter quarantine is visible. */
        if (result == RAYA_MANAGED_HANDOFF_TRANSFERRED_DEGRADED &&
            on_degraded != NULL && !on_degraded(op, context))
            mark_cause_locked(w, RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
#endif
    }
    LeaveCriticalSection(&w->lock);
    return result;
}

raya_managed_handoff_result raya_managed_worker_handoff(
    raya_managed_worker *w, raya_managed_operation *op) {
#ifdef RAYA_ADAPTER_TESTING
    return managed_worker_handoff_internal(w, op, NULL, NULL);
#else
    return managed_worker_handoff_internal(w, op);
#endif
}
#ifdef RAYA_ADAPTER_TESTING
raya_managed_handoff_result raya_managed_worker_handoff_with_hook(
    raya_managed_worker *w, raya_managed_operation *op,
    raya_managed_handoff_degraded_fn on_degraded, void *context) {
    return managed_worker_handoff_internal(w, op, on_degraded, context);
}
#endif
static void handle_wait_failure(raya_managed_worker *w) {
    EnterCriticalSection(&w->lock);
    mark_cause_locked(w, RAYA_RECOVERY_CAUSE_WAIT_FAILED);
    LeaveCriticalSection(&w->lock);
}

static DWORD WINAPI managed_worker_main(void *parameter) {
    raya_managed_worker *w = (raya_managed_worker *)parameter;
    raya_reaper_waitplan plan;
    for (;;) {
        DWORD wr;
        raya_waitplan_result resolved;
        raya_reaper_op *base = NULL;
        HANDLE event_handle = NULL;
        raya_managed_operation *op = NULL;
        int build_ok = 1;
        EnterCriticalSection(&w->lock);
        if (w->integrated.core.lifecycle == RAYA_REAPER_DEGRADED) {
            LeaveCriticalSection(&w->lock);
            return ERROR_INVALID_STATE;
        }
        if (!ResetEvent(w->wake_event)) {
            mark_cause_locked(w, RAYA_RECOVERY_CAUSE_WAITSET_INVALID);
            LeaveCriticalSection(&w->lock);
            return ERROR_INVALID_HANDLE;
        }
        for (;;) {
            raya_integrated_op *promoted = NULL;
            raya_integrated_result pr =
                raya_reaper_integrated_promote_one(&w->integrated, &promoted);
            if (pr == RAYA_INTEGRATED_EMPTY) break;
            if (pr != RAYA_INTEGRATED_OK || promoted == NULL) {
                build_ok = 0;
                break;
            }
            if (!raya_pipe_owner_reaper_take(
                    &((raya_managed_operation *)promoted)->owner)) {
                mark_cause_locked(w, RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
                build_ok = 0;
                break;
            }
        }
        if (!build_ok ||
            !raya_reaper_waitplan_build(&w->integrated.registry,
                w->wake_event, &plan)) {
            if (w->integrated.core.lifecycle != RAYA_REAPER_DEGRADED)
                mark_cause_locked(w, RAYA_RECOVERY_CAUSE_WAITSET_INVALID);
            LeaveCriticalSection(&w->lock);
            return ERROR_INVALID_DATA;
        }
        if (raya_reaper_integrated_can_exit(&w->integrated)) {
            if (!raya_reaper_mark_stopped(&w->integrated.core)) {
                mark_cause_locked(w, RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
                LeaveCriticalSection(&w->lock);
                return ERROR_INVALID_DATA;
            }
            LeaveCriticalSection(&w->lock);
            return ERROR_SUCCESS;
        }
        LeaveCriticalSection(&w->lock);

#ifdef RAYA_ADAPTER_TESTING
        if (w->wait_failure_test != NULL) {
            DWORD injected_error = ERROR_GEN_FAILURE;
            if (w->wait_failure_test(&injected_error,
                    w->wait_failure_test_context)) {
                SetLastError(injected_error);
                wr = WAIT_FAILED;
            } else {
                wr = WaitForMultipleObjects(plan.waitset.handle_count,
                    plan.waitset.handles, FALSE, INFINITE);
            }
        } else {
            /* Test-only finite wait: independently observe a missed wake signal. */
            wr = WaitForMultipleObjects(plan.waitset.handle_count,
                plan.waitset.handles, FALSE, 100);
        }
#else
        wr = WaitForMultipleObjects(plan.waitset.handle_count,
            plan.waitset.handles, FALSE, INFINITE);
#endif
        if (wr == WAIT_FAILED) {
            DWORD wait_error = GetLastError();
            handle_wait_failure(w);
            return wait_error;
        }
        EnterCriticalSection(&w->lock);
        resolved = raya_reaper_waitplan_resolve(&w->integrated.registry,
            &plan, wr, &base, &event_handle);
        if (resolved == RAYA_WAITPLAN_WAKE) {
            LeaveCriticalSection(&w->lock);
            continue;
        }
        if (resolved == RAYA_WAITPLAN_TIMEOUT) {
            if (w->integrated.core.lifecycle == RAYA_REAPER_DEGRADED) {
                LeaveCriticalSection(&w->lock);
                return ERROR_INVALID_STATE;
            }
            LeaveCriticalSection(&w->lock);
            continue;
        }
        if (resolved != RAYA_WAITPLAN_OPERATION || base == NULL ||
            event_handle == NULL) {
            mark_cause_locked(w, RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
            LeaveCriticalSection(&w->lock);
            return ERROR_INVALID_DATA;
        }
        op = (raya_managed_operation *)base;
        if (!raya_reaper_integrated_claim(&w->integrated,
                &op->integrated, event_handle)) {
            mark_cause_locked(w, RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
            LeaveCriticalSection(&w->lock);
            return ERROR_INVALID_DATA;
        }
        LeaveCriticalSection(&w->lock);

        if (WaitForSingleObject(event_handle, 0) != WAIT_OBJECT_0) {
            (void)raya_pipe_owner_reaper_collect(&op->owner,
                FALSE, TRUE, FALSE, ERROR_INVALID_HANDLE);
            EnterCriticalSection(&w->lock);
            (void)raya_reaper_integrated_terminal_hold(&w->integrated,
                &op->integrated, RAYA_INTEGRATED_UNCERTAIN_RESULT);
            LeaveCriticalSection(&w->lock);
            return ERROR_INVALID_DATA;
        }
        {
            BOOL api_ok = w->get_overlapped_result(op->issuing_pipe,
                &op->overlapped, &op->transferred, FALSE,
                w->completion_context);
            DWORD error = api_ok ? ERROR_SUCCESS : GetLastError();
            raya_pipe_completion_class cls = raya_classify_pipe_completion(
                TRUE, op->overlapped.hEvent == event_handle, api_ok, error);
            raya_integrated_collect_result cr;
            if (cls == RAYA_PIPE_COMPLETION_INCOMPLETE) {
                DWORD delay_ms;
                cr = RAYA_INTEGRATED_INCOMPLETE;
                if (!raya_pipe_owner_reaper_collect(&op->owner,
                    TRUE, TRUE, FALSE, error)) {
                    EnterCriticalSection(&w->lock);
                    mark_cause_locked(w, RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
                    LeaveCriticalSection(&w->lock);
                    return ERROR_INVALID_DATA;
                }
                if (op->incomplete_retries < 6) ++op->incomplete_retries;
                (void)InterlockedIncrement(&op->incomplete_observations);
                delay_ms = 1U << (op->incomplete_retries - 1U);
                EnterCriticalSection(&w->lock);
                if (raya_reaper_integrated_collect(&w->integrated,
                    &op->integrated, cr) != RAYA_INTEGRATED_OK) {
                    mark_cause_locked(w, RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
                    LeaveCriticalSection(&w->lock);
                    return ERROR_INVALID_DATA;
                }
                LeaveCriticalSection(&w->lock);
                Sleep(delay_ms);
                continue;
            }
            if (cls == RAYA_PIPE_COMPLETION_UNCERTAIN) {
                (void)raya_pipe_owner_reaper_collect(&op->owner,
                    TRUE, TRUE, FALSE, error);
                EnterCriticalSection(&w->lock);
                (void)raya_reaper_integrated_terminal_hold(&w->integrated,
                    &op->integrated, RAYA_INTEGRATED_UNCERTAIN_RESULT);
                LeaveCriticalSection(&w->lock);
                return error == ERROR_SUCCESS ? ERROR_INVALID_DATA : error;
            }
            op->incomplete_retries = 0;
            op->completion_error = api_ok ? ERROR_SUCCESS : error;
            cr = cls == RAYA_PIPE_COMPLETION_TERMINAL_ABORTED ?
                RAYA_INTEGRATED_TERMINAL_ABORTED : RAYA_INTEGRATED_TERMINAL_SUCCESS;
            if (!raya_pipe_owner_reaper_collect(&op->owner,
                    TRUE, TRUE, api_ok, error)) {
                EnterCriticalSection(&w->lock);
                mark_cause_locked(w, RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
                LeaveCriticalSection(&w->lock);
                return ERROR_INVALID_DATA;
            }
            EnterCriticalSection(&w->lock);
            if (raya_reaper_integrated_terminal_hold(&w->integrated,
                    &op->integrated, cr) != RAYA_INTEGRATED_OK) {
                mark_cause_locked(w, RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
                LeaveCriticalSection(&w->lock);
                return ERROR_INVALID_DATA;
            }
            LeaveCriticalSection(&w->lock);
            if (raya_pipe_finalizer_close_handles(&op->finalizer,
                    w->close_handle, w->close_context) != RAYA_FINALIZER_OK ||
                !raya_pipe_finalizer_commit_release(&op->finalizer)) {
                EnterCriticalSection(&w->lock);
                mark_cause_locked(w, RAYA_RECOVERY_CAUSE_HANDLE_CLOSE);
                LeaveCriticalSection(&w->lock);
                return ERROR_INVALID_HANDLE;
            }
            if (!raya_pipe_owner_release(&op->owner)) {
                EnterCriticalSection(&w->lock);
                mark_cause_locked(w, RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
                LeaveCriticalSection(&w->lock);
                return ERROR_INVALID_DATA;
            }
            EnterCriticalSection(&w->lock);
            if (!raya_reaper_integrated_release(&w->integrated,
                    &op->integrated)) {
                mark_cause_locked(w, RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
                LeaveCriticalSection(&w->lock);
                return ERROR_INVALID_DATA;
            }
            LeaveCriticalSection(&w->lock);
            {
                raya_managed_release_fn release = op->release;
                void *context = op->release_context;
                release(op, context);
            }
        }
    }
}

static void worker_fill_result(raya_managed_worker *w, raya_worker_op_result *r,
    raya_worker_op_status status, raya_worker_effect effect,
    raya_worker_retry_class retry, DWORD step, DWORD error) {
    if (r==NULL) return;
    memset(r,0,sizeof(*r));
    r->struct_size=(DWORD)sizeof(*r); r->status=status; r->effect=effect;
    r->retry=retry; r->step=step; r->win32_error=error;
    if (w!=NULL) {
        r->thread_exit_code=w->thread_exit_code;
        r->wake_event_present=w->wake_event!=NULL;
        r->thread_present=w->thread!=NULL;
    }
}
void raya_worker_result_init(raya_worker_op_result *r) {
    if (r!=NULL) {
        memset(r,0,sizeof(*r)); r->struct_size=(DWORD)sizeof(*r);
        r->status=RAYA_WORKER_OP_INVALID_STATE;
    }
}
raya_worker_op_status raya_managed_worker_shutdown_ex(
    raya_managed_worker *w, raya_worker_op_result *r) {
    int ok=0, stopped=0, stopping=0, degraded=0, stopping_effect=0; DWORD error=ERROR_SUCCESS;
    if (w==NULL || !w->initialized) {
        worker_fill_result(w,r,RAYA_WORKER_OP_INVALID_STATE,RAYA_WORKER_EFFECT_NONE,
            RAYA_WORKER_RETRY_NONE,1,ERROR_INVALID_STATE);
        return RAYA_WORKER_OP_INVALID_STATE;
    }
    EnterCriticalSection(&w->lock);
    if (w->integrated.core.lifecycle==RAYA_REAPER_ACCEPTING)
        ok=raya_reaper_integrated_shutdown(&w->integrated);
    else if (w->integrated.core.lifecycle==RAYA_REAPER_STOPPING ||
             w->integrated.core.lifecycle==RAYA_REAPER_STOPPED) ok=1;
    if (w->integrated.core.lifecycle==RAYA_REAPER_STOPPING) stopping_effect=1;
    if (ok && w->integrated.core.lifecycle==RAYA_REAPER_STOPPING &&
        !signal_wake_locked(w)) {
        error=GetLastError();
        mark_cause_locked(w,RAYA_RECOVERY_CAUSE_WAKE_SIGNAL);
        ok=0;
    }
    stopped=(w->integrated.core.lifecycle==RAYA_REAPER_STOPPED);
    stopping=(w->integrated.core.lifecycle==RAYA_REAPER_STOPPING);
    degraded=(w->integrated.core.lifecycle==RAYA_REAPER_DEGRADED ||
              w->integrated.core.lifecycle==RAYA_REAPER_FAILED);
    LeaveCriticalSection(&w->lock);
    if (ok) {
        worker_fill_result(w,r,stopped?RAYA_WORKER_OP_ALREADY_DONE:RAYA_WORKER_OP_SUCCESS,
            stopped?RAYA_WORKER_EFFECT_NONE:RAYA_WORKER_EFFECT_SHUTDOWN_SIGNALLED,
            RAYA_WORKER_RETRY_NONE,0,ERROR_SUCCESS);
        return stopped?RAYA_WORKER_OP_ALREADY_DONE:RAYA_WORKER_OP_SUCCESS;
    }
    if (degraded || (!stopping && !stopped)) {
        worker_fill_result(w,r,RAYA_WORKER_OP_TERMINAL_FAILURE,
            stopping_effect?RAYA_WORKER_EFFECT_STOPPING:RAYA_WORKER_EFFECT_NONE,
            RAYA_WORKER_RETRY_RECONCILE,2,error?error:ERROR_INVALID_STATE);
        return RAYA_WORKER_OP_TERMINAL_FAILURE;
    }
    worker_fill_result(w,r,RAYA_WORKER_OP_RETRYABLE_FAILURE,
        stopping_effect?RAYA_WORKER_EFFECT_STOPPING:RAYA_WORKER_EFFECT_NONE,
        RAYA_WORKER_RETRY_SAME_OPERATION,2,
        error?error:ERROR_INVALID_STATE);
    return RAYA_WORKER_OP_RETRYABLE_FAILURE;
}
raya_worker_op_status raya_managed_worker_join_ex(
    raya_managed_worker *w, DWORD timeout_ms, raya_worker_op_result *r) {
    DWORD wr,code=0,error=ERROR_SUCCESS;
    if (w==NULL || !w->initialized || !w->started) {
        worker_fill_result(w,r,RAYA_WORKER_OP_INVALID_STATE,RAYA_WORKER_EFFECT_NONE,
            RAYA_WORKER_RETRY_NONE,1,ERROR_INVALID_STATE);
        return RAYA_WORKER_OP_INVALID_STATE;
    }
    if (w->joined) {
        raya_worker_op_status st=w->thread_exit_code==ERROR_SUCCESS?
            RAYA_WORKER_OP_ALREADY_DONE:RAYA_WORKER_OP_TERMINAL_FAILURE;
        worker_fill_result(w,r,st,RAYA_WORKER_EFFECT_THREAD_REAPED,
            RAYA_WORKER_RETRY_NONE,0,w->thread_exit_code==ERROR_SUCCESS?
            ERROR_SUCCESS:w->thread_exit_code);
        return st;
    }
#ifdef RAYA_ADAPTER_TESTING
    wr=w->lifecycle_wait(w->thread,timeout_ms,w->lifecycle_api_context);
#else
    wr=WaitForSingleObject(w->thread,timeout_ms);
#endif
    if (wr!=WAIT_OBJECT_0) {
        error=(wr==WAIT_TIMEOUT)?ERROR_TIMEOUT:GetLastError();
        worker_fill_result(w,r,RAYA_WORKER_OP_RETRYABLE_FAILURE,RAYA_WORKER_EFFECT_NONE,
            RAYA_WORKER_RETRY_SAME_OPERATION,2,error);
        return RAYA_WORKER_OP_RETRYABLE_FAILURE;
    }
#ifdef RAYA_ADAPTER_TESTING
    if (!w->lifecycle_exit_code(w->thread,&code,w->lifecycle_api_context)) {
#else
    if (!GetExitCodeThread(w->thread,&code)) {
#endif
        error=GetLastError();
        worker_fill_result(w,r,RAYA_WORKER_OP_RETRYABLE_FAILURE,RAYA_WORKER_EFFECT_NONE,
            RAYA_WORKER_RETRY_SAME_OPERATION,3,error);
        return RAYA_WORKER_OP_RETRYABLE_FAILURE;
    }
    w->thread_exit_code=code;
#ifdef RAYA_ADAPTER_TESTING
    if (!w->lifecycle_close(w->thread,w->lifecycle_api_context)) {
#else
    if (!CloseHandle(w->thread)) {
#endif
        error=GetLastError();
        worker_fill_result(w,r,RAYA_WORKER_OP_RETRYABLE_FAILURE,RAYA_WORKER_EFFECT_NONE,
            RAYA_WORKER_RETRY_SAME_OPERATION,4,error);
        return RAYA_WORKER_OP_RETRYABLE_FAILURE;
    }
    w->thread=NULL; w->joined=TRUE;
    if (code==ERROR_SUCCESS) {
        worker_fill_result(w,r,RAYA_WORKER_OP_SUCCESS,RAYA_WORKER_EFFECT_THREAD_REAPED,
            RAYA_WORKER_RETRY_NONE,0,ERROR_SUCCESS);
        return RAYA_WORKER_OP_SUCCESS;
    }
    worker_fill_result(w,r,RAYA_WORKER_OP_TERMINAL_FAILURE,RAYA_WORKER_EFFECT_THREAD_REAPED,
        RAYA_WORKER_RETRY_NONE,5,code);
    return RAYA_WORKER_OP_TERMINAL_FAILURE;
}
raya_worker_op_status raya_managed_worker_destroy_ex(
    raya_managed_worker *w, raya_worker_op_result *r) {
    int safe; DWORD error=ERROR_SUCCESS;
    if (w==NULL || !w->initialized || !w->joined) {
        worker_fill_result(w,r,RAYA_WORKER_OP_INVALID_STATE,RAYA_WORKER_EFFECT_NONE,
            RAYA_WORKER_RETRY_NONE,1,ERROR_INVALID_STATE);
        return RAYA_WORKER_OP_INVALID_STATE;
    }
    EnterCriticalSection(&w->lock);
    safe=w->integrated.core.lifecycle==RAYA_REAPER_STOPPED &&
        w->integrated.core.caller_active==0 && w->integrated.core.outstanding==0 &&
        w->integrated.core.queued==0 && w->integrated.core.head==NULL &&
        w->integrated.core.tail==NULL && w->integrated.reserved_slots==0 &&
        w->integrated.registry.count==0 && w->retained_count==0 && w->retained_head==NULL;
    LeaveCriticalSection(&w->lock);
    if (!safe) {
        worker_fill_result(w,r,RAYA_WORKER_OP_INVALID_STATE,RAYA_WORKER_EFFECT_NONE,
            RAYA_WORKER_RETRY_RECONCILE,2,ERROR_BUSY);
        return RAYA_WORKER_OP_INVALID_STATE;
    }
#ifdef RAYA_ADAPTER_TESTING
    if (!w->lifecycle_close(w->wake_event,w->lifecycle_api_context)) {
#else
    if (!CloseHandle(w->wake_event)) {
#endif
        error=GetLastError();
        worker_fill_result(w,r,RAYA_WORKER_OP_RETRYABLE_FAILURE,RAYA_WORKER_EFFECT_NONE,
            RAYA_WORKER_RETRY_SAME_OPERATION,3,error);
        return RAYA_WORKER_OP_RETRYABLE_FAILURE;
    }
    w->wake_event=NULL; DeleteCriticalSection(&w->lock); w->initialized=FALSE;
    worker_fill_result(w,r,RAYA_WORKER_OP_SUCCESS,RAYA_WORKER_EFFECT_DESTROYED,
        RAYA_WORKER_RETRY_NONE,0,ERROR_SUCCESS);
    return RAYA_WORKER_OP_SUCCESS;
}
int raya_managed_worker_shutdown(raya_managed_worker *w) {
    raya_worker_op_result r;
    raya_worker_op_status st=raya_managed_worker_shutdown_ex(w,&r);
    return st==RAYA_WORKER_OP_SUCCESS || st==RAYA_WORKER_OP_ALREADY_DONE;
}
int raya_managed_worker_join(raya_managed_worker *w,DWORD timeout_ms) {
    raya_worker_op_result r;
    raya_worker_op_status st=raya_managed_worker_join_ex(w,timeout_ms,&r);
    return st==RAYA_WORKER_OP_SUCCESS || st==RAYA_WORKER_OP_ALREADY_DONE;
}
int raya_managed_worker_destroy(raya_managed_worker *w) {
    raya_worker_op_result r;
    return raya_managed_worker_destroy_ex(w,&r)==RAYA_WORKER_OP_SUCCESS;
}
