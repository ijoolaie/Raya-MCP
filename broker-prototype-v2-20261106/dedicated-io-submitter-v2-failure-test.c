#define WIN32_LEAN_AND_MEAN
#include "dedicated-io-submitter-v2.h"
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { (void)fprintf(stderr,"FAIL line %d: %s err=%lu\n",__LINE__,#x,(unsigned long)GetLastError()); return 1; } } while(0)

typedef struct {
 LONG create_event_calls, create_thread_calls, set_calls, reset_calls;
 LONG wait_calls, close_calls, get_exit_calls, not_issued_calls;
 LONG fail_create_event_at, fail_create_thread_at, fail_set_at;
 LONG fail_set_at2, fail_set_at3;
 LONG fail_reset_at, fail_wait_at, fail_close_at, fail_get_exit_at;
 HANDLE fail_set_handle_once, fail_close_handle_once;
} fault_plan;

static fault_plan g;
static volatile LONG g_issue_uncertain_calls, g_issue_accepted_calls;
static volatile LONG g_publish_noop_calls, g_publish_fail_calls;
static HANDLE WINAPI f_create_event(LPSECURITY_ATTRIBUTES a, BOOL m, BOOL i, LPCWSTR n) {
 LONG call=InterlockedIncrement(&g.create_event_calls);
 if (call==g.fail_create_event_at) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
 return CreateEventW(a,m,i,n);
}
static HANDLE WINAPI f_create_thread(LPSECURITY_ATTRIBUTES a, SIZE_T z,
 LPTHREAD_START_ROUTINE entry, LPVOID p, DWORD flags, LPDWORD id) {
 LONG call=InterlockedIncrement(&g.create_thread_calls);
 if (call==g.fail_create_thread_at) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
 return CreateThread(a,z,entry,p,flags,id);
}
static BOOL WINAPI f_set_event(HANDLE h) {
 LONG call=InterlockedIncrement(&g.set_calls);
 if (call==g.fail_set_at || call==g.fail_set_at2 || call==g.fail_set_at3 ||
     (g.fail_set_handle_once==h && InterlockedCompareExchangePointer((PVOID volatile *)&g.fail_set_handle_once,NULL,h)==h)) {
  SetLastError(ERROR_INVALID_HANDLE); return FALSE;
 }
 return SetEvent(h);
}
static BOOL WINAPI f_reset_event(HANDLE h) {
 LONG call=InterlockedIncrement(&g.reset_calls);
 if (call==g.fail_reset_at) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
 return ResetEvent(h);
}
static DWORD WINAPI f_wait(HANDLE h, DWORD ms) {
 LONG call=InterlockedIncrement(&g.wait_calls);
 if (call==g.fail_wait_at) { SetLastError(ERROR_INVALID_HANDLE); return WAIT_FAILED; }
 return WaitForSingleObject(h,ms);
}
static BOOL WINAPI f_get_exit(HANDLE h, LPDWORD code) {
 LONG call=InterlockedIncrement(&g.get_exit_calls);
 if (call==g.fail_get_exit_at) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
 return GetExitCodeThread(h,code);
}
static BOOL WINAPI f_close(HANDLE h) {
 LONG call=InterlockedIncrement(&g.close_calls);
 if (call==g.fail_close_at ||
     (g.fail_close_handle_once==h && InterlockedCompareExchangePointer((PVOID volatile *)&g.fail_close_handle_once,NULL,h)==h)) {
  SetLastError(ERROR_INVALID_HANDLE); return FALSE;
 }
 return CloseHandle(h);
}
static const raya_submitter_win32_api injected_api = {
 f_create_event,f_create_thread,f_set_event,f_reset_event,f_wait,f_get_exit,f_close
};

static raya_submitter_issue_result issue_noop(raya_submitter_request *r, void *c) {
 (void)r; (void)c; return RAYA_SUBMITTER_ISSUE_NOT_ACCEPTED;
}
static int publish_noop(raya_submitter_request *r, void *c) { (void)r; (void)c; return 1; }
static int publish_noop_counted(raya_submitter_request *r, void *c) { (void)r; (void)c; (void)InterlockedIncrement(&g_publish_noop_calls); return 1; }
static void not_issued_noop(raya_submitter_request *r, void *c) { (void)r; (void)c; }
static void not_issued_counted(raya_submitter_request *r, void *c) {
 (void)r; (void)c; (void)InterlockedIncrement(&g.not_issued_calls);
}

static void cleanup_test_state(raya_dedicated_submitter *s) {
 if (s->thread!=NULL) {
  (void)WaitForSingleObject(s->thread,5000);
  (void)CloseHandle(s->thread);
 }
 if (s->wake_event!=NULL) (void)CloseHandle(s->wake_event);
 if (s->changed_event!=NULL) (void)CloseHandle(s->changed_event);
 if (s->initialized) DeleteCriticalSection(&s->lock);
 ZeroMemory(s,sizeof(*s));
}
static int start_injected(raya_dedicated_submitter *s) {
 return raya_submitter_start_with_api(s,2,&injected_api,issue_noop,
                                      publish_noop,not_issued_noop,NULL);
}

static HANDLE g_issue_entered=NULL;
static HANDLE g_issue_release=NULL;
static raya_submitter_issue_result issue_blocked(raya_submitter_request *r, void *c) {
 (void)r; (void)c;
 if (!SetEvent(g_issue_entered)) return RAYA_SUBMITTER_ISSUE_UNCERTAIN;
 if (WaitForSingleObject(g_issue_release,5000)!=WAIT_OBJECT_0)
  return RAYA_SUBMITTER_ISSUE_UNCERTAIN;
 return RAYA_SUBMITTER_ISSUE_ACCEPTED;
}
static int test_ack_wait_path_failure(int fail_reset) {
 raya_dedicated_submitter s;
 raya_submitter_request req;
 raya_submitter_lifecycle life;
 size_t q=0,p=0,a=0,l=0;
 LONG before;
 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s)); ZeroMemory(&req,sizeof(req));
 g_issue_entered=CreateEventW(NULL,TRUE,FALSE,NULL);
 g_issue_release=CreateEventW(NULL,TRUE,FALSE,NULL);
 CHECK(g_issue_entered!=NULL && g_issue_release!=NULL);
 CHECK(raya_submitter_start_with_api(&s,2,&injected_api,issue_blocked,
                                     publish_noop,not_issued_noop,NULL));
 CHECK(raya_submitter_admit(&s,&req,NULL));
 CHECK(raya_submitter_enqueue(&s,&req)==RAYA_SUBMITTER_OK);
 CHECK(WaitForSingleObject(g_issue_entered,5000)==WAIT_OBJECT_0);
 if (fail_reset) {
  before=InterlockedCompareExchange(&g.reset_calls,0,0);
  g.fail_reset_at=before+1;
 } else {
  before=InterlockedCompareExchange(&g.wait_calls,0,0);
  g.fail_wait_at=before+1;
 }
 CHECK(!raya_submitter_acknowledge(&s,&req));
 CHECK(req.state==RAYA_SUBMITTER_REQ_ISSUING);
 CHECK(SetEvent(g_issue_release));
 CHECK(WaitForSingleObject(s.thread,5000)==WAIT_OBJECT_0);
 CHECK(raya_submitter_snapshot(&s,&life,&q,&p,&a,&l));
 CHECK(life==RAYA_SUBMITTER_DEGRADED);
 CHECK(q==0 && p==0 && a==0 && l==1);
 CHECK(req.state==RAYA_SUBMITTER_REQ_ISSUED);
 CHECK(!raya_submitter_shutdown(&s));
 CHECK(!raya_submitter_join(&s,5000));
 CHECK(s.joined && s.thread==NULL);
 CHECK(!raya_submitter_destroy(&s));
 CHECK(CloseHandle(g_issue_entered)); g_issue_entered=NULL;
 CHECK(CloseHandle(g_issue_release)); g_issue_release=NULL;
 cleanup_test_state(&s);
 return 0;
}
static int test_create_event_failures(void) {
 raya_dedicated_submitter s;
 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s));
 g.fail_create_event_at=1;
 CHECK(!start_injected(&s));
 CHECK(!s.initialized && s.wake_event==NULL && s.changed_event==NULL);

 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s));
 g.fail_create_event_at=2;
 CHECK(!start_injected(&s));
 CHECK(!s.initialized && s.wake_event==NULL && s.changed_event==NULL);
 CHECK(g.close_calls==1);

 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s));
 g.fail_create_event_at=2; g.fail_close_at=1;
 CHECK(!start_injected(&s));
 CHECK(s.initialized && s.lifecycle==RAYA_SUBMITTER_DEGRADED);
 CHECK(s.degraded_reason==RAYA_SUBMITTER_DEGRADED_REASON_STARTUP_ROLLBACK);
 CHECK(s.degraded_first_error==ERROR_INVALID_HANDLE);
 CHECK(s.degraded_from_lifecycle==(raya_submitter_lifecycle)0);
 CHECK(s.wake_event!=NULL && s.changed_event==NULL && !s.started);
 cleanup_test_state(&s);
 return 0;
}
static int test_create_thread_failure(void) {
 raya_dedicated_submitter s;
 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s));
 g.fail_create_thread_at=1; g.fail_close_at=1;
 CHECK(!start_injected(&s));
 CHECK(s.initialized && s.lifecycle==RAYA_SUBMITTER_DEGRADED);
 CHECK(!s.started && s.thread==NULL);
 CHECK(s.wake_event!=NULL && s.changed_event==NULL);
 CHECK(s.degraded_reason==RAYA_SUBMITTER_DEGRADED_REASON_STARTUP_ROLLBACK);
 CHECK(s.degraded_first_error==ERROR_INVALID_HANDLE);
 CHECK(s.degraded_from_lifecycle==RAYA_SUBMITTER_RUNNING);
 CHECK(g.close_calls==2);
 cleanup_test_state(&s);
 return 0;
}
static int wait_for_counter(volatile LONG *counter, LONG expected) {
 DWORD start=GetTickCount();
 while (InterlockedCompareExchange(counter,0,0)<expected) {
  if (GetTickCount()-start>5000) return 0;
  Sleep(1);
 }
 return 1;
}
static int test_set_event_failure(void) {
 raya_dedicated_submitter s;
 raya_submitter_lifecycle life;
 size_t q=0,p=0,a=0,l=0;
 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s));
 CHECK(start_injected(&s));
 CHECK(wait_for_counter(&g.wait_calls,1));
 g.fail_set_at=1;
 CHECK(!raya_submitter_shutdown(&s));
 CHECK(WaitForSingleObject(s.changed_event,5000)==WAIT_OBJECT_0);
 CHECK(raya_submitter_snapshot(&s,&life,&q,&p,&a,&l));
 CHECK(life==RAYA_SUBMITTER_DEGRADED && q==0 && p==0 && a==0 && l==0);
 CHECK(SetEvent(s.wake_event));
 CHECK(!raya_submitter_join(&s,5000));
 CHECK(s.joined && s.thread==NULL && s.lifecycle==RAYA_SUBMITTER_DEGRADED);
 cleanup_test_state(&s);
 return 0;
}
static int test_degrade_double_signal_failure(void) {
 raya_dedicated_submitter s;
 raya_submitter_lifecycle life;
 size_t q=0,p=0,a=0,l=0;
 LONG before;
 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s));
 CHECK(start_injected(&s));
 CHECK(wait_for_counter(&g.wait_calls,1));
 before=InterlockedCompareExchange(&g.set_calls,0,0);
 g.fail_set_at=before+1;
 g.fail_set_at2=before+2;
 g.fail_set_at3=before+3;
 CHECK(!raya_submitter_shutdown(&s));
 CHECK(g.set_calls==before+3);
 CHECK(s.lifecycle==RAYA_SUBMITTER_DEGRADED);
 CHECK(s.degraded_reason==RAYA_SUBMITTER_DEGRADED_REASON_NOTIFICATION);
  CHECK(s.degraded_first_error==ERROR_INVALID_HANDLE);
 CHECK(s.degraded_from_lifecycle==RAYA_SUBMITTER_STOPPING);
 CHECK(s.degraded_queue_count==0 && s.degraded_active_producers==0);
 CHECK(s.degraded_active_issue==0 && s.degraded_issuer_leases==0);
 CHECK(WaitForSingleObject(s.wake_event,0)==WAIT_TIMEOUT);
 CHECK(WaitForSingleObject(s.changed_event,0)==WAIT_TIMEOUT);
 {
  HANDLE retained_thread=s.thread, retained_wake=s.wake_event;
  HANDLE retained_changed=s.changed_event;
  CHECK(!raya_submitter_join(&s,25));
  CHECK(!s.joined && s.thread==retained_thread);
  CHECK(s.wake_event==retained_wake && s.changed_event==retained_changed);
  CHECK(WaitForSingleObject(s.thread,0)==WAIT_TIMEOUT);
 }
 CHECK(raya_submitter_snapshot(&s,&life,&q,&p,&a,&l));
 CHECK(life==RAYA_SUBMITTER_DEGRADED && q==0 && p==0 && a==0 && l==0);
 /* Test-only recovery signal: prove the blocked thread exits after observing DEGRADED. */
 CHECK(SetEvent(s.wake_event));
 CHECK(WaitForSingleObject(s.thread,5000)==WAIT_OBJECT_0);
 CHECK(!raya_submitter_join(&s,5000));
 CHECK(s.joined && s.thread==NULL);
 CHECK(!raya_submitter_destroy(&s));
 cleanup_test_state(&s);
 return 0;
}
static int test_reset_event_failure(void) {
 raya_dedicated_submitter s;
 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s));
 g.fail_reset_at=1;
 CHECK(start_injected(&s));
 CHECK(WaitForSingleObject(s.changed_event,5000)==WAIT_OBJECT_0);
 CHECK(s.lifecycle==RAYA_SUBMITTER_DEGRADED);
 CHECK(s.degraded_reason==RAYA_SUBMITTER_DEGRADED_REASON_WAKE_RESET);
 CHECK(s.degraded_first_error==ERROR_INVALID_HANDLE);
 CHECK(!raya_submitter_join(&s,5000));
 CHECK(s.joined && s.thread==NULL);
 cleanup_test_state(&s);
 return 0;
}
static int test_wait_failure(void) {
 raya_dedicated_submitter s;
 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s));
 g.fail_wait_at=1;
 CHECK(start_injected(&s));
 CHECK(WaitForSingleObject(s.changed_event,5000)==WAIT_OBJECT_0);
 CHECK(s.lifecycle==RAYA_SUBMITTER_DEGRADED);
 CHECK(s.degraded_reason==RAYA_SUBMITTER_DEGRADED_REASON_WAKE_WAIT);
 CHECK(s.degraded_first_error==ERROR_INVALID_HANDLE);
 CHECK(!raya_submitter_join(&s,5000));
 CHECK(s.joined && s.thread==NULL);
 cleanup_test_state(&s);
 return 0;
}
static int wait_for_stopped(raya_dedicated_submitter *s) {
 DWORD start=GetTickCount();
 raya_submitter_lifecycle life;
 size_t q,p,a,l;
 do {
  if (!raya_submitter_snapshot(s,&life,&q,&p,&a,&l)) return 0;
  if (life==RAYA_SUBMITTER_STOPPED) return 1;
  Sleep(1);
 } while (GetTickCount()-start<5000);
 return 0;
}
static int test_join_wait_failure(void) {
 raya_dedicated_submitter s;
 raya_submitter_lifecycle life;
 size_t q=0,p=0,a=0,l=0;
 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s));
 g.fail_wait_at=2;
 CHECK(start_injected(&s));
 CHECK(wait_for_counter(&g.wait_calls,1));
 CHECK(raya_submitter_shutdown(&s));
 CHECK(wait_for_stopped(&s));
 CHECK(raya_submitter_snapshot(&s,&life,&q,&p,&a,&l));
 CHECK(life==RAYA_SUBMITTER_STOPPED && q==0 && p==0 && a==0 && l==0);
 CHECK(!raya_submitter_join(&s,5000));
 CHECK(!s.joined && s.thread!=NULL);
 CHECK(raya_submitter_join(&s,5000));
 CHECK(s.joined && s.thread==NULL);
 CHECK(raya_submitter_destroy(&s));
 return 0;
}
static int test_destroy_close_failure(void) {
 raya_dedicated_submitter s;
 raya_submitter_op_result result;
 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s));
 CHECK(start_injected(&s));
 CHECK(wait_for_counter(&g.wait_calls,1));
 CHECK(raya_submitter_shutdown(&s));
 CHECK(WaitForSingleObject(s.changed_event,5000)==WAIT_OBJECT_0);
 CHECK(raya_submitter_join(&s,5000));
 g.fail_close_at=InterlockedCompareExchange(&g.close_calls,0,0)+1;
 CHECK(raya_submitter_destroy_ex(&s,&result)==RAYA_SUBMITTER_OP_RETRYABLE_FAILURE);
 CHECK(result.retry==RAYA_SUBMITTER_RETRY_SAME_OPERATION);
 CHECK(result.effect==RAYA_SUBMITTER_EFFECT_NONE);
 CHECK(s.destroy_progress==3 && s.wake_event!=NULL && s.changed_event!=NULL);
 CHECK(s.degraded_reason==RAYA_SUBMITTER_DEGRADED_REASON_DESTROY_CLOSE);
 CHECK(s.degraded_first_error==ERROR_INVALID_HANDLE);
 CHECK(s.degraded_from_lifecycle==RAYA_SUBMITTER_STOPPED);
 CHECK(raya_submitter_destroy_ex(&s,&result)==RAYA_SUBMITTER_OP_SUCCESS);
 CHECK(result.effect==RAYA_SUBMITTER_EFFECT_DESTROYED);
 CHECK(!s.initialized);
 return 0;
}
static raya_submitter_issue_result issue_accepted(raya_submitter_request *r, void *c) {
 (void)r; (void)c; return RAYA_SUBMITTER_ISSUE_ACCEPTED;
}
static raya_submitter_issue_result issue_accepted_counted(raya_submitter_request *r, void *c) { (void)r; (void)c; (void)InterlockedIncrement(&g_issue_accepted_calls); return RAYA_SUBMITTER_ISSUE_ACCEPTED; }

static int wait_for_request_state(raya_dedicated_submitter *s,
 raya_submitter_request *r, raya_submitter_request_state expected) {
 DWORD start=GetTickCount();
 do {
  int matched;
  EnterCriticalSection(&s->lock);
  matched=(r->state==expected);
  LeaveCriticalSection(&s->lock);
  if (matched) return 1;
  Sleep(1);
 } while (GetTickCount()-start<5000);
 return 0;
}
static int publish_fail(raya_submitter_request *r, void *c) {
 (void)r; (void)c; return 0;
}
static int publish_fail_counted(raya_submitter_request *r, void *c) { (void)r; (void)c; (void)InterlockedIncrement(&g_publish_fail_calls); return 0; }

static raya_submitter_issue_result issue_uncertain_now(raya_submitter_request *r, void *c) {
 (void)r; (void)c; (void)InterlockedIncrement(&g_issue_uncertain_calls); return RAYA_SUBMITTER_ISSUE_UNCERTAIN;
}
static raya_submitter_issue_result issue_blocked_uncertain_counted(raya_submitter_request *r, void *c) {
 (void)r; (void)c; (void)InterlockedIncrement(&g_issue_uncertain_calls);
 if (!SetEvent(g_issue_entered)) return RAYA_SUBMITTER_ISSUE_UNCERTAIN;
 (void)WaitForSingleObject(g_issue_release,5000);
 return RAYA_SUBMITTER_ISSUE_UNCERTAIN;
}
static int test_queued_request_remains_quarantined_after_uncertain_issue(void) {
 raya_dedicated_submitter s;
 raya_submitter_request req1,req2;
 raya_submitter_lifecycle life;
 size_t q=0,p=0,a=0,l=0;
 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s));
 ZeroMemory(&req1,sizeof(req1)); ZeroMemory(&req2,sizeof(req2));
 g_issue_uncertain_calls=0; g_publish_noop_calls=0;
 g_issue_entered=CreateEventW(NULL,TRUE,FALSE,NULL);
 g_issue_release=CreateEventW(NULL,TRUE,FALSE,NULL);
 CHECK(g_issue_entered!=NULL && g_issue_release!=NULL);
 CHECK(raya_submitter_start_with_api(&s,2,&injected_api,issue_blocked_uncertain_counted,
                                     publish_noop_counted,not_issued_counted,NULL));
 CHECK(raya_submitter_admit(&s,&req1,NULL));
 CHECK(raya_submitter_admit(&s,&req2,NULL));
 CHECK(req1.request_id==1 && req2.request_id==2);
 CHECK(raya_submitter_enqueue(&s,&req1)==RAYA_SUBMITTER_OK);
 CHECK(WaitForSingleObject(g_issue_entered,5000)==WAIT_OBJECT_0);
 CHECK(raya_submitter_enqueue(&s,&req2)==RAYA_SUBMITTER_OK);
 CHECK(req2.state==RAYA_SUBMITTER_REQ_QUEUED);
 CHECK(raya_submitter_snapshot(&s,&life,&q,&p,&a,&l));
 CHECK(life==RAYA_SUBMITTER_RUNNING && q==1 && p==0 && a==1 && l==1);
 CHECK(SetEvent(g_issue_release));
 CHECK(WaitForSingleObject(s.thread,5000)==WAIT_OBJECT_0);
 CHECK(req1.state==RAYA_SUBMITTER_REQ_UNCERTAIN);
 CHECK(req1.issue_effect==RAYA_SUBMITTER_EFFECT_UNKNOWN);
 CHECK(req1.publish_effect==RAYA_SUBMITTER_EFFECT_NOT_ATTEMPTED);
 CHECK(req2.state==RAYA_SUBMITTER_REQ_QUEUED);
 CHECK(raya_submitter_snapshot(&s,&life,&q,&p,&a,&l));
 CHECK(life==RAYA_SUBMITTER_DEGRADED && q==1 && p==0 && a==0 && l==1);
 CHECK(s.degraded_queue_count==1 && s.degraded_active_issue==0 && s.degraded_issuer_leases==1);
 CHECK(g_issue_uncertain_calls==1 && g_publish_noop_calls==0);
 CHECK(g.not_issued_calls==0);
 cleanup_test_state(&s);
 CloseHandle(g_issue_entered); CloseHandle(g_issue_release);
 g_issue_entered=NULL; g_issue_release=NULL;
 return 0;
}
static int test_request_identity_and_effect_commit(void) {
 raya_dedicated_submitter s;
 raya_submitter_request req;
 raya_submitter_request req2;
 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s)); ZeroMemory(&req,sizeof(req));
 ZeroMemory(&req2,sizeof(req2));
 CHECK(raya_submitter_start_with_api(&s,2,&injected_api,issue_accepted,
                                     publish_noop,not_issued_noop,NULL));
 CHECK(raya_submitter_admit(&s,&req,NULL));
 CHECK(req.request_id==1);
 CHECK(req.issue_effect==RAYA_SUBMITTER_EFFECT_NOT_ATTEMPTED);
 CHECK(req.publish_effect==RAYA_SUBMITTER_EFFECT_NOT_ATTEMPTED);
 CHECK(raya_submitter_enqueue(&s,&req)==RAYA_SUBMITTER_OK);
 CHECK(wait_for_request_state(&s,&req,RAYA_SUBMITTER_REQ_ISSUED));
 CHECK(req.issue_effect==RAYA_SUBMITTER_EFFECT_COMMITTED);
 CHECK(req.publish_effect==RAYA_SUBMITTER_EFFECT_COMMITTED);
 CHECK(s.degraded_request_id==0);
 CHECK(raya_submitter_acknowledge(&s,&req));
 CHECK(raya_submitter_admit(&s,&req2,NULL));
 CHECK(req2.request_id==2);
 CHECK(raya_submitter_abort_admission(&s,&req2));
 CHECK(raya_submitter_shutdown(&s));
 CHECK(WaitForSingleObject(s.thread,5000)==WAIT_OBJECT_0);
 CHECK(raya_submitter_join(&s,5000));
 cleanup_test_state(&s);
 return 0;
}
static HANDLE g_publish_entered=NULL;
static HANDLE g_publish_release=NULL;
static int publish_blocked_fail_counted(raya_submitter_request *r, void *c) {
 (void)r; (void)c; (void)InterlockedIncrement(&g_publish_fail_calls);
 if (!SetEvent(g_publish_entered)) return 0;
 if (WaitForSingleObject(g_publish_release,5000)!=WAIT_OBJECT_0) return 0;
 return 0;
}
static int test_queued_request_remains_quarantined_after_uncertain_publish(void) {
 raya_dedicated_submitter s;
 raya_submitter_request req1,req2;
 raya_submitter_lifecycle life;
 size_t q=0,p=0,a=0,l=0;
 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s));
 ZeroMemory(&req1,sizeof(req1)); ZeroMemory(&req2,sizeof(req2));
 g_issue_accepted_calls=0; g_publish_fail_calls=0;
 g_publish_entered=CreateEventW(NULL,TRUE,FALSE,NULL);
 g_publish_release=CreateEventW(NULL,TRUE,FALSE,NULL);
 CHECK(g_publish_entered!=NULL && g_publish_release!=NULL);
 CHECK(raya_submitter_start_with_api(&s,2,&injected_api,issue_accepted_counted,
                                     publish_blocked_fail_counted,not_issued_counted,NULL));
 CHECK(raya_submitter_admit(&s,&req1,NULL));
 CHECK(raya_submitter_admit(&s,&req2,NULL));
 CHECK(req1.request_id==1 && req2.request_id==2);
 CHECK(raya_submitter_enqueue(&s,&req1)==RAYA_SUBMITTER_OK);
 CHECK(WaitForSingleObject(g_publish_entered,5000)==WAIT_OBJECT_0);
 CHECK(req1.issue_effect==RAYA_SUBMITTER_EFFECT_COMMITTED);
 CHECK(req1.publish_effect==RAYA_SUBMITTER_EFFECT_IN_PROGRESS);
 CHECK(raya_submitter_enqueue(&s,&req2)==RAYA_SUBMITTER_OK);
 CHECK(req2.state==RAYA_SUBMITTER_REQ_QUEUED);
 CHECK(raya_submitter_snapshot(&s,&life,&q,&p,&a,&l));
 CHECK(life==RAYA_SUBMITTER_RUNNING && q==1 && p==0 && a==1 && l==1);
 CHECK(SetEvent(g_publish_release));
 CHECK(WaitForSingleObject(s.thread,5000)==WAIT_OBJECT_0);
 CHECK(req1.state==RAYA_SUBMITTER_REQ_UNCERTAIN);
 CHECK(req1.issue_effect==RAYA_SUBMITTER_EFFECT_COMMITTED);
 CHECK(req1.publish_effect==RAYA_SUBMITTER_EFFECT_UNKNOWN);
 CHECK(req2.state==RAYA_SUBMITTER_REQ_QUEUED);
 CHECK(raya_submitter_snapshot(&s,&life,&q,&p,&a,&l));
 CHECK(life==RAYA_SUBMITTER_DEGRADED && q==1 && p==0 && a==0 && l==1);
 CHECK(s.degraded_queue_count==1 && s.degraded_active_issue==0 && s.degraded_issuer_leases==1);
 CHECK(g_issue_accepted_calls==1 && g_publish_fail_calls==1);
 CHECK(g.not_issued_calls==0);
 cleanup_test_state(&s);
 CloseHandle(g_publish_entered); CloseHandle(g_publish_release);
 g_publish_entered=NULL; g_publish_release=NULL;
 return 0;
}static int test_publish_failure_effect_ledger(void) {
 raya_dedicated_submitter s;
 raya_submitter_request req;
 raya_submitter_request req2;
 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s)); ZeroMemory(&req,sizeof(req)); ZeroMemory(&req2,sizeof(req2));
 g_issue_accepted_calls=0; g_publish_fail_calls=0;
 CHECK(raya_submitter_start_with_api(&s,2,&injected_api,issue_accepted_counted,
                                     publish_fail_counted,not_issued_counted,NULL));
 CHECK(raya_submitter_admit(&s,&req,NULL));
 CHECK(req.request_id==1);
 CHECK(raya_submitter_enqueue(&s,&req)==RAYA_SUBMITTER_OK);
 CHECK(WaitForSingleObject(s.thread,5000)==WAIT_OBJECT_0);
 CHECK(req.state==RAYA_SUBMITTER_REQ_UNCERTAIN);
 CHECK(req.issue_effect==RAYA_SUBMITTER_EFFECT_COMMITTED);
 CHECK(req.publish_effect==RAYA_SUBMITTER_EFFECT_UNKNOWN);
 CHECK(s.degraded_reason==RAYA_SUBMITTER_DEGRADED_REASON_PUBLISH_UNCERTAIN);
 CHECK(s.degraded_request_id==req.request_id);
 CHECK(s.degraded_issue_effect==RAYA_SUBMITTER_EFFECT_COMMITTED);
 CHECK(s.degraded_publish_effect==RAYA_SUBMITTER_EFFECT_UNKNOWN);
 CHECK(g_issue_accepted_calls==1);
 CHECK(g_publish_fail_calls==1);
 CHECK(g.not_issued_calls==0);
 CHECK(s.lifecycle==RAYA_SUBMITTER_DEGRADED);
 CHECK(!raya_submitter_admit(&s,&req2,NULL));
 CHECK(req2.request_id==0 && req2.state==RAYA_SUBMITTER_REQ_BUILDING);
 CHECK(g_issue_accepted_calls==1 && g_publish_fail_calls==1);
 cleanup_test_state(&s);
 return 0;
}
static int test_issue_uncertain_effect_ledger(void) {
 raya_dedicated_submitter s;
 raya_submitter_request req;
 raya_submitter_request req2;
 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s)); ZeroMemory(&req,sizeof(req)); ZeroMemory(&req2,sizeof(req2));
 g_issue_uncertain_calls=0; g_publish_noop_calls=0;
 CHECK(raya_submitter_start_with_api(&s,2,&injected_api,issue_uncertain_now,
                                     publish_noop_counted,not_issued_counted,NULL));
 CHECK(raya_submitter_admit(&s,&req,NULL));
 CHECK(req.request_id==1);
 CHECK(raya_submitter_enqueue(&s,&req)==RAYA_SUBMITTER_OK);
 CHECK(WaitForSingleObject(s.thread,5000)==WAIT_OBJECT_0);
 CHECK(req.state==RAYA_SUBMITTER_REQ_UNCERTAIN);
 CHECK(req.issue_effect==RAYA_SUBMITTER_EFFECT_UNKNOWN);
 CHECK(req.publish_effect==RAYA_SUBMITTER_EFFECT_NOT_ATTEMPTED);
 CHECK(s.degraded_reason==RAYA_SUBMITTER_DEGRADED_REASON_ISSUE_UNCERTAIN);
 CHECK(s.degraded_request_id==req.request_id);
 CHECK(s.degraded_issue_effect==RAYA_SUBMITTER_EFFECT_UNKNOWN);
 CHECK(g_issue_uncertain_calls==1);
 CHECK(g_publish_noop_calls==0);
 CHECK(g.not_issued_calls==0);
 CHECK(s.lifecycle==RAYA_SUBMITTER_DEGRADED);
 CHECK(!raya_submitter_admit(&s,&req2,NULL));
 CHECK(req2.request_id==0 && req2.state==RAYA_SUBMITTER_REQ_BUILDING);
 CHECK(g_issue_uncertain_calls==1 && g_publish_noop_calls==0);
 cleanup_test_state(&s);
 return 0;
}
static int test_enqueue_set_failure(void) {
 raya_dedicated_submitter s;
 raya_submitter_request req;
 raya_submitter_lifecycle life;
 size_t q=0,p=0,a=0,l=0;
 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s)); ZeroMemory(&req,sizeof(req));
 CHECK(start_injected(&s));
 CHECK(wait_for_counter(&g.wait_calls,1));
 CHECK(raya_submitter_admit(&s,&req,NULL));
 g.fail_set_at=InterlockedCompareExchange(&g.set_calls,0,0)+1;
 CHECK(raya_submitter_enqueue(&s,&req)==RAYA_SUBMITTER_RETAINED);
 CHECK(raya_submitter_snapshot(&s,&life,&q,&p,&a,&l));
 CHECK(life==RAYA_SUBMITTER_DEGRADED && q==1 && p==0);
 CHECK(WaitForSingleObject(s.thread,5000)==WAIT_OBJECT_0);
 CHECK(!raya_submitter_join(&s,5000));
 cleanup_test_state(&s);
 return 0;
}
static int test_abort_set_failure(void) {
 raya_dedicated_submitter s;
 raya_submitter_request req;
 raya_submitter_lifecycle life;
 size_t q=0,p=0,a=0,l=0;
 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s)); ZeroMemory(&req,sizeof(req));
 CHECK(start_injected(&s));
 CHECK(wait_for_counter(&g.wait_calls,1));
 CHECK(raya_submitter_admit(&s,&req,NULL));
 g.fail_set_at=InterlockedCompareExchange(&g.set_calls,0,0)+1;
 CHECK(!raya_submitter_abort_admission(&s,&req));
 CHECK(raya_submitter_snapshot(&s,&life,&q,&p,&a,&l));
 CHECK(life==RAYA_SUBMITTER_DEGRADED && q==0 && p==0);
 CHECK(req.state==RAYA_SUBMITTER_REQ_NOT_ISSUED);
 CHECK(WaitForSingleObject(s.thread,5000)==WAIT_OBJECT_0);
 CHECK(!raya_submitter_join(&s,5000));
 cleanup_test_state(&s);
 return 0;
}
static int test_ack_invariant_failure(void) {
 raya_dedicated_submitter s;
 raya_submitter_request req;
 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s)); ZeroMemory(&req,sizeof(req));
 CHECK(raya_submitter_start_with_api(&s,2,&injected_api,issue_accepted,
                                     publish_noop,not_issued_noop,NULL));
 CHECK(raya_submitter_admit(&s,&req,NULL));
 CHECK(raya_submitter_enqueue(&s,&req)==RAYA_SUBMITTER_OK);
 CHECK(wait_for_request_state(&s,&req,RAYA_SUBMITTER_REQ_ISSUED));
 EnterCriticalSection(&s.lock);
 s.issuer_leases=0;
 LeaveCriticalSection(&s.lock);
 CHECK(!raya_submitter_acknowledge(&s,&req));
 CHECK(s.lifecycle==RAYA_SUBMITTER_DEGRADED);
 CHECK(s.degraded_reason==RAYA_SUBMITTER_DEGRADED_REASON_ACK_INVARIANT);
 CHECK(s.degraded_first_error==ERROR_INVALID_DATA);
 CHECK(WaitForSingleObject(s.thread,5000)==WAIT_OBJECT_0);
 CHECK(!raya_submitter_join(&s,5000));
 cleanup_test_state(&s);
 return 0;
}
static int test_acknowledge_set_failure(void) {
 raya_dedicated_submitter s;
 raya_submitter_request req;
 raya_submitter_lifecycle life;
 size_t q=0,p=0,a=0,l=0;
 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s)); ZeroMemory(&req,sizeof(req));
 CHECK(raya_submitter_start_with_api(&s,2,&injected_api,issue_accepted,
                                     publish_noop,not_issued_noop,NULL));
 CHECK(raya_submitter_admit(&s,&req,NULL));
 CHECK(raya_submitter_enqueue(&s,&req)==RAYA_SUBMITTER_OK);
 CHECK(wait_for_request_state(&s,&req,RAYA_SUBMITTER_REQ_ISSUED));
 g.fail_set_at=InterlockedCompareExchange(&g.set_calls,0,0)+1;
 CHECK(!raya_submitter_acknowledge(&s,&req));
 CHECK(raya_submitter_snapshot(&s,&life,&q,&p,&a,&l));
 CHECK(life==RAYA_SUBMITTER_DEGRADED && l==0 && a==0);
 CHECK(req.state==RAYA_SUBMITTER_REQ_ACKED);
 {
  LONG set_calls=InterlockedCompareExchange(&g.set_calls,0,0);
  CHECK(!raya_submitter_acknowledge(&s,&req));
  CHECK(req.state==RAYA_SUBMITTER_REQ_ACKED);
  CHECK(InterlockedCompareExchange(&g.set_calls,0,0)==set_calls);
  CHECK(raya_submitter_snapshot(&s,&life,&q,&p,&a,&l));
  CHECK(life==RAYA_SUBMITTER_DEGRADED && q==0 && p==0 && a==0 && l==0);
 }
 CHECK(WaitForSingleObject(s.thread,5000)==WAIT_OBJECT_0);
 CHECK(!raya_submitter_join(&s,5000));
 cleanup_test_state(&s);
 return 0;
}
static int test_get_exit_code_failure(void) {
 raya_dedicated_submitter s;
 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s));
 CHECK(start_injected(&s));
 CHECK(wait_for_counter(&g.wait_calls,1));
 CHECK(raya_submitter_shutdown(&s));
 CHECK(wait_for_stopped(&s));
 g.fail_get_exit_at=1;
 CHECK(!raya_submitter_join(&s,5000));
 CHECK(!s.joined && s.thread!=NULL);
 CHECK(g.get_exit_calls==1);
 CHECK(raya_submitter_join(&s,5000));
 CHECK(s.joined && s.thread==NULL);
 CHECK(g.get_exit_calls==2);
 CHECK(raya_submitter_destroy(&s));
 return 0;
}
static int test_not_accepted_notification_failure(void) {
 raya_dedicated_submitter s;
 raya_submitter_request req;
 raya_submitter_lifecycle life;
 size_t q=0,p=0,a=0,l=0;
 LONG before;
 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s)); ZeroMemory(&req,sizeof(req));
 CHECK(raya_submitter_start_with_api(&s,2,&injected_api,issue_noop,
                                     publish_noop,not_issued_counted,NULL));
 CHECK(wait_for_counter(&g.wait_calls,1));
 CHECK(raya_submitter_admit(&s,&req,NULL));
 before=InterlockedCompareExchange(&g.set_calls,0,0);
 g.fail_set_at=before+3;
 CHECK(raya_submitter_enqueue(&s,&req)==RAYA_SUBMITTER_OK);
 CHECK(WaitForSingleObject(s.thread,5000)==WAIT_OBJECT_0);
 CHECK(raya_submitter_snapshot(&s,&life,&q,&p,&a,&l));
 CHECK(life==RAYA_SUBMITTER_DEGRADED && q==0 && p==0 && a==0 && l==0);
 CHECK(req.state==RAYA_SUBMITTER_REQ_NOT_ISSUED);
 CHECK(InterlockedCompareExchange(&g.not_issued_calls,0,0)==1);
 CHECK(!raya_submitter_join(&s,5000));
 CHECK(s.joined && s.thread==NULL);
 cleanup_test_state(&s);
 return 0;
}
static int test_stopped_notification_failure(void) {
 raya_dedicated_submitter s;
 raya_submitter_lifecycle life;
 size_t q=0,p=0,a=0,l=0;
 LONG before;
 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s));
 CHECK(start_injected(&s));
 CHECK(wait_for_counter(&g.wait_calls,1));
 before=InterlockedCompareExchange(&g.set_calls,0,0);
 g.fail_set_at=before+3;
 CHECK(raya_submitter_shutdown(&s));
 CHECK(WaitForSingleObject(s.thread,5000)==WAIT_OBJECT_0);
 CHECK(raya_submitter_snapshot(&s,&life,&q,&p,&a,&l));
 CHECK(life==RAYA_SUBMITTER_DEGRADED && q==0 && p==0 && a==0 && l==0);
 CHECK(!raya_submitter_join(&s,5000));
 CHECK(s.joined && s.thread==NULL);
 cleanup_test_state(&s);
 return 0;
}
static int test_shutdown_changed_event_failure(void) {
 raya_dedicated_submitter s;
 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s));
 CHECK(start_injected(&s));
 CHECK(wait_for_counter(&g.wait_calls,1));
 g.fail_set_handle_once=s.changed_event;
 CHECK(!raya_submitter_shutdown(&s));
 CHECK(s.lifecycle==RAYA_SUBMITTER_DEGRADED);
  CHECK(s.degraded_reason==RAYA_SUBMITTER_DEGRADED_REASON_NOTIFICATION);
  CHECK(s.degraded_first_error==ERROR_INVALID_HANDLE);
 CHECK(WaitForSingleObject(s.thread,5000)==WAIT_OBJECT_0);
 CHECK(!raya_submitter_join(&s,5000));
 CHECK(s.joined && s.thread==NULL);
 CHECK(g.fail_set_handle_once==NULL);
 cleanup_test_state(&s);
 return 0;
}
static int test_join_thread_close_failure(void) {
 raya_dedicated_submitter s;
 HANDLE thread;
 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s));
 CHECK(start_injected(&s));
 CHECK(wait_for_counter(&g.wait_calls,1));
 CHECK(raya_submitter_shutdown(&s));
 CHECK(wait_for_stopped(&s));
 thread=s.thread;
 g.fail_close_handle_once=thread;
 CHECK(!raya_submitter_join(&s,5000));
 CHECK(!s.joined && s.thread==thread);
 CHECK(g.fail_close_handle_once==NULL);
 CHECK(raya_submitter_join(&s,5000));
 CHECK(s.joined && s.thread==NULL);
 CHECK(raya_submitter_destroy(&s));
 return 0;
}
static int test_destroy_second_event_close_failure(void) {
 raya_dedicated_submitter s;
 HANDLE wake,changed;
 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s));
 CHECK(start_injected(&s));
 CHECK(wait_for_counter(&g.wait_calls,1));
 CHECK(raya_submitter_shutdown(&s));
 CHECK(wait_for_stopped(&s));
 CHECK(raya_submitter_join(&s,5000));
 wake=s.wake_event; changed=s.changed_event;
 g.fail_close_handle_once=changed;
 CHECK(!raya_submitter_destroy(&s));
 CHECK(s.initialized && s.joined);
 CHECK(s.wake_event==NULL && s.changed_event==changed);
 CHECK(s.lifecycle==RAYA_SUBMITTER_DEGRADED);
 CHECK(g.fail_close_handle_once==NULL);
 CHECK(wake!=NULL);
 cleanup_test_state(&s);
 return 0;
}
static int test_destroy_partial_retry_ex(void) {
 raya_dedicated_submitter s;
 raya_submitter_op_result result;
 HANDLE changed;
 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s));
 CHECK(start_injected(&s));
 CHECK(wait_for_counter(&g.wait_calls,1));
 CHECK(raya_submitter_shutdown(&s));
 CHECK(wait_for_stopped(&s));
 CHECK(raya_submitter_join(&s,5000));
 changed=s.changed_event;
 g.fail_close_handle_once=changed;
 CHECK(raya_submitter_destroy_ex(&s,&result)==RAYA_SUBMITTER_OP_RETRYABLE_FAILURE);
 CHECK(result.effect==RAYA_SUBMITTER_EFFECT_PARTIAL_DESTROY);
 CHECK(result.retry==RAYA_SUBMITTER_RETRY_SAME_OPERATION);
 CHECK(result.step==4 && result.win32_error==ERROR_INVALID_HANDLE);
 CHECK(!result.wake_event_present && result.changed_event_present);
 CHECK(s.destroy_progress==1 && s.wake_event==NULL && s.changed_event==changed);
 CHECK(s.degraded_reason==RAYA_SUBMITTER_DEGRADED_REASON_DESTROY_CLOSE);
 CHECK(s.degraded_first_error==ERROR_INVALID_HANDLE);
 CHECK(s.degraded_from_lifecycle==RAYA_SUBMITTER_STOPPED);
 CHECK(raya_submitter_destroy_ex(&s,&result)==RAYA_SUBMITTER_OP_SUCCESS);
 CHECK(result.effect==RAYA_SUBMITTER_EFFECT_DESTROYED);
 CHECK(result.retry==RAYA_SUBMITTER_RETRY_NONE);
 CHECK(!result.wake_event_present && !result.changed_event_present);
 CHECK(!s.initialized && s.destroy_progress==2);
 return 0;
}
static int test_join_abnormal_effect_ex(void) {
 raya_dedicated_submitter s;
 raya_submitter_op_result result;
 ZeroMemory(&g,sizeof(g)); ZeroMemory(&s,sizeof(s));
 g.fail_reset_at=1;
 CHECK(start_injected(&s));
 CHECK(WaitForSingleObject(s.changed_event,5000)==WAIT_OBJECT_0);
 CHECK(WaitForSingleObject(s.thread,5000)==WAIT_OBJECT_0);
 CHECK(raya_submitter_join_ex(&s,5000,&result)==RAYA_SUBMITTER_OP_TERMINAL_FAILURE);
 CHECK(result.effect==RAYA_SUBMITTER_EFFECT_THREAD_JOINED);
 CHECK(result.retry==RAYA_SUBMITTER_RETRY_NONE);
 CHECK(!result.thread_present && s.joined);
 CHECK(raya_submitter_join_ex(&s,5000,&result)==RAYA_SUBMITTER_OP_TERMINAL_FAILURE);
 CHECK(result.effect==RAYA_SUBMITTER_EFFECT_THREAD_JOINED);
 CHECK(!result.thread_present);
 cleanup_test_state(&s);
 return 0;
}
int main(void) {
 if (test_create_event_failures()) return 1;
 if (test_create_thread_failure()) return 1;
 if (test_set_event_failure()) return 1;
 if (test_degrade_double_signal_failure()) return 1;
 if (test_reset_event_failure()) return 1;
 if (test_wait_failure()) return 1;
 if (test_join_wait_failure()) return 1;
 if (test_destroy_close_failure()) return 1;
 if (test_shutdown_changed_event_failure()) return 1;
 if (test_join_thread_close_failure()) return 1;
 if (test_destroy_second_event_close_failure()) return 1;
 if (test_destroy_partial_retry_ex()) return 1;
 if (test_join_abnormal_effect_ex()) return 1;
 if (test_get_exit_code_failure()) return 1;
 if (test_queued_request_remains_quarantined_after_uncertain_issue()) return 1;
 if (test_request_identity_and_effect_commit()) return 1;
 if (test_queued_request_remains_quarantined_after_uncertain_publish()) return 1;
 if (test_publish_failure_effect_ledger()) return 1;
 if (test_issue_uncertain_effect_ledger()) return 1;
 if (test_enqueue_set_failure()) return 1;
 if (test_abort_set_failure()) return 1;
 if (test_acknowledge_set_failure()) return 1;
 if (test_ack_invariant_failure()) return 1;
 if (test_not_accepted_notification_failure()) return 1;
 if (test_stopped_notification_failure()) return 1;
 if (test_ack_wait_path_failure(1)) return 1;
 if (test_ack_wait_path_failure(0)) return 1;
 (void)printf("Dedicated Submitter Win32 failure matrix: %u assertions PASS\n",checks);
 return 0;
}
