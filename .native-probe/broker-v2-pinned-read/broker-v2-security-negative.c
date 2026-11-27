#define UNICODE
#define _UNICODE
#include <windows.h>
#include <aclapi.h>
#include <stdio.h>
static BOOL expect_denied(const wchar_t *label,const wchar_t *path,DWORD access,BOOL dir){HANDLE h=CreateFileW(path,access,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,dir?FILE_FLAG_BACKUP_SEMANTICS:FILE_ATTRIBUTE_NORMAL,NULL);if(h!=INVALID_HANDLE_VALUE){CloseHandle(h);wprintf(L"FAIL %ls unexpectedly granted\n",label);return FALSE;}DWORD e=GetLastError();if(e!=ERROR_ACCESS_DENIED){wprintf(L"FAIL %ls error=%lu (expected ACCESS_DENIED)\n",label,e);return FALSE;}wprintf(L"PASS %ls denied\n",label);return TRUE;}
int wmain(void){HANDLE t=NULL,r=NULL;BOOL ok=TRUE;BYTE adminbuf[SECURITY_MAX_SID_SIZE];DWORD admins=sizeof(adminbuf);if(!CreateWellKnownSid(WinBuiltinAdministratorsSid,NULL,adminbuf,&admins)){wprintf(L"CreateWellKnownSid=%lu\n",GetLastError());return 1;}if(!OpenProcessToken(GetCurrentProcess(),TOKEN_DUPLICATE|TOKEN_QUERY|TOKEN_IMPERSONATE,&t)){wprintf(L"OpenProcessToken=%lu\n",GetLastError());return 2;}SID_AND_ATTRIBUTES dis={(PSID)adminbuf,0};if(!CreateRestrictedToken(t,DISABLE_MAX_PRIVILEGE,1,&dis,0,NULL,0,NULL,&r)){wprintf(L"CreateRestrictedToken=%lu\n",GetLastError());return 3;}if(!ImpersonateLoggedOnUser(r)){wprintf(L"ImpersonateLoggedOnUser=%lu\n",GetLastError());return 4;}
const wchar_t *file=L"C:\\ProgramData\\RayaFsBrokerV2Probe-20261127\\payload.txt";const wchar_t *root=L"C:\\ProgramData\\RayaFsBrokerV2Probe-20261127";const wchar_t *image=L"C:\\ProgramData\\RayaFsBrokerV2ProbeBin-20261127\\named-pipe-broker.exe";
ok &= expect_denied(L"fixture FILE_WRITE_DATA",file,GENERIC_WRITE,FALSE);
ok &= expect_denied(L"fixture WRITE_DAC",file,WRITE_DAC,FALSE);
ok &= expect_denied(L"fixture WRITE_OWNER",file,WRITE_OWNER,FALSE);
ok &= expect_denied(L"fixture FILE_WRITE_ATTRIBUTES",file,FILE_WRITE_ATTRIBUTES,FALSE);
ok &= expect_denied(L"fixture directory FILE_ADD_FILE",root,FILE_ADD_FILE|FILE_ADD_SUBDIRECTORY,TRUE);
ok &= expect_denied(L"broker image GENERIC_WRITE",image,GENERIC_WRITE,FALSE);
{BOOL linked=CreateHardLinkW(L"C:\\ProgramData\\RayaFsBrokerV2Probe-20261127\\user-link.tmp",file,NULL);DWORD le=linked?ERROR_SUCCESS:GetLastError();if(linked){DeleteFileW(L"C:\\ProgramData\\RayaFsBrokerV2Probe-20261127\\user-link.tmp");wprintf(L"FAIL hardlink unexpectedly created\\n");ok=FALSE;}else if(le==ERROR_ACCESS_DENIED){wprintf(L"PASS fixture hardlink creation denied\\n");}else{wprintf(L"FAIL hardlink error=%lu (expected ACCESS_DENIED)\\n",le);ok=FALSE;}}
if(!RevertToSelf()){wprintf(L"RevertToSelf=%lu\n",GetLastError());ok=FALSE;}CloseHandle(r);CloseHandle(t);return ok?0:5;}