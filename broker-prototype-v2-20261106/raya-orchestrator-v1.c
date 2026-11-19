#include "raya-orchestrator-v1.h"
#include <string.h>

void raya_orchestrator_context_init(raya_orchestrator_context *context) {
    if (context!=NULL) memset(context,0,sizeof(*context));
}

raya_orch_status raya_orchestrator_context_register(
    raya_orch_registry *registry,
    raya_orchestrator_context *context,
    raya_integrated_submit_adapter *adapter,
    raya_dedicated_submitter *submitter,
    raya_managed_worker *worker,
    raya_orch_worker_ownership worker_ownership,
    raya_orch_handle *handle_out) {
    raya_orch_handle handle=0;
    raya_orch_status status;
    if (registry==NULL || context==NULL || adapter==NULL || submitter==NULL ||
        worker==NULL || handle_out==NULL ||
        (worker_ownership!=RAYA_ORCH_WORKER_BORROWED &&
         worker_ownership!=RAYA_ORCH_WORKER_OWNED))
        return RAYA_ORCH_INVALID;
    *handle_out=0;
    if (context->registered || context->magic==RAYA_ORCHESTRATOR_CONTEXT_MAGIC)
        return RAYA_ORCH_INVALID;
    memset(context,0,sizeof(*context));
    context->magic=RAYA_ORCHESTRATOR_CONTEXT_MAGIC;
    context->registry=registry;
    context->adapter=adapter;
    context->submitter=submitter;
    context->worker=worker;
    context->worker_ownership=worker_ownership;
    status=raya_orch_register_with_policy(registry,context,
        RAYA_ORCHESTRATOR_CONTEXT_KIND,
        RAYA_ORCH_CLOSE_POLICY_REQUIRED_STEPS,
        (1u<<5)-1u | (worker_ownership==RAYA_ORCH_WORKER_OWNED ?
            (((1u<<RAYA_ORCH_CLEANUP_STEPS)-1u) & ~((1u<<5)-1u)) : 0u),
        &handle);
    if (status!=RAYA_ORCH_OK) {
        memset(context,0,sizeof(*context));
        return status;
    }
    context->handle=handle;
    context->registered=TRUE;
    *handle_out=handle;
    return RAYA_ORCH_OK;
}

raya_orch_status raya_orchestrator_acquire(
    raya_orch_registry *registry, raya_orch_handle handle,
    raya_orchestrator_lease *lease_out) {
    raya_orch_status status;
    raya_orch_lease registry_lease;
    raya_orchestrator_context *context;
    if (lease_out==NULL) return RAYA_ORCH_INVALID;
    memset(lease_out,0,sizeof(*lease_out));
    status=raya_orch_acquire(registry,handle,RAYA_ORCHESTRATOR_CONTEXT_KIND,
                             &registry_lease);
    if (status!=RAYA_ORCH_OK) return status;
    /* Dereference is deliberately after successful typed Registry acquisition. */
    context=(raya_orchestrator_context *)registry_lease.object;
    if (context==NULL || context->magic!=RAYA_ORCHESTRATOR_CONTEXT_MAGIC ||
        !context->registered || context->registry!=registry ||
        context->handle!=handle || context->adapter==NULL ||
        context->submitter==NULL || context->worker==NULL ||
        (context->worker_ownership!=RAYA_ORCH_WORKER_BORROWED &&
         context->worker_ownership!=RAYA_ORCH_WORKER_OWNED)) {
        (void)raya_orch_release(&registry_lease);
        return RAYA_ORCH_INVALID;
    }
    lease_out->registry_lease=registry_lease;
    lease_out->context=context;
    lease_out->active=TRUE;
    return RAYA_ORCH_OK;
}

raya_orch_status raya_orchestrator_release(raya_orchestrator_lease *lease) {
    raya_orch_status status;
    if (lease==NULL || !lease->active) return RAYA_ORCH_INVALID;
    status=raya_orch_release(&lease->registry_lease);
    if (status==RAYA_ORCH_OK) {
        lease->context=NULL;
        lease->active=FALSE;
    }
    return status;
}

raya_orch_status raya_orchestrator_owner_get_context(
    raya_orch_registry *registry, raya_orch_handle handle,
    raya_orch_owner_token owner, raya_orchestrator_context **context_out) {
    void *object=NULL;
    raya_orch_status status;
    raya_orchestrator_context *context;
    if (context_out==NULL) return RAYA_ORCH_INVALID;
    *context_out=NULL;
    status=raya_orch_owner_get_object(registry,handle,owner,
        RAYA_ORCHESTRATOR_CONTEXT_KIND,&object);
    if (status!=RAYA_ORCH_OK) return status;
    /* Owner token + zero leases prevent reclamation while caller retains owner. */
    context=(raya_orchestrator_context *)object;
    if (context->magic!=RAYA_ORCHESTRATOR_CONTEXT_MAGIC ||
        !context->registered || context->registry!=registry ||
        context->handle!=handle || context->adapter==NULL ||
        context->submitter==NULL || context->worker==NULL ||
        (context->worker_ownership!=RAYA_ORCH_WORKER_BORROWED &&
         context->worker_ownership!=RAYA_ORCH_WORKER_OWNED))
        return RAYA_ORCH_INVALID;
    *context_out=context;
    return RAYA_ORCH_OK;
}

raya_orch_status raya_orchestrator_destroy_adapter_owned(
    raya_orch_registry *registry, raya_orch_handle handle,
    raya_orch_owner_token owner, raya_adapter_destroy_result *result_out) {
    raya_orchestrator_context *context=NULL;
    raya_orch_cleanup_step cursor;
    raya_orch_status status;
    raya_adapter_destroy_status destroy_status;
    if (result_out==NULL) return RAYA_ORCH_INVALID;
    raya_adapter_destroy_result_init(result_out);
    status=raya_orchestrator_owner_get_context(registry,handle,owner,&context);
    if (status!=RAYA_ORCH_OK) return status;
    status=raya_orch_cursor_read(registry,handle,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_ADAPTER_DESTROY,&cursor);
    if (status!=RAYA_ORCH_OK) return status;
    if (cursor.effect==RAYA_ORCH_EFFECT_COMMITTED) {
        if (cursor.outcome!=RAYA_ORCH_OUTCOME_SUCCESS)
            return RAYA_ORCH_INVALID;
        result_out->status=RAYA_ADAPTER_DESTROY_SUCCESS;
        result_out->effect=RAYA_ADAPTER_DESTROY_EFFECT_DESTROYED;
        result_out->step=3;
        result_out->admission_closed=TRUE;
        result_out->lock_present=FALSE;
        return RAYA_ORCH_OK;
    }
    if (cursor.effect==RAYA_ORCH_EFFECT_UNKNOWN ||
        cursor.effect==RAYA_ORCH_EFFECT_BLOCKED ||
        cursor.effect==RAYA_ORCH_EFFECT_IN_PROGRESS) {
        result_out->status=RAYA_ADAPTER_DESTROY_BUSY;
        result_out->retry=RAYA_ADAPTER_DESTROY_RETRY_RECONCILE;
        return RAYA_ORCH_BUSY;
    }
    status=raya_orch_cursor_update(registry,handle,owner,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_ADAPTER_DESTROY,
        RAYA_ORCH_EFFECT_IN_PROGRESS,RAYA_ORCH_OUTCOME_NOT_EVALUATED,0,0);
    if (status!=RAYA_ORCH_OK) return status;
    destroy_status=raya_integrated_adapter_destroy_ex(context->adapter,result_out);
    if (destroy_status==RAYA_ADAPTER_DESTROY_SUCCESS) {
        status=raya_orch_cursor_update(registry,handle,owner,
            RAYA_ORCHESTRATOR_CLEANUP_STEP_ADAPTER_DESTROY,
            RAYA_ORCH_EFFECT_COMMITTED,RAYA_ORCH_OUTCOME_SUCCESS,0,0);
        return status;
    }
    /* Admission may already be irreversibly closed. Never label this a
       no-effect retry: the explicit reconciliation path must inspect fresh
       Adapter/entrant/retained evidence before authorizing another attempt. */
    status=raya_orch_cursor_update(registry,handle,owner,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_ADAPTER_DESTROY,
        RAYA_ORCH_EFFECT_BLOCKED,RAYA_ORCH_OUTCOME_UNCERTAIN,0,0);
    if (status!=RAYA_ORCH_OK) return status;
    return destroy_status==RAYA_ADAPTER_DESTROY_BUSY ?
        RAYA_ORCH_BUSY : RAYA_ORCH_INVALID;
}


raya_orch_status raya_orchestrator_close_submitter_admission_owned(
    raya_orch_registry *registry, raya_orch_handle handle,
    raya_orch_owner_token owner, LONG *active_count_out) {
    raya_orchestrator_context *context=NULL;
    raya_orch_cleanup_step adapter_cursor, cursor;
    raya_orch_status status;
    LONG active_count=0;
    int drained;
    if (active_count_out==NULL) return RAYA_ORCH_INVALID;
    *active_count_out=0;
    status=raya_orchestrator_owner_get_context(registry,handle,owner,&context);
    if (status!=RAYA_ORCH_OK) return status;
    status=raya_orch_cursor_read(registry,handle,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_ADAPTER_DESTROY,&adapter_cursor);
    if (status!=RAYA_ORCH_OK ||
        adapter_cursor.effect!=RAYA_ORCH_EFFECT_COMMITTED ||
        adapter_cursor.outcome!=RAYA_ORCH_OUTCOME_SUCCESS)
        return RAYA_ORCH_BUSY;
    status=raya_orch_cursor_read(registry,handle,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_ADMISSION_CLOSE,&cursor);
    if (status!=RAYA_ORCH_OK) return status;
    if (cursor.effect==RAYA_ORCH_EFFECT_COMMITTED) {
        if (cursor.outcome!=RAYA_ORCH_OUTCOME_SUCCESS) return RAYA_ORCH_INVALID;
        return RAYA_ORCH_OK;
    }
    if (cursor.effect==RAYA_ORCH_EFFECT_UNKNOWN ||
        cursor.effect==RAYA_ORCH_EFFECT_BLOCKED ||
        cursor.effect==RAYA_ORCH_EFFECT_IN_PROGRESS)
        return RAYA_ORCH_BUSY;
    status=raya_orch_cursor_update(registry,handle,owner,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_ADMISSION_CLOSE,
        RAYA_ORCH_EFFECT_IN_PROGRESS,RAYA_ORCH_OUTCOME_NOT_EVALUATED,0,0);
    if (status!=RAYA_ORCH_OK) return status;
    drained=raya_submitter_admission_close(context->submitter,&active_count);
    *active_count_out=active_count;
    if (drained) {
        return raya_orch_cursor_update(registry,handle,owner,
            RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_ADMISSION_CLOSE,
            RAYA_ORCH_EFFECT_COMMITTED,RAYA_ORCH_OUTCOME_SUCCESS,0,0);
    }
    if (context->submitter->initialized && active_count>0) {
        status=raya_orch_cursor_update(registry,handle,owner,
            RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_ADMISSION_CLOSE,
            RAYA_ORCH_EFFECT_NOT_ATTEMPTED,RAYA_ORCH_OUTCOME_RETRYABLE_FAILURE,
            (DWORD)active_count,0);
        return status==RAYA_ORCH_OK ? RAYA_ORCH_BUSY : status;
    }
    status=raya_orch_cursor_update(registry,handle,owner,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_ADMISSION_CLOSE,
        RAYA_ORCH_EFFECT_BLOCKED,RAYA_ORCH_OUTCOME_UNCERTAIN,
        ERROR_INVALID_STATE,0);
    return status==RAYA_ORCH_OK ? RAYA_ORCH_INVALID : status;
}


static raya_orch_status submitter_cursor_begin(
    raya_orch_registry *registry, raya_orch_handle handle,
    raya_orch_owner_token owner, uint32_t step,
    raya_orch_cleanup_step *cursor) {
    raya_orch_status status=raya_orch_cursor_read(registry,handle,step,cursor);
    if (status!=RAYA_ORCH_OK) return status;
    if (cursor->effect==RAYA_ORCH_EFFECT_COMMITTED) return RAYA_ORCH_BUSY;
    if (cursor->effect==RAYA_ORCH_EFFECT_UNKNOWN ||
        cursor->effect==RAYA_ORCH_EFFECT_BLOCKED ||
        cursor->effect==RAYA_ORCH_EFFECT_IN_PROGRESS)
        return RAYA_ORCH_BUSY;
    return raya_orch_cursor_update(registry,handle,owner,step,
        RAYA_ORCH_EFFECT_IN_PROGRESS,RAYA_ORCH_OUTCOME_NOT_EVALUATED,0,0);
}
static int submitter_cursor_succeeded(
    raya_orch_registry *registry, raya_orch_handle handle, uint32_t step,
    int allow_abnormal_exit) {
    raya_orch_cleanup_step cursor;
    if (raya_orch_cursor_read(registry,handle,step,&cursor)!=RAYA_ORCH_OK ||
        cursor.effect!=RAYA_ORCH_EFFECT_COMMITTED) return 0;
    return cursor.outcome==RAYA_ORCH_OUTCOME_SUCCESS ||
        (allow_abnormal_exit &&
         (cursor.outcome==RAYA_ORCH_OUTCOME_ABNORMAL_EXIT ||
          cursor.outcome==RAYA_ORCH_OUTCOME_TERMINAL_FAILURE));
}

raya_orch_status raya_orchestrator_submitter_shutdown_owned(
    raya_orch_registry *registry, raya_orch_handle handle,
    raya_orch_owner_token owner, raya_submitter_op_result *result_out) {
    raya_orchestrator_context *context=NULL;
    raya_orch_cleanup_step cursor;
    raya_orch_status status;
    raya_submitter_op_status operation;
    if (result_out==NULL) return RAYA_ORCH_INVALID;
    raya_submitter_result_init(result_out);
    status=raya_orchestrator_owner_get_context(registry,handle,owner,&context);
    if (status!=RAYA_ORCH_OK) return status;
    if (!submitter_cursor_succeeded(registry,handle,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_ADMISSION_CLOSE,0))
        return RAYA_ORCH_BUSY;
    status=submitter_cursor_begin(registry,handle,owner,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_SHUTDOWN,&cursor);
    if (status!=RAYA_ORCH_OK) {
        if (status==RAYA_ORCH_BUSY && cursor.effect==RAYA_ORCH_EFFECT_COMMITTED &&
            cursor.outcome==RAYA_ORCH_OUTCOME_SUCCESS) {
            result_out->status=RAYA_SUBMITTER_OP_ALREADY_DONE;
            result_out->effect=RAYA_SUBMITTER_EFFECT_SIGNALLED;
            return RAYA_ORCH_OK;
        }
        return status;
    }
    operation=raya_submitter_shutdown_ex(context->submitter,result_out);
    if (operation==RAYA_SUBMITTER_OP_SUCCESS ||
        operation==RAYA_SUBMITTER_OP_ALREADY_DONE)
        return raya_orch_cursor_update(registry,handle,owner,
            RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_SHUTDOWN,
            RAYA_ORCH_EFFECT_COMMITTED,RAYA_ORCH_OUTCOME_SUCCESS,0,0);
    status=raya_orch_cursor_update(registry,handle,owner,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_SHUTDOWN,
        RAYA_ORCH_EFFECT_BLOCKED,RAYA_ORCH_OUTCOME_UNCERTAIN,
        result_out->win32_error,0);
    return status==RAYA_ORCH_OK ? RAYA_ORCH_INVALID : status;
}

raya_orch_status raya_orchestrator_submitter_join_owned(
    raya_orch_registry *registry, raya_orch_handle handle,
    raya_orch_owner_token owner, DWORD timeout_ms,
    raya_submitter_op_result *result_out) {
    raya_orchestrator_context *context=NULL;
    raya_orch_cleanup_step cursor;
    raya_orch_status status;
    raya_submitter_op_status operation;
    if (result_out==NULL) return RAYA_ORCH_INVALID;
    raya_submitter_result_init(result_out);
    status=raya_orchestrator_owner_get_context(registry,handle,owner,&context);
    if (status!=RAYA_ORCH_OK) return status;
    if (!submitter_cursor_succeeded(registry,handle,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_SHUTDOWN,1))
        return RAYA_ORCH_BUSY;
    status=submitter_cursor_begin(registry,handle,owner,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_JOIN,&cursor);
    if (status!=RAYA_ORCH_OK) {
        if (status==RAYA_ORCH_BUSY && cursor.effect==RAYA_ORCH_EFFECT_COMMITTED &&
            (cursor.outcome==RAYA_ORCH_OUTCOME_SUCCESS ||
             cursor.outcome==RAYA_ORCH_OUTCOME_ABNORMAL_EXIT)) {
            result_out->status=cursor.outcome==RAYA_ORCH_OUTCOME_SUCCESS ?
                RAYA_SUBMITTER_OP_ALREADY_DONE : RAYA_SUBMITTER_OP_TERMINAL_FAILURE;
            result_out->effect=RAYA_SUBMITTER_EFFECT_THREAD_JOINED;
            result_out->thread_exit_code=cursor.exit_code;
            return RAYA_ORCH_OK;
        }
        return status;
    }
    operation=raya_submitter_join_ex(context->submitter,timeout_ms,result_out);
    if (operation==RAYA_SUBMITTER_OP_SUCCESS ||
        operation==RAYA_SUBMITTER_OP_ALREADY_DONE)
        return raya_orch_cursor_update(registry,handle,owner,
            RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_JOIN,
            RAYA_ORCH_EFFECT_COMMITTED,RAYA_ORCH_OUTCOME_SUCCESS,0,
            result_out->thread_exit_code);
    if (operation==RAYA_SUBMITTER_OP_RETRYABLE_FAILURE)
        return raya_orch_cursor_update(registry,handle,owner,
            RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_JOIN,
            RAYA_ORCH_EFFECT_NOT_ATTEMPTED,RAYA_ORCH_OUTCOME_RETRYABLE_FAILURE,
            result_out->win32_error,0);
    if (result_out->effect==RAYA_SUBMITTER_EFFECT_THREAD_JOINED)
        return raya_orch_cursor_update(registry,handle,owner,
            RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_JOIN,
            RAYA_ORCH_EFFECT_COMMITTED,RAYA_ORCH_OUTCOME_ABNORMAL_EXIT,
            result_out->win32_error,result_out->thread_exit_code);
    status=raya_orch_cursor_update(registry,handle,owner,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_JOIN,
        RAYA_ORCH_EFFECT_BLOCKED,RAYA_ORCH_OUTCOME_UNCERTAIN,
        result_out->win32_error,result_out->thread_exit_code);
    return status==RAYA_ORCH_OK ? RAYA_ORCH_INVALID : status;
}

raya_orch_status raya_orchestrator_submitter_destroy_owned(
    raya_orch_registry *registry, raya_orch_handle handle,
    raya_orch_owner_token owner, raya_submitter_op_result *result_out) {
    raya_orchestrator_context *context=NULL;
    raya_orch_cleanup_step cursor;
    raya_orch_status status;
    raya_submitter_op_status operation;
    if (result_out==NULL) return RAYA_ORCH_INVALID;
    raya_submitter_result_init(result_out);
    status=raya_orchestrator_owner_get_context(registry,handle,owner,&context);
    if (status!=RAYA_ORCH_OK) return status;
    if (!submitter_cursor_succeeded(registry,handle,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_SHUTDOWN,1) ||
        !submitter_cursor_succeeded(registry,handle,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_JOIN,1))
        return RAYA_ORCH_BUSY;
    status=submitter_cursor_begin(registry,handle,owner,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_DESTROY,&cursor);
    if (status!=RAYA_ORCH_OK) {
        if (status==RAYA_ORCH_BUSY && cursor.effect==RAYA_ORCH_EFFECT_COMMITTED &&
            cursor.outcome==RAYA_ORCH_OUTCOME_SUCCESS) {
            result_out->status=RAYA_SUBMITTER_OP_ALREADY_DONE;
            result_out->effect=RAYA_SUBMITTER_EFFECT_DESTROYED;
            return RAYA_ORCH_OK;
        }
        return status;
    }
    operation=raya_submitter_destroy_ex(context->submitter,result_out);
    if (operation==RAYA_SUBMITTER_OP_SUCCESS)
        return raya_orch_cursor_update(registry,handle,owner,
            RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_DESTROY,
            RAYA_ORCH_EFFECT_COMMITTED,RAYA_ORCH_OUTCOME_SUCCESS,0,0);
    if (operation==RAYA_SUBMITTER_OP_RETRYABLE_FAILURE) {
        raya_orch_effect effect=result_out->effect==RAYA_SUBMITTER_EFFECT_PARTIAL_DESTROY ?
            RAYA_ORCH_EFFECT_PARTIAL : RAYA_ORCH_EFFECT_NOT_ATTEMPTED;
        return raya_orch_cursor_update(registry,handle,owner,
            RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_DESTROY,effect,
            RAYA_ORCH_OUTCOME_RETRYABLE_FAILURE,result_out->win32_error,0);
    }
    status=raya_orch_cursor_update(registry,handle,owner,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_DESTROY,
        RAYA_ORCH_EFFECT_BLOCKED,RAYA_ORCH_OUTCOME_UNCERTAIN,
        result_out->win32_error,0);
    return status==RAYA_ORCH_OK ? RAYA_ORCH_INVALID : status;
}

static raya_orchestrator_context *leased_context(
    const raya_orchestrator_lease *lease) {
    if (lease==NULL || !lease->active || lease->context==NULL ||
        !raya_orch_lease_is_active(&lease->registry_lease) ||
        lease->registry_lease.object!=(void *)lease->context)
        return NULL;
    return lease->context;
}

raya_integrated_submit_adapter *raya_orchestrator_lease_adapter(
    const raya_orchestrator_lease *lease) {
    raya_orchestrator_context *c=leased_context(lease);
    return c!=NULL ? c->adapter : NULL;
}

raya_dedicated_submitter *raya_orchestrator_lease_submitter(
    const raya_orchestrator_lease *lease) {
    raya_orchestrator_context *c=leased_context(lease);
    return c!=NULL ? c->submitter : NULL;
}

raya_managed_worker *raya_orchestrator_lease_worker(
    const raya_orchestrator_lease *lease) {
    raya_orchestrator_context *c=leased_context(lease);
    return c!=NULL ? c->worker : NULL;
}

raya_orch_worker_ownership raya_orchestrator_lease_worker_ownership(
    const raya_orchestrator_lease *lease) {
    raya_orchestrator_context *c=leased_context(lease);
    return c!=NULL ? c->worker_ownership : (raya_orch_worker_ownership)0;
}

raya_orch_status raya_orchestrator_complete_close_owned(
    raya_orch_registry *registry, raya_orch_handle handle,
    raya_orch_owner_token owner) {
    raya_orchestrator_context *context=NULL;
    raya_orch_status status;
    status=raya_orchestrator_owner_get_context(registry,handle,owner,&context);
    if (status!=RAYA_ORCH_OK) return status;
    if (context->worker_ownership!=RAYA_ORCH_WORKER_BORROWED &&
        context->worker_ownership!=RAYA_ORCH_WORKER_OWNED)
        return RAYA_ORCH_INVALID;
    return raya_orch_complete_close_required(registry,handle,owner,
        RAYA_ORCHESTRATOR_CONTEXT_KIND);
}
static int orchestrator_common_cleanup_succeeded(
    raya_orch_registry *registry, raya_orch_handle handle) {
    uint32_t step;
    for (step=0;step<=RAYA_ORCHESTRATOR_CLEANUP_STEP_SUBMITTER_DESTROY;++step)
        if (!submitter_cursor_succeeded(registry,handle,step,0)) return 0;
    return 1;
}

static raya_orch_status orchestrator_owned_worker_context(
    raya_orch_registry *registry, raya_orch_handle handle,
    raya_orch_owner_token owner, raya_orchestrator_context **context_out) {
    raya_orch_status status;
    status=raya_orchestrator_owner_get_context(registry,handle,owner,context_out);
    if (status!=RAYA_ORCH_OK) return status;
    if ((*context_out)->worker_ownership!=RAYA_ORCH_WORKER_OWNED)
        return RAYA_ORCH_INVALID;
    return RAYA_ORCH_OK;
}

raya_orch_status raya_orchestrator_worker_shutdown_owned(
    raya_orch_registry *registry, raya_orch_handle handle,
    raya_orch_owner_token owner, raya_worker_op_result *result_out) {
    raya_orchestrator_context *context=NULL;
    raya_orch_cleanup_step cursor;
    raya_orch_status status;
    raya_worker_op_status operation;
    if (result_out==NULL) return RAYA_ORCH_INVALID;
    raya_worker_result_init(result_out);
    status=orchestrator_owned_worker_context(registry,handle,owner,&context);
    if (status!=RAYA_ORCH_OK) return status;
    if (!orchestrator_common_cleanup_succeeded(registry,handle))
        return RAYA_ORCH_BUSY;
    status=submitter_cursor_begin(registry,handle,owner,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_WORKER_SHUTDOWN,&cursor);
    if (status!=RAYA_ORCH_OK) {
        if (status==RAYA_ORCH_BUSY && cursor.effect==RAYA_ORCH_EFFECT_COMMITTED &&
            cursor.outcome==RAYA_ORCH_OUTCOME_SUCCESS) {
            result_out->status=RAYA_WORKER_OP_ALREADY_DONE;
            result_out->effect=RAYA_WORKER_EFFECT_SHUTDOWN_SIGNALLED;
            return RAYA_ORCH_OK;
        }
        return status;
    }
    operation=raya_managed_worker_shutdown_ex(context->worker,result_out);
    if (operation==RAYA_WORKER_OP_SUCCESS ||
        operation==RAYA_WORKER_OP_ALREADY_DONE)
        return raya_orch_cursor_update(registry,handle,owner,
            RAYA_ORCHESTRATOR_CLEANUP_STEP_WORKER_SHUTDOWN,
            RAYA_ORCH_EFFECT_COMMITTED,RAYA_ORCH_OUTCOME_SUCCESS,0,0);
    if (operation==RAYA_WORKER_OP_RETRYABLE_FAILURE) {
        raya_orch_effect effect=result_out->effect==RAYA_WORKER_EFFECT_STOPPING ?
            RAYA_ORCH_EFFECT_PARTIAL : RAYA_ORCH_EFFECT_NOT_ATTEMPTED;
        return raya_orch_cursor_update(registry,handle,owner,
            RAYA_ORCHESTRATOR_CLEANUP_STEP_WORKER_SHUTDOWN,effect,
            RAYA_ORCH_OUTCOME_RETRYABLE_FAILURE,result_out->win32_error,0);
    }
    status=raya_orch_cursor_update(registry,handle,owner,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_WORKER_SHUTDOWN,
        RAYA_ORCH_EFFECT_BLOCKED,RAYA_ORCH_OUTCOME_UNCERTAIN,
        result_out->win32_error,0);
    return status==RAYA_ORCH_OK ? RAYA_ORCH_INVALID : status;
}

raya_orch_status raya_orchestrator_worker_join_owned(
    raya_orch_registry *registry, raya_orch_handle handle,
    raya_orch_owner_token owner, DWORD timeout_ms,
    raya_worker_op_result *result_out) {
    raya_orchestrator_context *context=NULL;
    raya_orch_cleanup_step cursor;
    raya_orch_status status;
    raya_worker_op_status operation;
    if (result_out==NULL) return RAYA_ORCH_INVALID;
    raya_worker_result_init(result_out);
    status=orchestrator_owned_worker_context(registry,handle,owner,&context);
    if (status!=RAYA_ORCH_OK) return status;
    if (!submitter_cursor_succeeded(registry,handle,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_WORKER_SHUTDOWN,0))
        return RAYA_ORCH_BUSY;
    status=submitter_cursor_begin(registry,handle,owner,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_WORKER_JOIN,&cursor);
    if (status!=RAYA_ORCH_OK) {
        if (status==RAYA_ORCH_BUSY && cursor.effect==RAYA_ORCH_EFFECT_COMMITTED &&
            (cursor.outcome==RAYA_ORCH_OUTCOME_SUCCESS ||
             cursor.outcome==RAYA_ORCH_OUTCOME_ABNORMAL_EXIT)) {
            result_out->status=cursor.outcome==RAYA_ORCH_OUTCOME_SUCCESS ?
                RAYA_WORKER_OP_ALREADY_DONE : RAYA_WORKER_OP_TERMINAL_FAILURE;
            result_out->effect=RAYA_WORKER_EFFECT_THREAD_REAPED;
            result_out->thread_exit_code=cursor.exit_code;
            return RAYA_ORCH_OK;
        }
        return status;
    }
    operation=raya_managed_worker_join_ex(context->worker,timeout_ms,result_out);
    if (operation==RAYA_WORKER_OP_SUCCESS ||
        operation==RAYA_WORKER_OP_ALREADY_DONE)
        return raya_orch_cursor_update(registry,handle,owner,
            RAYA_ORCHESTRATOR_CLEANUP_STEP_WORKER_JOIN,
            RAYA_ORCH_EFFECT_COMMITTED,RAYA_ORCH_OUTCOME_SUCCESS,0,
            result_out->thread_exit_code);
    if (operation==RAYA_WORKER_OP_RETRYABLE_FAILURE)
        return raya_orch_cursor_update(registry,handle,owner,
            RAYA_ORCHESTRATOR_CLEANUP_STEP_WORKER_JOIN,
            RAYA_ORCH_EFFECT_NOT_ATTEMPTED,RAYA_ORCH_OUTCOME_RETRYABLE_FAILURE,
            result_out->win32_error,0);
    if (result_out->effect==RAYA_WORKER_EFFECT_THREAD_REAPED)
        return raya_orch_cursor_update(registry,handle,owner,
            RAYA_ORCHESTRATOR_CLEANUP_STEP_WORKER_JOIN,
            RAYA_ORCH_EFFECT_COMMITTED,RAYA_ORCH_OUTCOME_ABNORMAL_EXIT,
            result_out->win32_error,result_out->thread_exit_code);
    status=raya_orch_cursor_update(registry,handle,owner,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_WORKER_JOIN,
        RAYA_ORCH_EFFECT_BLOCKED,RAYA_ORCH_OUTCOME_UNCERTAIN,
        result_out->win32_error,result_out->thread_exit_code);
    return status==RAYA_ORCH_OK ? RAYA_ORCH_INVALID : status;
}

raya_orch_status raya_orchestrator_worker_destroy_owned(
    raya_orch_registry *registry, raya_orch_handle handle,
    raya_orch_owner_token owner, raya_worker_op_result *result_out) {
    raya_orchestrator_context *context=NULL;
    raya_orch_cleanup_step cursor;
    raya_orch_status status;
    raya_worker_op_status operation;
    if (result_out==NULL) return RAYA_ORCH_INVALID;
    raya_worker_result_init(result_out);
    status=orchestrator_owned_worker_context(registry,handle,owner,&context);
    if (status!=RAYA_ORCH_OK) return status;
    if (!submitter_cursor_succeeded(registry,handle,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_WORKER_SHUTDOWN,0) ||
        !submitter_cursor_succeeded(registry,handle,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_WORKER_JOIN,0))
        return RAYA_ORCH_BUSY;
    status=submitter_cursor_begin(registry,handle,owner,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_WORKER_DESTROY,&cursor);
    if (status!=RAYA_ORCH_OK) {
        if (status==RAYA_ORCH_BUSY && cursor.effect==RAYA_ORCH_EFFECT_COMMITTED &&
            cursor.outcome==RAYA_ORCH_OUTCOME_SUCCESS) {
            result_out->status=RAYA_WORKER_OP_ALREADY_DONE;
            result_out->effect=RAYA_WORKER_EFFECT_DESTROYED;
            return RAYA_ORCH_OK;
        }
        return status;
    }
    operation=raya_managed_worker_destroy_ex(context->worker,result_out);
    if (operation==RAYA_WORKER_OP_SUCCESS)
        return raya_orch_cursor_update(registry,handle,owner,
            RAYA_ORCHESTRATOR_CLEANUP_STEP_WORKER_DESTROY,
            RAYA_ORCH_EFFECT_COMMITTED,RAYA_ORCH_OUTCOME_SUCCESS,0,0);
    if (operation==RAYA_WORKER_OP_RETRYABLE_FAILURE)
        return raya_orch_cursor_update(registry,handle,owner,
            RAYA_ORCHESTRATOR_CLEANUP_STEP_WORKER_DESTROY,
            RAYA_ORCH_EFFECT_NOT_ATTEMPTED,RAYA_ORCH_OUTCOME_RETRYABLE_FAILURE,
            result_out->win32_error,0);
    status=raya_orch_cursor_update(registry,handle,owner,
        RAYA_ORCHESTRATOR_CLEANUP_STEP_WORKER_DESTROY,
        RAYA_ORCH_EFFECT_BLOCKED,RAYA_ORCH_OUTCOME_UNCERTAIN,
        result_out->win32_error,0);
    return status==RAYA_ORCH_OK ? RAYA_ORCH_INVALID : status;
}