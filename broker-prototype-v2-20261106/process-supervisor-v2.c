#include "process-supervisor-v2.h"
#include <string.h>

static BOOL close_handle_real(HANDLE h, void *context) {
 (void)context;
 return CloseHandle(h);
}
static int close_if_valid(HANDLE *h, const raya_supervisor_api *api) {
 raya_supervisor_close_handle_fn fn=close_handle_real;
 if (*h==NULL || *h==INVALID_HANDLE_VALUE) return 1;
 if (api!=NULL && api->close_handle!=NULL) fn=api->close_handle;
 if (!fn(*h,api!=NULL ? api->context : NULL)) return 0;
 *h=NULL; return 1;
}
static HANDLE api_create_job(const raya_supervisor_api *a) {
 return a!=NULL && a->create_job!=NULL ? a->create_job(a->context) : CreateJobObjectW(NULL,NULL);
}
static BOOL api_set_job_info(const raya_supervisor_api *a,HANDLE job,
 JOBOBJECTINFOCLASS cls,void *info,DWORD size) {
 return a!=NULL && a->set_job_information!=NULL ?
  a->set_job_information(job,cls,info,size,a->context) :
  SetInformationJobObject(job,cls,info,size);
}
static BOOL api_terminate_job(const raya_supervisor_api *a,HANDLE job,UINT code) {
 return a!=NULL && a->terminate_job!=NULL ?
  a->terminate_job(job,code,a->context) : TerminateJobObject(job,code);
}
static BOOL api_create_process(const raya_supervisor_api *a,const wchar_t *app,
 wchar_t *cmd,DWORD flags,const wchar_t *cwd,STARTUPINFOW *si,PROCESS_INFORMATION *pi) {
 return a!=NULL && a->create_process!=NULL ?
  a->create_process(app,cmd,flags,cwd,si,pi,a->context) :
  CreateProcessW(app,cmd,NULL,NULL,FALSE,flags,NULL,cwd,si,pi);
}
static BOOL api_assign(const raya_supervisor_api *a,HANDLE job,HANDLE process) {
 return a!=NULL && a->assign_process_to_job!=NULL ?
  a->assign_process_to_job(job,process,a->context) : AssignProcessToJobObject(job,process);
}
static BOOL api_is_in_job(const raya_supervisor_api *a,HANDLE process,HANDLE job,BOOL *in_job) {
 return a!=NULL && a->is_process_in_job!=NULL ?
  a->is_process_in_job(process,job,in_job,a->context) : IsProcessInJob(process,job,in_job);
}
static DWORD api_resume(const raya_supervisor_api *a,HANDLE thread) {
 return a!=NULL && a->resume_thread!=NULL ?
  a->resume_thread(thread,a->context) : ResumeThread(thread);
}
static BOOL api_terminate_process(const raya_supervisor_api *a,HANDLE process,UINT code) {
 return a!=NULL && a->terminate_process!=NULL ?
  a->terminate_process(process,code,a->context) : TerminateProcess(process,code);
}
static DWORD api_wait(const raya_supervisor_api *a,HANDLE h,DWORD timeout) {
 return a!=NULL && a->wait!=NULL ? a->wait(h,timeout,a->context) :
  WaitForSingleObject(h,timeout);
}
static BOOL api_exit_code(const raya_supervisor_api *a,HANDLE process,DWORD *code) {
 return a!=NULL && a->get_exit_code!=NULL ?
  a->get_exit_code(process,code,a->context) : GetExitCodeProcess(process,code);
}
static int collect_process(raya_process_supervisor *s, DWORD timeout_ms,
                           const raya_supervisor_api *api) {
 DWORD wr,code=0;
 wr=api_wait(api,s->process,timeout_ms);
 if (wr!=WAIT_OBJECT_0) return 0;
 if (!api_exit_code(api,s->process,&code)) {
  s->state=RAYA_SUPERVISOR_DEGRADED; return 0;
 }
 s->exit_code=code; s->state=RAYA_SUPERVISOR_EXITED; return 1;
}
int raya_supervisor_start_with_api(raya_process_supervisor *s,
 const wchar_t *application, wchar_t *command_line,
 const wchar_t *working_directory,const raya_supervisor_api *api) {
 JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
 STARTUPINFOW si;
 PROCESS_INFORMATION pi;
 BOOL in_job=FALSE;
 DWORD error;
 if (s==NULL || application==NULL || application[0]==L'\0' ||
     command_line==NULL || command_line[0]==L'\0') return 0;
 memset(s,0,sizeof(*s));
 s->job=api_create_job(api);
 if (s->job==NULL) goto failed_empty;
 memset(&limits,0,sizeof(limits));
 limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
 if (!api_set_job_info(api,s->job,JobObjectExtendedLimitInformation,
      &limits,sizeof(limits))) goto failed_job;
 memset(&si,0,sizeof(si)); si.cb=sizeof(si);
 memset(&pi,0,sizeof(pi));
 if (!api_create_process(api,application,command_line,
      CREATE_SUSPENDED|CREATE_NO_WINDOW,working_directory,&si,&pi))
  goto failed_job;
 s->process=pi.hProcess; s->primary_thread=pi.hThread;
 s->process_id=pi.dwProcessId;
 if (!api_assign(api,s->job,s->process)) goto failed_child;
 if (!api_is_in_job(api,s->process,s->job,&in_job) || !in_job)
  goto failed_child;
 if (api_resume(api,pi.hThread)==(DWORD)-1) goto failed_child;
 if (!close_if_valid(&s->primary_thread,api)) {
  s->state=RAYA_SUPERVISOR_DEGRADED; return 0;
 }
 s->state=RAYA_SUPERVISOR_RUNNING;
 return 1;
failed_child:
 error=GetLastError();
 if (!api_terminate_process(api,s->process,error ? error : ERROR_PROCESS_ABORTED) &&
     GetLastError()!=ERROR_ACCESS_DENIED) {
  s->state=RAYA_SUPERVISOR_DEGRADED; SetLastError(error); return 0;
 }
 if (api_wait(api,s->process,5000)!=WAIT_OBJECT_0) {
  s->state=RAYA_SUPERVISOR_DEGRADED; SetLastError(error); return 0;
 }
 if (!close_if_valid(&s->primary_thread,api) ||
     !close_if_valid(&s->process,api) ||
     !close_if_valid(&s->job,api)) {
  s->state=RAYA_SUPERVISOR_DEGRADED; SetLastError(error); return 0;
 }
 memset(s,0,sizeof(*s));
 SetLastError(error);
 return 0;
failed_job:
 error=GetLastError();
 if (!close_if_valid(&s->job,api)) {
  s->state=RAYA_SUPERVISOR_DEGRADED; SetLastError(error); return 0;
 }
 memset(s,0,sizeof(*s));
 SetLastError(error);
 return 0;
failed_empty:
 return 0;
}
int raya_supervisor_start(raya_process_supervisor *s,const wchar_t *application,
 wchar_t *command_line,const wchar_t *working_directory) {
 return raya_supervisor_start_with_api(s,application,command_line,
                                       working_directory,NULL);
}
int raya_supervisor_wait_with_api(raya_process_supervisor *s,DWORD timeout_ms,
                                  const raya_supervisor_api *api) {
 if (s==NULL || s->process==NULL ||
     (s->state!=RAYA_SUPERVISOR_RUNNING &&
      s->state!=RAYA_SUPERVISOR_DEGRADED)) return 0;
 return collect_process(s,timeout_ms,api);
}
int raya_supervisor_wait(raya_process_supervisor *s,DWORD timeout_ms) {
 return raya_supervisor_wait_with_api(s,timeout_ms,NULL);
}
int raya_supervisor_terminate_job_with_api(raya_process_supervisor *s,
 UINT exit_code,const raya_supervisor_api *api) {
 if (s==NULL || s->job==NULL || s->process==NULL ||
     (s->state!=RAYA_SUPERVISOR_RUNNING &&
      s->state!=RAYA_SUPERVISOR_DEGRADED)) return 0;
 if (!api_terminate_job(api,s->job,exit_code)) {
  s->state=RAYA_SUPERVISOR_DEGRADED; return 0;
 }
 return collect_process(s,5000,api);
}
int raya_supervisor_terminate_job(raya_process_supervisor *s,UINT exit_code) {
 return raya_supervisor_terminate_job_with_api(s,exit_code,NULL);
}
int raya_supervisor_close_with_api(raya_process_supervisor *s,
                                   const raya_supervisor_api *api) {
 if (s==NULL ||
     (s->state!=RAYA_SUPERVISOR_EXITED &&
      s->state!=RAYA_SUPERVISOR_DEGRADED)) return 0;
 if (s->process!=NULL) {
  if (api_wait(api,s->process,0)!=WAIT_OBJECT_0) return 0;
  if (s->state==RAYA_SUPERVISOR_DEGRADED) {
   DWORD code=0;
   if (!api_exit_code(api,s->process,&code)) return 0;
   s->exit_code=code;
  }
  s->state=RAYA_SUPERVISOR_EXITED;
 }
 if (!close_if_valid(&s->primary_thread,api)) {
  s->state=RAYA_SUPERVISOR_DEGRADED; return 0;
 }
 if (!close_if_valid(&s->process,api)) {
  s->state=RAYA_SUPERVISOR_DEGRADED; return 0;
 }
 if (!close_if_valid(&s->job,api)) {
  s->state=RAYA_SUPERVISOR_DEGRADED; return 0;
 }
 memset(s,0,sizeof(*s));
 return 1;
}
int raya_supervisor_close(raya_process_supervisor *s) {
 return raya_supervisor_close_with_api(s,NULL);
}
