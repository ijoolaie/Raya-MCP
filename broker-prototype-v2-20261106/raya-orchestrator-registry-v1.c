#include "raya-orchestrator-registry-v1.h"
#include <string.h>

static uint32_t token_slot(raya_orch_handle h) { return (uint32_t)h; }
static uint32_t token_generation(raya_orch_handle h) { return (uint32_t)(h >> 32); }
static raya_orch_handle make_handle(uint32_t slot, uint32_t generation) {
    return (((uint64_t)generation) << 32) | (uint64_t)(slot + 1u);
}
static raya_orch_registry_slot *find_slot(raya_orch_registry *r,
                                           raya_orch_handle h) {
    uint32_t encoded=token_slot(h), generation=token_generation(h);
    raya_orch_registry_slot *s;
    if (encoded==0 || encoded>RAYA_ORCH_REGISTRY_CAPACITY || generation==0)
        return NULL;
    s=&r->slots[encoded-1u];
    if (s->generation!=generation || s->state==RAYA_ORCH_SLOT_EMPTY ||
        s->state==RAYA_ORCH_SLOT_RETIRED) return NULL;
    return s;
}
int raya_orch_registry_init(raya_orch_registry *r) {
    uint32_t i;
    if (r==NULL) return 0;
    memset(r,0,sizeof(*r));
    InitializeSRWLock(&r->lock);
    InitializeConditionVariable(&r->drained);
    for (i=0;i<RAYA_ORCH_REGISTRY_CAPACITY;++i) r->slots[i].generation=1u;
    r->next_owner=1u;
    r->next_lease_id=1u;
    r->initialized=TRUE;
    return 1;
}
raya_orch_status raya_orch_register_with_policy(raya_orch_registry *r,
    void *object, uint32_t kind, raya_orch_close_policy policy,
    uint32_t required_mask, raya_orch_handle *out) {
    uint32_t i;
    const uint32_t valid_mask=(1u<<RAYA_ORCH_CLEANUP_STEPS)-1u;
    if (out==NULL) return RAYA_ORCH_INVALID;
    *out=0;
    if (r==NULL || !r->initialized || object==NULL || kind==0 ||
        (policy!=RAYA_ORCH_CLOSE_POLICY_GENERIC &&
         policy!=RAYA_ORCH_CLOSE_POLICY_REQUIRED_STEPS) ||
        (required_mask & ~valid_mask)!=0 ||
        (policy==RAYA_ORCH_CLOSE_POLICY_GENERIC && required_mask!=0) ||
        (policy==RAYA_ORCH_CLOSE_POLICY_REQUIRED_STEPS && required_mask==0))
        return RAYA_ORCH_INVALID;
    AcquireSRWLockExclusive(&r->lock);
    for (i=0;i<RAYA_ORCH_REGISTRY_CAPACITY;++i) {
        raya_orch_registry_slot *s=&r->slots[i];
        if (s->state==RAYA_ORCH_SLOT_EMPTY) {
            s->object=object; s->object_kind=kind; s->lease_count=0;
            s->owner=0; s->close_policy=policy;
            s->required_success_mask=required_mask;
            memset(s->steps,0,sizeof(s->steps));
            s->state=RAYA_ORCH_SLOT_OPEN;
            *out=make_handle(i,s->generation);
            ReleaseSRWLockExclusive(&r->lock);
            return RAYA_ORCH_OK;
        }
    }
    ReleaseSRWLockExclusive(&r->lock);
    return RAYA_ORCH_CAPACITY;
}

raya_orch_status raya_orch_register(raya_orch_registry *r, void *object,
    uint32_t kind, raya_orch_handle *out) {
    return raya_orch_register_with_policy(r,object,kind,
        RAYA_ORCH_CLOSE_POLICY_GENERIC,0,out);
}
raya_orch_status raya_orch_acquire(raya_orch_registry *r, raya_orch_handle h,
    uint32_t expected_kind, raya_orch_lease *lease) {
    raya_orch_registry_slot *s;
    raya_orch_lease_record *record=NULL;
    uint32_t i;
    if (r==NULL || !r->initialized || lease==NULL || h==0) return RAYA_ORCH_INVALID;
    memset(lease,0,sizeof(*lease));
    AcquireSRWLockExclusive(&r->lock);
    s=find_slot(r,h);
    if (s==NULL || s->state!=RAYA_ORCH_SLOT_OPEN ||
        (expected_kind!=0 && s->object_kind!=expected_kind) ||
        s->lease_count==UINT32_MAX || r->next_lease_id==0) {
        ReleaseSRWLockExclusive(&r->lock); return RAYA_ORCH_INVALID;
    }
    for (i=0;i<RAYA_ORCH_LEASE_CAPACITY;++i) {
        if (!r->leases[i].active) { record=&r->leases[i]; break; }
    }
    if (record==NULL) {
        ReleaseSRWLockExclusive(&r->lock); return RAYA_ORCH_CAPACITY;
    }
    record->id=r->next_lease_id++;
    record->handle=h; record->object=s->object; record->active=TRUE;
    ++s->lease_count;
    lease->handle=h; lease->object=s->object; lease->registry_private=r;
    lease->lease_id=record->id; lease->active=TRUE;
    ReleaseSRWLockExclusive(&r->lock);
    return RAYA_ORCH_OK;
}
raya_orch_status raya_orch_release(raya_orch_lease *lease) {
    raya_orch_registry *r;
    raya_orch_registry_slot *s;
    raya_orch_lease_record *record=NULL;
    uint32_t i;
    if (lease==NULL || !lease->active || lease->registry_private==NULL ||
        lease->lease_id==0) return RAYA_ORCH_INVALID;
    r=(raya_orch_registry *)lease->registry_private;
    AcquireSRWLockExclusive(&r->lock);
    for (i=0;i<RAYA_ORCH_LEASE_CAPACITY;++i) {
        if (r->leases[i].active && r->leases[i].id==lease->lease_id &&
            r->leases[i].handle==lease->handle && r->leases[i].object==lease->object) {
            record=&r->leases[i]; break;
        }
    }
    s=find_slot(r,lease->handle);
    if (record==NULL || s==NULL || s->lease_count==0 || s->object!=lease->object) {
        ReleaseSRWLockExclusive(&r->lock); return RAYA_ORCH_INVALID;
    }
    record->active=FALSE; record->object=NULL; record->handle=0;
    --s->lease_count;
    lease->active=FALSE; lease->object=NULL; lease->registry_private=NULL;
    lease->lease_id=0;
    if (s->state==RAYA_ORCH_SLOT_CLOSING && s->lease_count==0)
        WakeAllConditionVariable(&r->drained);
    ReleaseSRWLockExclusive(&r->lock);
    return RAYA_ORCH_OK;
}
int raya_orch_lease_is_active(const raya_orch_lease *lease) {
    raya_orch_registry *r;
    uint32_t i;
    int active=0;
    if (lease==NULL || !lease->active || lease->registry_private==NULL ||
        lease->lease_id==0) return 0;
    r=(raya_orch_registry *)lease->registry_private;
    if (!r->initialized) return 0;
    AcquireSRWLockShared(&r->lock);
    for (i=0;i<RAYA_ORCH_LEASE_CAPACITY;++i) {
        const raya_orch_lease_record *record=&r->leases[i];
        if (record->active && record->id==lease->lease_id &&
            record->handle==lease->handle && record->object==lease->object) {
            active=1;
            break;
        }
    }
    ReleaseSRWLockShared(&r->lock);
    return active;
}

raya_orch_status raya_orch_begin_close(raya_orch_registry *r,
    raya_orch_handle h, raya_orch_owner_token *owner_out) {
    raya_orch_registry_slot *s;
    if (r==NULL || !r->initialized || owner_out==NULL) return RAYA_ORCH_INVALID;
    *owner_out=0;
    AcquireSRWLockExclusive(&r->lock);
    s=find_slot(r,h);
    if (s==NULL ||
        !((s->state==RAYA_ORCH_SLOT_OPEN && s->owner==0) ||
          (s->state==RAYA_ORCH_SLOT_CLOSING && s->owner==0))) {
        ReleaseSRWLockExclusive(&r->lock); return RAYA_ORCH_BUSY;
    }
    if (r->next_owner==0) {
        ReleaseSRWLockExclusive(&r->lock); return RAYA_ORCH_CAPACITY;
    }
    s->owner=r->next_owner++;
    s->state=RAYA_ORCH_SLOT_CLOSING;
    *owner_out=s->owner;
    ReleaseSRWLockExclusive(&r->lock);
    return RAYA_ORCH_OK;
}
raya_orch_status raya_orch_wait_drained(raya_orch_registry *r,
    raya_orch_handle h, raya_orch_owner_token owner, DWORD timeout_ms) {
    raya_orch_registry_slot *s;
    DWORD start=GetTickCount(), remaining=timeout_ms;
    if (r==NULL || !r->initialized || owner==0) return RAYA_ORCH_INVALID;
    AcquireSRWLockExclusive(&r->lock);
    for (;;) {
        s=find_slot(r,h);
        if (s==NULL || s->state!=RAYA_ORCH_SLOT_CLOSING || s->owner!=owner) {
            ReleaseSRWLockExclusive(&r->lock); return RAYA_ORCH_INVALID;
        }
        if (s->lease_count==0) {
            ReleaseSRWLockExclusive(&r->lock); return RAYA_ORCH_OK;
        }
        if (timeout_ms==0) {
            ReleaseSRWLockExclusive(&r->lock); return RAYA_ORCH_TIMEOUT;
        }
        if (!SleepConditionVariableSRW(&r->drained,&r->lock,remaining,0)) {
            DWORD e=GetLastError();
            ReleaseSRWLockExclusive(&r->lock);
            return e==ERROR_TIMEOUT ? RAYA_ORCH_TIMEOUT : RAYA_ORCH_BUSY;
        }
        if (timeout_ms!=INFINITE) {
            DWORD elapsed=GetTickCount()-start;
            if (elapsed>=timeout_ms) remaining=0;
            else remaining=timeout_ms-elapsed;
        }
    }
}
raya_orch_status raya_orch_owner_get_object(raya_orch_registry *r,
    raya_orch_handle h, raya_orch_owner_token owner,
    uint32_t expected_kind, void **object_out) {
    raya_orch_registry_slot *s;
    if (object_out==NULL) return RAYA_ORCH_INVALID;
    *object_out=NULL;
    if (r==NULL || !r->initialized || owner==0 || expected_kind==0)
        return RAYA_ORCH_INVALID;
    AcquireSRWLockShared(&r->lock);
    s=find_slot(r,h);
    if (s==NULL || s->state!=RAYA_ORCH_SLOT_CLOSING ||
        s->owner!=owner || s->object_kind!=expected_kind ||
        s->lease_count!=0 || s->object==NULL) {
        ReleaseSRWLockShared(&r->lock);
        return RAYA_ORCH_BUSY;
    }
    *object_out=s->object;
    ReleaseSRWLockShared(&r->lock);
    return RAYA_ORCH_OK;
}

raya_orch_status raya_orch_cursor_update(raya_orch_registry *r,
    raya_orch_handle h, raya_orch_owner_token owner, uint32_t step,
    raya_orch_effect effect, raya_orch_outcome outcome,
    DWORD last_error, DWORD exit_code) {
    raya_orch_registry_slot *s;
    if (r==NULL || !r->initialized || owner==0 || step>=RAYA_ORCH_CLEANUP_STEPS ||
        effect<RAYA_ORCH_EFFECT_NOT_ATTEMPTED || effect>RAYA_ORCH_EFFECT_PARTIAL ||
        outcome<RAYA_ORCH_OUTCOME_NOT_EVALUATED || outcome>RAYA_ORCH_OUTCOME_UNCERTAIN)
        return RAYA_ORCH_INVALID;
    AcquireSRWLockExclusive(&r->lock);
    s=find_slot(r,h);
    if (s==NULL || s->state!=RAYA_ORCH_SLOT_CLOSING || s->owner!=owner) {
        ReleaseSRWLockExclusive(&r->lock); return RAYA_ORCH_INVALID;
    }
    {
        raya_orch_cleanup_step *current=&s->steps[step];
        if (current->effect==RAYA_ORCH_EFFECT_COMMITTED) {
            int identical=(effect==current->effect && outcome==current->outcome &&
                last_error==current->last_error && exit_code==current->exit_code);
            ReleaseSRWLockExclusive(&r->lock);
            return identical ? RAYA_ORCH_OK : RAYA_ORCH_INVALID;
        }
        if (effect==RAYA_ORCH_EFFECT_IN_PROGRESS) {
            /* A new attempt is legal only before first execution or after an
               explicitly retryable no-effect result. Unknown/blocked effects
               require reconciliation, never blind re-execution. */
            if ((current->effect!=RAYA_ORCH_EFFECT_NOT_ATTEMPTED &&
                 current->effect!=RAYA_ORCH_EFFECT_PARTIAL) ||
                (current->outcome!=RAYA_ORCH_OUTCOME_NOT_EVALUATED &&
                 current->outcome!=RAYA_ORCH_OUTCOME_RETRYABLE_FAILURE) ||
                outcome!=RAYA_ORCH_OUTCOME_NOT_EVALUATED) {
                ReleaseSRWLockExclusive(&r->lock); return RAYA_ORCH_INVALID;
            }
            if (current->attempts==UINT32_MAX) {
                ReleaseSRWLockExclusive(&r->lock); return RAYA_ORCH_CAPACITY;
            }
            ++current->attempts;
        } else {
            /* Every result must close an in-progress attempt. This prevents
               callers from fabricating completion or changing a prior result. */
            if (current->effect!=RAYA_ORCH_EFFECT_IN_PROGRESS ||
                outcome==RAYA_ORCH_OUTCOME_NOT_EVALUATED) {
                ReleaseSRWLockExclusive(&r->lock); return RAYA_ORCH_INVALID;
            }
        }
        current->effect=effect;
        current->outcome=outcome;
        current->last_error=last_error;
        current->exit_code=exit_code;
    }
    ReleaseSRWLockExclusive(&r->lock);
    return RAYA_ORCH_OK;
}
raya_orch_status raya_orch_release_cleanup_owner(raya_orch_registry *r,
    raya_orch_handle h, raya_orch_owner_token owner) {
    raya_orch_registry_slot *s;
    uint32_t i;
    if (r==NULL || !r->initialized || owner==0) return RAYA_ORCH_INVALID;
    AcquireSRWLockExclusive(&r->lock);
    s=find_slot(r,h);
    if (s==NULL || s->state!=RAYA_ORCH_SLOT_CLOSING || s->owner!=owner) {
        ReleaseSRWLockExclusive(&r->lock); return RAYA_ORCH_INVALID;
    }
    if (s->lease_count!=0) {
        ReleaseSRWLockExclusive(&r->lock); return RAYA_ORCH_BUSY;
    }
    for (i=0;i<RAYA_ORCH_CLEANUP_STEPS;++i) {
        raya_orch_cleanup_step *step=&s->steps[i];
        if (step->effect==RAYA_ORCH_EFFECT_IN_PROGRESS ||
            step->effect==RAYA_ORCH_EFFECT_UNKNOWN ||
            step->effect==RAYA_ORCH_EFFECT_BLOCKED ||
            step->effect==RAYA_ORCH_EFFECT_PARTIAL) {
            ReleaseSRWLockExclusive(&r->lock); return RAYA_ORCH_BUSY;
        }
    }
    s->owner=0;
    ReleaseSRWLockExclusive(&r->lock);
    return RAYA_ORCH_OK;
}

raya_orch_status raya_orch_cursor_reconcile(raya_orch_registry *r,
    raya_orch_handle h, raya_orch_owner_token owner, uint32_t index,
    raya_orch_effect resolved_effect, raya_orch_outcome resolved_outcome,
    DWORD last_error, DWORD exit_code) {
    raya_orch_registry_slot *s;
    raya_orch_cleanup_step *step;
    int valid_no_effect, valid_committed;
    if (r==NULL || !r->initialized || owner==0 ||
        index>=RAYA_ORCH_CLEANUP_STEPS) return RAYA_ORCH_INVALID;
    valid_no_effect=(resolved_effect==RAYA_ORCH_EFFECT_NOT_ATTEMPTED &&
        resolved_outcome==RAYA_ORCH_OUTCOME_RETRYABLE_FAILURE);
    valid_committed=(resolved_effect==RAYA_ORCH_EFFECT_COMMITTED &&
        (resolved_outcome==RAYA_ORCH_OUTCOME_SUCCESS ||
         resolved_outcome==RAYA_ORCH_OUTCOME_ABNORMAL_EXIT ||
         resolved_outcome==RAYA_ORCH_OUTCOME_TERMINAL_FAILURE));
    if (!valid_no_effect && !valid_committed) return RAYA_ORCH_INVALID;
    AcquireSRWLockExclusive(&r->lock);
    s=find_slot(r,h);
    if (s==NULL || s->state!=RAYA_ORCH_SLOT_CLOSING || s->owner!=owner) {
        ReleaseSRWLockExclusive(&r->lock); return RAYA_ORCH_INVALID;
    }
    step=&s->steps[index];
    if (step->effect!=RAYA_ORCH_EFFECT_IN_PROGRESS &&
        step->effect!=RAYA_ORCH_EFFECT_UNKNOWN &&
        step->effect!=RAYA_ORCH_EFFECT_BLOCKED &&
        step->effect!=RAYA_ORCH_EFFECT_PARTIAL) {
        ReleaseSRWLockExclusive(&r->lock); return RAYA_ORCH_INVALID;
    }
    step->effect=resolved_effect;
    step->outcome=resolved_outcome;
    step->last_error=last_error;
    step->exit_code=exit_code;
    ReleaseSRWLockExclusive(&r->lock);
    return RAYA_ORCH_OK;
}

raya_orch_status raya_orch_cursor_read(raya_orch_registry *r,
    raya_orch_handle h, uint32_t step, raya_orch_cleanup_step *out) {
    raya_orch_registry_slot *s;
    if (r==NULL || !r->initialized || out==NULL || step>=RAYA_ORCH_CLEANUP_STEPS)
        return RAYA_ORCH_INVALID;
    AcquireSRWLockShared(&r->lock);
    s=find_slot(r,h);
    if (s==NULL) { ReleaseSRWLockShared(&r->lock); return RAYA_ORCH_INVALID; }
    *out=s->steps[step];
    ReleaseSRWLockShared(&r->lock);
    return RAYA_ORCH_OK;
}
raya_orch_status raya_orch_complete_close(raya_orch_registry *r,
    raya_orch_handle h, raya_orch_owner_token owner) {
    raya_orch_registry_slot *s;
    if (r==NULL || !r->initialized || owner==0) return RAYA_ORCH_INVALID;
    AcquireSRWLockExclusive(&r->lock);
    s=find_slot(r,h);
    if (s==NULL || s->state!=RAYA_ORCH_SLOT_CLOSING || s->owner!=owner ||
        s->lease_count!=0 ||
        s->close_policy!=RAYA_ORCH_CLOSE_POLICY_GENERIC) {
        ReleaseSRWLockExclusive(&r->lock); return RAYA_ORCH_BUSY;
    }
    {
        uint32_t i;
        for (i=0;i<RAYA_ORCH_CLEANUP_STEPS;++i) {
            raya_orch_cleanup_step *step=&s->steps[i];
            if (step->effect!=RAYA_ORCH_EFFECT_NOT_ATTEMPTED &&
                !(step->effect==RAYA_ORCH_EFFECT_COMMITTED &&
                  (step->outcome==RAYA_ORCH_OUTCOME_SUCCESS ||
                   step->outcome==RAYA_ORCH_OUTCOME_ABNORMAL_EXIT ||
                   step->outcome==RAYA_ORCH_OUTCOME_TERMINAL_FAILURE))) {
                ReleaseSRWLockExclusive(&r->lock); return RAYA_ORCH_BUSY;
            }
        }
    }
    s->object=NULL; s->object_kind=0; s->owner=0;
    s->close_policy=RAYA_ORCH_CLOSE_POLICY_GENERIC;
    s->required_success_mask=0;
    if (s->generation==UINT32_MAX) s->state=RAYA_ORCH_SLOT_RETIRED;
    else { ++s->generation; s->state=RAYA_ORCH_SLOT_EMPTY; }
    ReleaseSRWLockExclusive(&r->lock);
    return RAYA_ORCH_OK;
}

raya_orch_status raya_orch_complete_close_required(raya_orch_registry *r,
    raya_orch_handle h, raya_orch_owner_token owner, uint32_t expected_kind) {
    raya_orch_registry_slot *s;
    uint32_t i;
    const uint32_t valid_mask=(1u<<RAYA_ORCH_CLEANUP_STEPS)-1u;
    if (r==NULL || !r->initialized || owner==0 || expected_kind==0)
        return RAYA_ORCH_INVALID;
    AcquireSRWLockExclusive(&r->lock);
    s=find_slot(r,h);
    if (s==NULL || s->state!=RAYA_ORCH_SLOT_CLOSING || s->owner!=owner ||
        s->lease_count!=0 || s->object_kind!=expected_kind ||
        s->close_policy!=RAYA_ORCH_CLOSE_POLICY_REQUIRED_STEPS ||
        s->required_success_mask==0 ||
        (s->required_success_mask & ~valid_mask)!=0) {
        ReleaseSRWLockExclusive(&r->lock);
        return RAYA_ORCH_BUSY;
    }
    for (i=0;i<RAYA_ORCH_CLEANUP_STEPS;++i) {
        const raya_orch_cleanup_step *step=&s->steps[i];
        const uint32_t bit=(1u<<i);
        if ((s->required_success_mask & bit)!=0) {
            if (step->effect!=RAYA_ORCH_EFFECT_COMMITTED ||
                step->outcome!=RAYA_ORCH_OUTCOME_SUCCESS) {
                ReleaseSRWLockExclusive(&r->lock);
                return RAYA_ORCH_BUSY;
            }
        } else if (step->effect!=RAYA_ORCH_EFFECT_NOT_ATTEMPTED ||
                   step->outcome!=RAYA_ORCH_OUTCOME_NOT_EVALUATED ||
                   step->attempts!=0 || step->last_error!=0 ||
                   step->exit_code!=0) {
            ReleaseSRWLockExclusive(&r->lock);
            return RAYA_ORCH_BUSY;
        }
    }
    s->object=NULL; s->object_kind=0; s->owner=0;
    s->close_policy=RAYA_ORCH_CLOSE_POLICY_GENERIC;
    s->required_success_mask=0;
    if (s->generation==UINT32_MAX) s->state=RAYA_ORCH_SLOT_RETIRED;
    else { ++s->generation; s->state=RAYA_ORCH_SLOT_EMPTY; }
    ReleaseSRWLockExclusive(&r->lock);
    return RAYA_ORCH_OK;
}