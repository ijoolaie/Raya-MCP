#include "raya-durable-runtime-v1.h"
#include <stdio.h>
#include <string.h>
static unsigned checks,failures;
#define T(x) do{++checks;if(!(x)){++failures;printf("FAIL %d %s\n",__LINE__,#x);}}while(0)
static volatile LONG active_calls,max_active,issue_calls,handoff_calls;
static int bad_echo;
static volatile LONG block_callback;
static HANDLE callback_entered,callback_release;
static void WINAPI fake_issue(void *ctx,const raya_durable_operation_id_v1 *id,raya_durable_issue_result_v1 *r){
 LONG a,m;(void)ctx; a=InterlockedIncrement(&active_calls);m=max_active;
 while(a>m){LONG old=InterlockedCompareExchange(&max_active,a,m);if(old==m)break;m=old;}
 InterlockedIncrement(&issue_calls); if(InterlockedCompareExchange(&block_callback,0,0)){SetEvent(callback_entered);WaitForSingleObject(callback_release,10000);} else Sleep(3);
 r->struct_size=sizeof(*r);r->schema_version=1;
 r->disposition=RAYA_DURABLE_ISSUE_PROVEN_NOT_SUBMITTED;
 r->native_status=ERROR_ACCESS_DENIED;r->reserved=0;r->operation_id_echo=*id;
 if(bad_echo)r->operation_id_echo.sequence++;
 InterlockedDecrement(&active_calls);
}
static void WINAPI fake_handoff(void *ctx,const raya_durable_operation_id_v1 *id,raya_durable_handoff_result_v1 *r){
 (void)ctx;InterlockedIncrement(&handoff_calls);r->struct_size=sizeof(*r);r->schema_version=1;
 r->disposition=RAYA_DURABLE_HANDOFF_TRANSFERRED;r->reserved=0;
 memset(r->worker_instance,0x5a,sizeof(r->worker_instance));r->worker_operation_token=77;
 r->operation_id_echo=*id;
}
static void make_id(raya_durable_operation_id_v1 *id,ULONGLONG seq){
 memset(id,0,sizeof(*id));id->struct_size=sizeof(*id);id->schema_version=1;
 memset(id->name_space,1,sizeof(id->name_space));id->sequence=seq;
 id->operation_kind=RAYA_DURABLE_OPERATION_WRITE;memset(id->payload_digest,2,sizeof(id->payload_digest));
}
typedef struct {raya_durable_runtime_v1 *r; raya_durable_operation_id_v1 id; raya_durable_issue_result_v1 out; ULONGLONG lease; raya_durable_owner_status_v1 status;} thread_arg;
static DWORD WINAPI invoke_thread(void *p){thread_arg *a=(thread_arg*)p;a->status=raya_durable_runtime_invoke_issue_v1(a->r,&a->id,&a->out,&a->lease);return 0;}
int main(void){
 raya_durable_runtime_v1 r; raya_durable_config_v1 cfg; raya_durable_callbacks_v1 cb;
 raya_durable_operation_id_v1 id; raya_durable_issue_result_v1 ir;
 raya_durable_handoff_result_v1 hr; ULONGLONG lease=0,unknown_lease=0; HANDLE th[2];thread_arg a[2];unsigned i;
 memset(&cfg,0,sizeof(cfg));cfg.struct_size=sizeof(cfg);cfg.schema_version=1;
 cfg.capabilities=RAYA_DURABLE_CAP_IDENTITY|RAYA_DURABLE_CAP_LOCAL_JOURNAL|RAYA_DURABLE_CAP_TYPED_ISSUE|RAYA_DURABLE_CAP_TYPED_HANDOFF;memset(cfg.name_space,3,sizeof(cfg.name_space));
 cfg.guarantee=RAYA_DURABLE_GUARANTEE_QUARANTINE_ONLY;
 memset(&cb,0,sizeof(cb));cb.struct_size=sizeof(cb);cb.schema_version=1;
 cb.capabilities=cfg.capabilities;cb.issue=fake_issue;cb.handoff=fake_handoff;
 make_id(&id,1);
 T(raya_durable_runtime_init_v1(&r,&cfg,&cb)==RAYA_DURABLE_OWNER_OK_V1);
 T(raya_durable_runtime_invoke_issue_v1(&r,&id,&ir,&lease)==RAYA_DURABLE_OWNER_OK_V1);
 T(ir.disposition==RAYA_DURABLE_ISSUE_PROVEN_NOT_SUBMITTED&&lease!=0);
 T(raya_durable_runtime_invoke_handoff_v1(&r,&id,&hr,&lease)==RAYA_DURABLE_OWNER_OK_V1);
 T(hr.disposition==RAYA_DURABLE_HANDOFF_TRANSFERRED&&hr.worker_operation_token==77);
 bad_echo=1;T(raya_durable_runtime_invoke_issue_v1(&r,&id,&ir,&lease)==RAYA_DURABLE_OWNER_QUARANTINED_RESULT_V1);
 T(ir.disposition==RAYA_DURABLE_ISSUE_UNKNOWN&&lease!=0);unknown_lease=lease;
 T(raya_durable_runtime_invoke_handoff_v1(&r,&id,&hr,&lease)==RAYA_DURABLE_OWNER_QUARANTINED_RESULT_V1);
 T(raya_durable_runtime_begin_close_v1(&r)==RAYA_DURABLE_OWNER_BUSY_V1);
 T(raya_durable_runtime_reconcile_v1(&r,unknown_lease,1)==RAYA_DURABLE_OWNER_OK_V1);
 T(raya_durable_runtime_finish_close_v1(&r)==RAYA_DURABLE_OWNER_OK_V1);
 T(raya_durable_runtime_destroy_v1(&r)==RAYA_DURABLE_OWNER_OK_V1);
 T(raya_durable_runtime_destroy_v1(&r)==RAYA_DURABLE_OWNER_INVALID_V1);
 bad_echo=0;InterlockedExchange(&active_calls,0);InterlockedExchange(&max_active,0);
 T(raya_durable_runtime_init_v1(&r,&cfg,&cb)==RAYA_DURABLE_OWNER_OK_V1);
 for(i=0;i<2;i++){memset(&a[i],0,sizeof(a[i]));a[i].r=&r;make_id(&a[i].id,i+10);th[i]=CreateThread(NULL,0,invoke_thread,&a[i],0,NULL);T(th[i]!=NULL);}
 WaitForMultipleObjects(2,th,TRUE,10000);
 for(i=0;i<2;i++){T(a[i].status==RAYA_DURABLE_OWNER_OK_V1);CloseHandle(th[i]);}
 T(max_active==1);T(issue_calls>=3);
 T(raya_durable_runtime_begin_close_v1(&r)==RAYA_DURABLE_OWNER_OK_V1);
 T(raya_durable_runtime_finish_close_v1(&r)==RAYA_DURABLE_OWNER_OK_V1);
 T(raya_durable_runtime_destroy_v1(&r)==RAYA_DURABLE_OWNER_OK_V1);
 callback_entered=CreateEvent(NULL,TRUE,FALSE,NULL);callback_release=CreateEvent(NULL,TRUE,FALSE,NULL);T(callback_entered!=NULL&&callback_release!=NULL);
 T(raya_durable_runtime_init_v1(&r,&cfg,&cb)==RAYA_DURABLE_OWNER_OK_V1);InterlockedExchange(&block_callback,1);
 memset(&a[0],0,sizeof(a[0]));a[0].r=&r;make_id(&a[0].id,30);th[0]=CreateThread(NULL,0,invoke_thread,&a[0],0,NULL);T(th[0]!=NULL);
 T(WaitForSingleObject(callback_entered,5000)==WAIT_OBJECT_0);memset(&a[1],0,sizeof(a[1]));a[1].r=&r;make_id(&a[1].id,31);th[1]=CreateThread(NULL,0,invoke_thread,&a[1],0,NULL);T(th[1]!=NULL);
 {DWORD spin;for(spin=0;spin<5000;spin++){DWORD count;EnterCriticalSection(&r.state_lock);count=r.owner.in_flight;LeaveCriticalSection(&r.state_lock);if(count==2)break;Sleep(1);}T(spin<5000);}
 T(raya_durable_runtime_begin_close_v1(&r)==RAYA_DURABLE_OWNER_BUSY_V1);SetEvent(callback_release);WaitForMultipleObjects(2,th,TRUE,10000);
 T(a[0].status==RAYA_DURABLE_OWNER_OK_V1);T(a[1].status==RAYA_DURABLE_OWNER_BUSY_V1);T(issue_calls==5);
 CloseHandle(th[0]);CloseHandle(th[1]);InterlockedExchange(&block_callback,0);T(raya_durable_runtime_finish_close_v1(&r)==RAYA_DURABLE_OWNER_OK_V1);T(raya_durable_runtime_destroy_v1(&r)==RAYA_DURABLE_OWNER_OK_V1);CloseHandle(callback_entered);CloseHandle(callback_release);
 printf("Raya durable runtime v1: %u checks, %u failures\n",checks,failures);
 return failures?1:0;
}
