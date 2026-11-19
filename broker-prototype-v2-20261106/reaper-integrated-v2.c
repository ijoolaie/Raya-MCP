#include "reaper-integrated-v2.h"
#include <string.h>
static int valid_event(HANDLE h) { return h != NULL && h != INVALID_HANDLE_VALUE; }
int raya_reaper_integrated_init(raya_reaper_integrated *s) {
 if (s==NULL) return 0;
 memset(s,0,sizeof(*s));
 return raya_reaper_core_start(&s->core);
}
int raya_reaper_integrated_admit(raya_reaper_integrated *s) {
 return s!=NULL && raya_reaper_admit(&s->core);
}
int raya_reaper_integrated_reserve(raya_reaper_integrated *s,
                                   raya_integrated_op *op) {
 if (s==NULL || op==NULL || s->core.lifecycle!=RAYA_REAPER_ACCEPTING ||
     s->core.caller_active==0 ||
     op->base.state!=RAYA_REAPER_OP_CALLER_PENDING || op->base.next!=NULL ||
     op->reservation_state!=RAYA_INTEGRATED_RESERVATION_NONE ||
     !valid_event(op->completion_event) ||
     s->reserved_slots>=RAYA_REAPER_INTEGRATED_CAPACITY)
  return 0;
 ++s->reserved_slots;
 op->reservation_state=RAYA_INTEGRATED_RESERVATION_HELD;
 return 1;
}
int raya_reaper_integrated_caller_terminal(raya_reaper_integrated *s,
                                   raya_integrated_op *op) {
 if (s==NULL || op==NULL ||
     op->reservation_state!=RAYA_INTEGRATED_RESERVATION_HELD ||
     op->base.state!=RAYA_REAPER_OP_CALLER_PENDING ||
     op->collect_state!=RAYA_INTEGRATED_COLLECT_PENDING ||
     !valid_event(op->completion_event) || s->core.caller_active==0)
  return 0;
 if (!raya_reaper_caller_terminal(&s->core,&op->base)) {
  (void)raya_reaper_mark_degraded_cause(
      &s->core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
  return 0;
 }
 return 1;
}
int raya_reaper_integrated_unreserve(raya_reaper_integrated *s,
                                   raya_integrated_op *op) {
 if (s==NULL || op==NULL ||
     op->reservation_state!=RAYA_INTEGRATED_RESERVATION_HELD ||
     op->base.state!=RAYA_REAPER_OP_CALLER_TERMINAL ||
     op->base.next!=NULL || s->reserved_slots==0 ||
     s->core.caller_active==0)
  return 0;
 --s->reserved_slots;
 op->reservation_state=RAYA_INTEGRATED_RESERVATION_RELEASED;
 return 1;
}
int raya_reaper_integrated_handoff(raya_reaper_integrated *s,
                                   raya_integrated_op *op) {
 if (s==NULL || op==NULL ||
     op->reservation_state!=RAYA_INTEGRATED_RESERVATION_HELD ||
     !valid_event(op->completion_event) ||
     op->collect_state!=RAYA_INTEGRATED_COLLECT_PENDING ||
     !raya_reaper_handoff(&s->core,&op->base)) return 0;
 op->reservation_state=RAYA_INTEGRATED_RESERVATION_HANDED_OFF;
 return 1;
}
raya_integrated_result raya_reaper_integrated_promote_one(
                                   raya_reaper_integrated *s,
                                   raya_integrated_op **out) {
 raya_reaper_op *base;
 raya_reaper_op *dequeued;
 raya_integrated_op *op;
 if (out!=NULL) *out=NULL;
 if (s==NULL || out==NULL) return RAYA_INTEGRATED_INVALID;
 if (s->core.lifecycle==RAYA_REAPER_DEGRADED) return RAYA_INTEGRATED_DEGRADED;
 base=s->core.head;
 if (base==NULL) return RAYA_INTEGRATED_EMPTY;
 op=(raya_integrated_op *)base;
 if (s->reserved_slots==0 || s->reserved_slots<s->registry.count+1 ||
     op->reservation_state!=RAYA_INTEGRATED_RESERVATION_HANDED_OFF ||
     base->state!=RAYA_REAPER_OP_QUEUED ||
     !valid_event(op->completion_event) ||
     !raya_reaper_registry_add(&s->registry,base,op->completion_event)) {
  (void)raya_reaper_mark_degraded_cause(&s->core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
  return RAYA_INTEGRATED_DEGRADED;
 }
 dequeued=raya_reaper_dequeue(&s->core);
 if (dequeued!=base) {
  (void)raya_reaper_registry_remove(&s->registry,base);
  (void)raya_reaper_mark_degraded_cause(&s->core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
  return RAYA_INTEGRATED_DEGRADED;
 }
 *out=op;
 return RAYA_INTEGRATED_OK;
}
int raya_reaper_integrated_claim(raya_reaper_integrated *s,
                                 raya_integrated_op *op, HANDLE event_handle) {
 size_t i;
 if (s==NULL || op==NULL || !valid_event(event_handle) ||
     op->base.state!=RAYA_REAPER_OP_WORKER_PENDING ||
     op->collect_state!=RAYA_INTEGRATED_COLLECT_PENDING ||
     op->reservation_state!=RAYA_INTEGRATED_RESERVATION_HANDED_OFF) return 0;
 for (i=0;i<s->registry.count;i++) {
  if (s->registry.entries[i].op==&op->base &&
      s->registry.entries[i].event_handle==event_handle &&
      op->completion_event==event_handle) {
   op->collect_state=RAYA_INTEGRATED_COLLECTING;
   return 1;
  }
 }
 return 0;
}
raya_integrated_result raya_reaper_integrated_collect(
                                 raya_reaper_integrated *s,
                                 raya_integrated_op *op,
                                 raya_integrated_collect_result result) {
 if (s==NULL || op==NULL ||
     op->base.state!=RAYA_REAPER_OP_WORKER_PENDING ||
     op->collect_state!=RAYA_INTEGRATED_COLLECTING ||
     op->reservation_state!=RAYA_INTEGRATED_RESERVATION_HANDED_OFF)
  return RAYA_INTEGRATED_INVALID;
 if (result==RAYA_INTEGRATED_INCOMPLETE) {
  op->collect_state=RAYA_INTEGRATED_COLLECT_PENDING;
  return RAYA_INTEGRATED_OK;
 }
 if (result==RAYA_INTEGRATED_UNCERTAIN_RESULT) {
  op->collect_state=RAYA_INTEGRATED_UNCERTAIN;
  (void)raya_reaper_mark_degraded_cause(
      &s->core,RAYA_RECOVERY_CAUSE_COMPLETION_UNCERTAIN);
  return RAYA_INTEGRATED_DEGRADED;
 }
 if (result!=RAYA_INTEGRATED_TERMINAL_SUCCESS &&
     result!=RAYA_INTEGRATED_TERMINAL_ABORTED)
  return RAYA_INTEGRATED_INVALID;
 {
  size_t i;
  int exact_pair=0;
  for (i=0;i<s->registry.count;i++) {
   if (s->registry.entries[i].op==&op->base &&
       s->registry.entries[i].event_handle==op->completion_event) {
    exact_pair=1;
    break;
   }
  }
  if (!exact_pair || s->reserved_slots==0 || s->core.outstanding==0 ||
      op->base.next!=NULL) {
   (void)raya_reaper_mark_degraded_cause(&s->core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
   return RAYA_INTEGRATED_DEGRADED;
  }
 }
 if (!raya_reaper_registry_remove(&s->registry,&op->base) ||
     !raya_reaper_worker_terminal(&s->core,&op->base)) {
  (void)raya_reaper_mark_degraded_cause(&s->core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
  return RAYA_INTEGRATED_DEGRADED;
 }
 --s->reserved_slots;
 op->reservation_state=RAYA_INTEGRATED_RESERVATION_RELEASED;
 return RAYA_INTEGRATED_OK;
}
raya_integrated_result raya_reaper_integrated_terminal_hold(
                                 raya_reaper_integrated *s,
                                 raya_integrated_op *op,
                                 raya_integrated_collect_result result) {
 if (s==NULL || op==NULL ||
     op->base.state!=RAYA_REAPER_OP_WORKER_PENDING ||
     op->collect_state!=RAYA_INTEGRATED_COLLECTING ||
     op->reservation_state!=RAYA_INTEGRATED_RESERVATION_HANDED_OFF)
  return RAYA_INTEGRATED_INVALID;
 if (result==RAYA_INTEGRATED_INCOMPLETE) {
  op->collect_state=RAYA_INTEGRATED_COLLECT_PENDING;
  return RAYA_INTEGRATED_OK;
 }
 if (result==RAYA_INTEGRATED_UNCERTAIN_RESULT) {
  op->collect_state=RAYA_INTEGRATED_UNCERTAIN;
  (void)raya_reaper_mark_degraded_cause(
      &s->core,RAYA_RECOVERY_CAUSE_COMPLETION_UNCERTAIN);
  return RAYA_INTEGRATED_DEGRADED;
 }
 if (result!=RAYA_INTEGRATED_TERMINAL_SUCCESS &&
     result!=RAYA_INTEGRATED_TERMINAL_ABORTED)
  return RAYA_INTEGRATED_INVALID;
 {
  size_t i;
  int exact_pair=0;
  for (i=0;i<s->registry.count;i++) {
   if (s->registry.entries[i].op==&op->base &&
       s->registry.entries[i].event_handle==op->completion_event) {
    exact_pair=1;
    break;
   }
  }
  if (!exact_pair || s->reserved_slots==0 || s->core.outstanding==0 ||
      op->base.next!=NULL) {
   (void)raya_reaper_mark_degraded_cause(&s->core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
   return RAYA_INTEGRATED_DEGRADED;
  }
 }
 if (!raya_reaper_registry_remove(&s->registry,&op->base) ||
     !raya_reaper_worker_terminal_hold(&s->core,&op->base)) {
  (void)raya_reaper_mark_degraded_cause(&s->core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
  return RAYA_INTEGRATED_DEGRADED;
 }
 op->reservation_state=RAYA_INTEGRATED_RESERVATION_TERMINAL_HELD;
 return RAYA_INTEGRATED_OK;
}
int raya_reaper_integrated_release(raya_reaper_integrated *s,
                                   raya_integrated_op *op) {
 if (s==NULL || op==NULL) return 0;
 if ((s->core.degradation_causes & RAYA_RECOVERY_CAUSE_HANDLE_CLOSE)!=0)
  return 0;
 if (op->reservation_state!=RAYA_INTEGRATED_RESERVATION_TERMINAL_HELD ||
     op->base.state!=RAYA_REAPER_OP_TERMINAL_HELD || s->reserved_slots==0) {
  (void)raya_reaper_mark_degraded_cause(
      &s->core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
  return 0;
 }
 if (!raya_reaper_worker_release(&s->core,&op->base)) {
  (void)raya_reaper_mark_degraded_cause(
      &s->core,RAYA_RECOVERY_CAUSE_OWNERSHIP_INVARIANT);
  return 0;
 }
 --s->reserved_slots;
 op->reservation_state=RAYA_INTEGRATED_RESERVATION_RELEASED;
 return 1;
}
int raya_reaper_integrated_shutdown(raya_reaper_integrated *s) {
 return s!=NULL && raya_reaper_begin_shutdown(&s->core);
}
int raya_reaper_integrated_can_exit(const raya_reaper_integrated *s) {
 return s!=NULL && s->reserved_slots==0 && s->registry.count==0 &&
        raya_reaper_can_exit(&s->core);
}
