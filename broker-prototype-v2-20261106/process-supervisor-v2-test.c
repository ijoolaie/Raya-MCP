#define WIN32_LEAN_AND_MEAN
#include "process-supervisor-v2.h"
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <strsafe.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { (void)fprintf(stderr,"FAIL line %d: %s error=%lu\n",__LINE__,#x,(unsigned long)GetLastError()); return 1; } } while(0)

static int worker_mode(int exit_mode) {
 if (exit_mode) return 37;
 Sleep(60000);
 return 0;
}
static int test_launch_and_kill(const wchar_t *self) {
 raya_process_supervisor s;
 wchar_t cmd[2048];
 DWORD pid;
 memset(&s,0,sizeof(s));
 if (FAILED(StringCchPrintfW(cmd,2048,L"\"%s\" --sleep-worker",self))) return 1;
 CHECK(raya_supervisor_start(&s,self,cmd,NULL));
 CHECK(s.state==RAYA_SUPERVISOR_RUNNING && s.process!=NULL && s.job!=NULL);
 pid=s.process_id;
 CHECK(pid!=0);
 CHECK(raya_supervisor_wait(&s,25)==0);
 CHECK(s.process!=NULL && s.job!=NULL && s.state==RAYA_SUPERVISOR_RUNNING);
 CHECK(raya_supervisor_terminate_job(&s,123));
 CHECK(s.state==RAYA_SUPERVISOR_EXITED && s.exit_code==123);
 CHECK(raya_supervisor_close(&s));
 CHECK(s.state==RAYA_SUPERVISOR_EMPTY && s.process==NULL && s.job==NULL);
 return 0;
}
static int test_job_close_contains_child(const wchar_t *self) {
 JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
 STARTUPINFOW si;
 PROCESS_INFORMATION pi;
 HANDLE job;
 wchar_t cmd[2048];
 BOOL in_job=FALSE;
 DWORD wr;
 CHECK(SUCCEEDED(StringCchPrintfW(cmd,2048,L"\"%s\" --sleep-worker",self)));
 job=CreateJobObjectW(NULL,NULL);
 CHECK(job!=NULL);
 ZeroMemory(&limits,sizeof(limits));
 limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
 CHECK(SetInformationJobObject(job,JobObjectExtendedLimitInformation,
                               &limits,sizeof(limits)));
 ZeroMemory(&si,sizeof(si)); si.cb=sizeof(si);
 ZeroMemory(&pi,sizeof(pi));
 CHECK(CreateProcessW(self,cmd,NULL,NULL,FALSE,
                      CREATE_SUSPENDED|CREATE_NO_WINDOW,NULL,NULL,&si,&pi));
 CHECK(AssignProcessToJobObject(job,pi.hProcess));
 CHECK(IsProcessInJob(pi.hProcess,job,&in_job) && in_job);
 CHECK(ResumeThread(pi.hThread)!=(DWORD)-1);
 CHECK(CloseHandle(pi.hThread));
 /* Simulate supervisor process loss: last job handle closes. */
 CHECK(CloseHandle(job));
 wr=WaitForSingleObject(pi.hProcess,5000);
 CHECK(wr==WAIT_OBJECT_0);
 CHECK(CloseHandle(pi.hProcess));
 return 0;
}
static int test_natural_exit(const wchar_t *self) {
 raya_process_supervisor s;
 wchar_t cmd[2048];
 memset(&s,0,sizeof(s));
 if (FAILED(StringCchPrintfW(cmd,2048,L"\"%s\" --exit-worker",self))) return 1;
 CHECK(raya_supervisor_start(&s,self,cmd,NULL));
 CHECK(raya_supervisor_wait(&s,5000));
 CHECK(s.state==RAYA_SUPERVISOR_EXITED && s.exit_code==37);
 CHECK(raya_supervisor_close(&s));
 return 0;
}
typedef struct { unsigned calls; unsigned fail_on_call; } close_fault;
static BOOL WINAPI injected_close(HANDLE h, void *context) {
 close_fault *f=(close_fault *)context;
 ++f->calls;
 if (f->calls==f->fail_on_call) {
  SetLastError(ERROR_ACCESS_DENIED);
  return FALSE;
 }
 return CloseHandle(h);
}
static int test_close_retry(const wchar_t *self) {
 raya_process_supervisor s;
 raya_supervisor_api api;
 close_fault fault;
 wchar_t cmd[2048];
 HANDLE retained_process,retained_job;
 memset(&s,0,sizeof(s));
 memset(&api,0,sizeof(api));
 memset(&fault,0,sizeof(fault));
 fault.fail_on_call=1;
 api.close_handle=injected_close;
 api.context=&fault;
 CHECK(SUCCEEDED(StringCchPrintfW(cmd,2048,L"\"%s\" --exit-worker",self)));
 CHECK(raya_supervisor_start(&s,self,cmd,NULL));
 CHECK(raya_supervisor_wait(&s,5000));
 retained_process=s.process;
 retained_job=s.job;
 CHECK(!raya_supervisor_close_with_api(&s,&api));
 CHECK(s.state==RAYA_SUPERVISOR_DEGRADED);
 CHECK(s.process==retained_process && s.job==retained_job);
 CHECK(fault.calls==1);
 CHECK(raya_supervisor_close_with_api(&s,&api));
 CHECK(fault.calls==3);
 CHECK(s.state==RAYA_SUPERVISOR_EMPTY && s.process==NULL && s.job==NULL);
 return 0;
}
typedef struct { unsigned fail_assign; unsigned fail_resume; unsigned fail_is_job_api; unsigned report_not_in_job; unsigned fail_terminate_job; unsigned calls; } start_fault;
static BOOL WINAPI fault_create(const wchar_t *app,wchar_t *cmd,DWORD flags,
 const wchar_t *cwd,STARTUPINFOW *si,PROCESS_INFORMATION *pi,void *ctx) {
 (void)ctx;
 return CreateProcessW(app,cmd,NULL,NULL,FALSE,flags,NULL,cwd,si,pi);
}
static BOOL WINAPI fault_assign(HANDLE job,HANDLE process,void *ctx) {
 start_fault *f=(start_fault *)ctx;
 ++f->calls;
 if (f->fail_assign) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
 return AssignProcessToJobObject(job,process);
}
static BOOL WINAPI fault_is_in_job(HANDLE process,HANDLE job,BOOL *in_job,void *ctx) {
 start_fault *f=(start_fault *)ctx;
 if (f->fail_is_job_api) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
 if (f->report_not_in_job) { *in_job=FALSE; SetLastError(ERROR_ACCESS_DENIED); return TRUE; }
 return IsProcessInJob(process,job,in_job);
}
static BOOL WINAPI fault_terminate_job(HANDLE job,UINT code,void *ctx) {
 start_fault *f=(start_fault *)ctx;
 if (f->fail_terminate_job) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
 return TerminateJobObject(job,code);
}
static DWORD WINAPI fault_resume(HANDLE thread,void *ctx) {
 start_fault *f=(start_fault *)ctx;
 ++f->calls;
 if (f->fail_resume) { SetLastError(ERROR_ACCESS_DENIED); return (DWORD)-1; }
 return ResumeThread(thread);
}
static int test_start_failure_injection(const wchar_t *self,int fail_resume) {
 raya_process_supervisor s;
 raya_supervisor_api api;
 start_fault fault;
 wchar_t cmd[2048];
 memset(&s,0xA5,sizeof(s));
 memset(&api,0,sizeof(api));
 memset(&fault,0,sizeof(fault));
 fault.fail_assign=fail_resume ? 0U : 1U;
 fault.fail_resume=fail_resume ? 1U : 0U;
 api.create_process=fault_create;
 api.assign_process_to_job=fault_assign;
 api.is_process_in_job=fault_is_in_job;
 api.resume_thread=fault_resume;
 api.context=&fault;
 CHECK(SUCCEEDED(StringCchPrintfW(cmd,2048,L"\"%s\" --sleep-worker",self)));
 CHECK(!raya_supervisor_start_with_api(&s,self,cmd,NULL,&api));
 CHECK(s.state==RAYA_SUPERVISOR_EMPTY);
 CHECK(s.job==NULL && s.process==NULL && s.primary_thread==NULL);
 CHECK(fault.calls==(fail_resume ? 2U : 1U));
 return 0;
}
typedef struct { unsigned fail_create; unsigned fail_info; } job_fault;
static HANDLE WINAPI fault_create_job(void *ctx) {
 job_fault *f=(job_fault *)ctx;
 if (f->fail_create) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
 return CreateJobObjectW(NULL,NULL);
}
static BOOL WINAPI fault_set_job_info(HANDLE job,JOBOBJECTINFOCLASS cls,
 void *info,DWORD size,void *ctx) {
 job_fault *f=(job_fault *)ctx;
 if (f->fail_info) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
 return SetInformationJobObject(job,cls,info,size);
}
static int test_job_api_injection(const wchar_t *self) {
 raya_process_supervisor s;
 raya_supervisor_api api;
 job_fault jf;
 wchar_t cmd[2048];
 memset(&api,0,sizeof(api)); memset(&jf,0,sizeof(jf));
 memset(&s,0,sizeof(s)); jf.fail_create=1;
 api.create_job=fault_create_job; api.context=&jf;
 CHECK(SUCCEEDED(StringCchPrintfW(cmd,2048,L"\"%s\" --sleep-worker",self)));
 CHECK(!raya_supervisor_start_with_api(&s,self,cmd,NULL,&api));
 CHECK(s.state==RAYA_SUPERVISOR_EMPTY && s.job==NULL);
 memset(&s,0,sizeof(s)); memset(&api,0,sizeof(api));
 memset(&jf,0,sizeof(jf));
 jf.fail_info=1;
 api.create_job=fault_create_job; api.set_job_information=fault_set_job_info;
 api.context=&jf;
 CHECK(!raya_supervisor_start_with_api(&s,self,cmd,NULL,&api));
 CHECK(s.state==RAYA_SUPERVISOR_EMPTY && s.job==NULL);
 return 0;
}
typedef struct { unsigned close_calls; unsigned fail_info; } setup_cleanup_fault;
static BOOL WINAPI setup_cleanup_set_info(HANDLE job,JOBOBJECTINFOCLASS cls,
 void *info,DWORD size,void *ctx) {
 setup_cleanup_fault *f=(setup_cleanup_fault *)ctx;
 if (f->fail_info) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
 return SetInformationJobObject(job,cls,info,size);
}
static BOOL WINAPI setup_cleanup_close(HANDLE h,void *ctx) {
 setup_cleanup_fault *f=(setup_cleanup_fault *)ctx;
 ++f->close_calls;
 if (f->close_calls==1U) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
 return CloseHandle(h);
}
static int test_is_in_job_failure_paths(const wchar_t *self) {
 raya_process_supervisor s;
 raya_supervisor_api api;
 start_fault fault;
 wchar_t cmd[2048];
 unsigned mode;
 for (mode=0;mode<2U;++mode) {
  memset(&s,0,sizeof(s)); memset(&api,0,sizeof(api));
  memset(&fault,0,sizeof(fault));
  fault.fail_is_job_api=(mode==0U);
  fault.report_not_in_job=(mode==1U);
  api.create_process=fault_create; api.assign_process_to_job=fault_assign;
  api.is_process_in_job=fault_is_in_job; api.context=&fault;
  CHECK(SUCCEEDED(StringCchPrintfW(cmd,2048,L"\"%s\" --sleep-worker",self)));
  CHECK(!raya_supervisor_start_with_api(&s,self,cmd,NULL,&api));
  CHECK(s.state==RAYA_SUPERVISOR_EMPTY);
  CHECK(s.job==NULL && s.process==NULL && s.primary_thread==NULL);
 }
 return 0;
}
static int test_terminate_job_failure(const wchar_t *self) {
 raya_process_supervisor s;
 raya_supervisor_api api;
 start_fault fault;
 wchar_t cmd[2048];
 HANDLE process,job;
 memset(&s,0,sizeof(s)); memset(&api,0,sizeof(api)); memset(&fault,0,sizeof(fault));
 fault.fail_terminate_job=1U;
 api.terminate_job=fault_terminate_job; api.context=&fault;
  CHECK(SUCCEEDED(StringCchPrintfW(cmd,2048,L"\"%s\" --sleep-worker",self)));
 CHECK(raya_supervisor_start(&s,self,cmd,NULL));
 process=s.process; job=s.job;
 CHECK(!raya_supervisor_terminate_job_with_api(&s,91U,&api));
 CHECK(s.state==RAYA_SUPERVISOR_DEGRADED);
 CHECK(s.process==process && s.job==job && s.primary_thread==NULL);
 CHECK(WaitForSingleObject(process,0)==WAIT_TIMEOUT);
 CHECK(raya_supervisor_terminate_job(&s,92U));
 CHECK(s.state==RAYA_SUPERVISOR_EXITED && s.exit_code==92U);
 CHECK(raya_supervisor_close(&s));
 CHECK(s.state==RAYA_SUPERVISOR_EMPTY && s.process==NULL && s.job==NULL);
 return 0;
}
static int test_setup_close_retry(const wchar_t *self) {
 raya_process_supervisor s;
 raya_supervisor_api api;
 setup_cleanup_fault cf;
 wchar_t cmd[2048];
 HANDLE retained_job;
 memset(&s,0,sizeof(s)); memset(&api,0,sizeof(api));
 memset(&cf,0,sizeof(cf));
 cf.fail_info=1U;
 api.set_job_information=setup_cleanup_set_info;
 api.close_handle=setup_cleanup_close; api.context=&cf;
  CHECK(SUCCEEDED(StringCchPrintfW(cmd,2048,L"\"%s\" --sleep-worker",self)));
 CHECK(!raya_supervisor_start_with_api(&s,self,cmd,NULL,&api));
 retained_job=s.job;
 CHECK(s.state==RAYA_SUPERVISOR_DEGRADED && retained_job!=NULL);
 CHECK(s.process==NULL && s.primary_thread==NULL && cf.close_calls==1U);
 CHECK(raya_supervisor_close_with_api(&s,&api));
 CHECK(cf.close_calls==2U);
 CHECK(s.state==RAYA_SUPERVISOR_EMPTY && s.job==NULL);
 return 0;
}
static int test_host_and_nested_jobs(const wchar_t *self) {
 HANDLE outer=NULL,inner=NULL;
 STARTUPINFOW si;
 PROCESS_INFORMATION pi;
 wchar_t cmd[2048];
 BOOL parent_host=FALSE,child_host=FALSE,in_outer=FALSE,in_inner=FALSE;
 DWORD err=0,wr;
 int rc=0,assigned_outer=0;
 memset(&pi,0,sizeof(pi));
 if (!IsProcessInJob(GetCurrentProcess(),NULL,&parent_host)) {
  (void)fprintf(stderr,"HOST JOB QUERY failed: %lu\\n",(unsigned long)GetLastError()); return 1;
 }
 ZeroMemory(&si,sizeof(si)); si.cb=sizeof(si);
 if (FAILED(StringCchPrintfW(cmd,2048,L"\\\"%s\\\" --sleep-worker",self))) return 1;
 if (!CreateProcessW(self,cmd,NULL,NULL,FALSE,
      CREATE_SUSPENDED|CREATE_NO_WINDOW,NULL,NULL,&si,&pi)) {
  (void)fprintf(stderr,"HOST JOB child create failed: %lu\\n",(unsigned long)GetLastError()); return 1;
 }
 if (!IsProcessInJob(pi.hProcess,NULL,&child_host)) {
  (void)fprintf(stderr,"CHILD inherited-job query failed: %lu\\n",(unsigned long)GetLastError()); rc=1; goto cleanup;
 }
 ++checks;
 if (parent_host!=child_host) {
  (void)fprintf(stderr,"FAIL child host-job inheritance parent=%d child=%d\\n",parent_host,child_host); rc=1; goto cleanup;
 }
 outer=CreateJobObjectW(NULL,NULL);
 ++checks;
 if (outer==NULL) { (void)fprintf(stderr,"OUTER Job creation failed: %lu\\n",(unsigned long)GetLastError()); rc=1; goto cleanup; }
 if (!AssignProcessToJobObject(outer,pi.hProcess)) {
  err=GetLastError();
  (void)printf("HOST_NESTING: parent_job=%u child_inherited=%u outer_assignment=BLOCKED error=%lu\\n",
               parent_host?1U:0U,child_host?1U:0U,(unsigned long)err);
  goto cleanup;
 }
 assigned_outer=1;
 ++checks;
 if (!IsProcessInJob(pi.hProcess,outer,&in_outer) || !in_outer) {
  (void)fprintf(stderr,"FAIL outer Job membership verification error=%lu\\n",(unsigned long)GetLastError()); rc=1; goto cleanup;
 }
 inner=CreateJobObjectW(NULL,NULL);
 ++checks;
 if (inner==NULL) { (void)fprintf(stderr,"INNER Job creation failed: %lu\\n",(unsigned long)GetLastError()); rc=1; goto cleanup; }
 if (!AssignProcessToJobObject(inner,pi.hProcess)) {
  err=GetLastError();
  (void)printf("HOST_NESTING: parent_job=%u child_inherited=%u outer_assignment=PASS inner_assignment=BLOCKED error=%lu\\n",
               parent_host?1U:0U,child_host?1U:0U,(unsigned long)err);
  goto cleanup;
 }
 ++checks;
 if (!IsProcessInJob(pi.hProcess,inner,&in_inner) || !in_inner) {
  (void)fprintf(stderr,"FAIL inner Job membership verification error=%lu\\n",(unsigned long)GetLastError()); rc=1; goto cleanup;
 }
 in_outer=FALSE;
 ++checks;
 if (!IsProcessInJob(pi.hProcess,outer,&in_outer) || !in_outer) {
  (void)fprintf(stderr,"FAIL ancestor Job membership verification error=%lu\\n",(unsigned long)GetLastError()); rc=1; goto cleanup;
 }
 (void)printf("HOST_NESTING: parent_job=%u child_inherited=%u outer_assignment=PASS inner_assignment=PASS ancestor_membership=PASS\\n",
              parent_host?1U:0U,child_host?1U:0U);
cleanup:
 if (pi.hProcess!=NULL) {
  if (assigned_outer) {
   if (!TerminateJobObject(outer,93U)) (void)TerminateProcess(pi.hProcess,94U);
  } else {
   (void)TerminateProcess(pi.hProcess,95U);
  }
  wr=WaitForSingleObject(pi.hProcess,5000);
  if (wr!=WAIT_OBJECT_0) {
   (void)fprintf(stderr,"FAIL host/nested Job child cleanup wait=%lu error=%lu\\n",
                 (unsigned long)wr,(unsigned long)GetLastError()); rc=1;
  }
 }
 if (inner!=NULL && !CloseHandle(inner)) rc=1;
 if (outer!=NULL && !CloseHandle(outer)) rc=1;
 if (pi.hThread!=NULL && !CloseHandle(pi.hThread)) rc=1;
 if (pi.hProcess!=NULL && !CloseHandle(pi.hProcess)) rc=1;
 return rc;
}

int wmain(int argc, wchar_t **argv) {
 if (argc==2 && wcscmp(argv[1],L"--sleep-worker")==0) return worker_mode(0);
 if (argc==2 && wcscmp(argv[1],L"--exit-worker")==0) return worker_mode(1);
 if (argc!=1) return 2;
 if (test_host_and_nested_jobs(argv[0])) return 1;
 if (test_launch_and_kill(argv[0])) return 1;
 if (test_job_close_contains_child(argv[0])) return 1;
 if (test_natural_exit(argv[0])) return 1;
 if (test_close_retry(argv[0])) return 1;
 if (test_start_failure_injection(argv[0],0)) return 1;
 if (test_start_failure_injection(argv[0],1)) return 1;
 if (test_job_api_injection(argv[0])) return 1;
 if (test_is_in_job_failure_paths(argv[0])) return 1;
 if (test_terminate_job_failure(argv[0])) return 1;
 if (test_setup_close_retry(argv[0])) return 1;
 (void)printf("Process Supervisor v2: %u assertions PASS\n",checks);
 return 0;
}
