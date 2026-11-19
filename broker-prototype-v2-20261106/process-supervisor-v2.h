#ifndef RAYA_PROCESS_SUPERVISOR_V2_H
#define RAYA_PROCESS_SUPERVISOR_V2_H
#include <windows.h>

typedef enum {
 RAYA_SUPERVISOR_EMPTY=0,
 RAYA_SUPERVISOR_RUNNING=1,
 RAYA_SUPERVISOR_EXITED=2,
 RAYA_SUPERVISOR_DEGRADED=3
} raya_supervisor_state;

typedef struct {
 HANDLE job;
 HANDLE process;
 HANDLE primary_thread;
 DWORD process_id;
 DWORD exit_code;
 raya_supervisor_state state;
} raya_process_supervisor;

/* Narrow API seam for deterministic handle-close failure tests. A NULL
 * callback selects the real Win32 CloseHandle implementation. */
typedef BOOL (WINAPI *raya_supervisor_close_handle_fn)(HANDLE handle,
                                                       void *context);
typedef HANDLE (WINAPI *raya_supervisor_create_job_fn)(void *context);
typedef BOOL (WINAPI *raya_supervisor_set_job_info_fn)(HANDLE job,
 JOBOBJECTINFOCLASS info_class,void *info,DWORD info_size,void *context);
typedef BOOL (WINAPI *raya_supervisor_terminate_job_fn)(HANDLE job,UINT code,
                                                        void *context);
typedef BOOL (WINAPI *raya_supervisor_create_process_fn)(
 const wchar_t *application, wchar_t *command_line,
 DWORD creation_flags, const wchar_t *working_directory,
 STARTUPINFOW *startup, PROCESS_INFORMATION *process, void *context);
typedef BOOL (WINAPI *raya_supervisor_assign_job_fn)(HANDLE job,HANDLE process,void *context);
typedef BOOL (WINAPI *raya_supervisor_is_in_job_fn)(HANDLE process,HANDLE job,BOOL *in_job,void *context);
typedef DWORD (WINAPI *raya_supervisor_resume_thread_fn)(HANDLE thread,void *context);
typedef BOOL (WINAPI *raya_supervisor_terminate_process_fn)(HANDLE process,UINT code,void *context);
typedef DWORD (WINAPI *raya_supervisor_wait_fn)(HANDLE handle,DWORD timeout,void *context);
typedef BOOL (WINAPI *raya_supervisor_get_exit_code_fn)(HANDLE process,DWORD *code,void *context);
typedef struct {
 raya_supervisor_close_handle_fn close_handle;
 raya_supervisor_create_job_fn create_job;
 raya_supervisor_set_job_info_fn set_job_information;
 raya_supervisor_terminate_job_fn terminate_job;
 raya_supervisor_create_process_fn create_process;
 raya_supervisor_assign_job_fn assign_process_to_job;
 raya_supervisor_is_in_job_fn is_process_in_job;
 raya_supervisor_resume_thread_fn resume_thread;
 raya_supervisor_terminate_process_fn terminate_process;
 raya_supervisor_wait_fn wait;
 raya_supervisor_get_exit_code_fn get_exit_code;
 void *context;
} raya_supervisor_api;

/* Launches a dedicated child suspended, assigns it to a kill-on-job-close Job,
 * verifies membership, then resumes it. command_line is mutable and must
 * include argv[0]. No handles are inherited by the child.
 * IMPORTANT: a false return can leave a DEGRADED object with retained handles
 * or even a live child (for example, if ResumeThread succeeded but closing
 * the primary-thread handle failed). The caller must retain the structure;
 * use wait/terminate_job as appropriate, then retry close. Never zero/free it.
 */
int raya_supervisor_start(raya_process_supervisor *s,
 const wchar_t *application, wchar_t *command_line,
 const wchar_t *working_directory);
int raya_supervisor_start_with_api(raya_process_supervisor *s,
 const wchar_t *application,wchar_t *command_line,
 const wchar_t *working_directory,const raya_supervisor_api *api);
/* Wait never closes handles. Timeout/failure preserves all ownership. */
int raya_supervisor_wait(raya_process_supervisor *s, DWORD timeout_ms);
int raya_supervisor_wait_with_api(raya_process_supervisor *s,DWORD timeout_ms,
                                  const raya_supervisor_api *api);
/* Explicit containment: terminates the whole assigned Job, then waits. */
int raya_supervisor_terminate_job(raya_process_supervisor *s, UINT exit_code);
int raya_supervisor_terminate_job_with_api(raya_process_supervisor *s,
 UINT exit_code,const raya_supervisor_api *api);
/* Close is allowed only after the process is positively signaled and waited. */
int raya_supervisor_close(raya_process_supervisor *s);
int raya_supervisor_close_with_api(raya_process_supervisor *s,
                                   const raya_supervisor_api *api);
#endif
