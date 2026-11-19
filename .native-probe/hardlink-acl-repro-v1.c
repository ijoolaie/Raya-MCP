#define UNICODE
#define _UNICODE
#include <windows.h>
#include <aclapi.h>
#include <stdio.h>
#include <wchar.h>

static int child_link(const wchar_t *link,const wchar_t *source){
 if(CreateHardLinkW(link,source,NULL))return 0;
 return (int)GetLastError();
}
static int child_relax_and_link(const wchar_t *link,const wchar_t *source){
 DWORD e=SetNamedSecurityInfoW((LPWSTR)source,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,NULL,NULL,NULL,NULL);
 if(e!=ERROR_SUCCESS)return (int)e;
 return child_link(link,source);
}
static int run_child(const wchar_t *exe,const wchar_t *link,const wchar_t *source,DWORD *exitCode){
 wchar_t cmd[4*MAX_PATH];_snwprintf_s(cmd,_countof(cmd),_TRUNCATE,L"\"%ls\" --link \"%ls\" \"%ls\"",exe,link,source);
 STARTUPINFOW si={sizeof(si)};PROCESS_INFORMATION pi={0};
 if(!CreateProcessW(NULL,cmd,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi))return (int)GetLastError();
 DWORD wait=WaitForSingleObject(pi.hProcess,10000);*exitCode=999;
 if(wait==WAIT_OBJECT_0)GetExitCodeProcess(pi.hProcess,exitCode);
 CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
 return wait==WAIT_OBJECT_0?0:ERROR_TIMEOUT;
}
int wmain(int argc,wchar_t**argv){
 if(argc==4&&wcscmp(argv[1],L"--link")==0)return child_link(argv[2],argv[3]);
 if(argc==4&&wcscmp(argv[1],L"--relax-link")==0)return child_relax_and_link(argv[2],argv[3]);
 wchar_t temp[MAX_PATH],source[MAX_PATH],link[MAX_PATH],exe[MAX_PATH],volume[MAX_PATH],fs[64];
 DWORD n=GetTempPathW(MAX_PATH,temp);if(!n||n>=MAX_PATH)return 90;
 _snwprintf_s(source,MAX_PATH,_TRUNCATE,L"%sraya-hardlink-acl-%lu.src",temp,GetCurrentProcessId());
 _snwprintf_s(link,MAX_PATH,_TRUNCATE,L"%sraya-hardlink-acl-%lu.link",temp,GetCurrentProcessId());
 DeleteFileW(source);DeleteFileW(link);
 HANDLE f=CreateFileW(source,GENERIC_READ|GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
 if(f==INVALID_HANDLE_VALUE){wprintf(L"fixture-create-error=%lu\n",GetLastError());return 91;}
 CloseHandle(f);
 if(!GetVolumePathNameW(source,volume,MAX_PATH)||!GetVolumeInformationW(volume,NULL,0,NULL,NULL,NULL,fs,64)){DeleteFileW(source);return 92;}
 PSID userSid=NULL;PACL oldDacl=NULL;PSECURITY_DESCRIPTOR sd=NULL;HANDLE token=NULL;DWORD err=0;
 if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)){DeleteFileW(source);return 93;}
 DWORD need=0;GetTokenInformation(token,TokenUser,NULL,0,&need);TOKEN_USER *tu=(TOKEN_USER*)LocalAlloc(LPTR,need);
 if(!tu||!GetTokenInformation(token,TokenUser,tu,need,&need)){if(tu)LocalFree(tu);CloseHandle(token);DeleteFileW(source);return 94;}
 userSid=tu->User.Sid;
 err=GetNamedSecurityInfoW(source,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,NULL,NULL,&oldDacl,NULL,&sd);
 if(err!=ERROR_SUCCESS){wprintf(L"get-dacl-error=%lu\n",err);LocalFree(tu);CloseHandle(token);DeleteFileW(source);return 95;}
 EXPLICIT_ACCESSW deny={0};deny.grfAccessPermissions=FILE_WRITE_ATTRIBUTES;deny.grfAccessMode=DENY_ACCESS;deny.grfInheritance=NO_INHERITANCE;deny.Trustee.TrusteeForm=TRUSTEE_IS_SID;deny.Trustee.TrusteeType=TRUSTEE_IS_USER;deny.Trustee.ptstrName=(LPWSTR)userSid;
 PACL newDacl=NULL;err=SetEntriesInAclW(1,&deny,oldDacl,&newDacl);
 if(err!=ERROR_SUCCESS){wprintf(L"build-dacl-error=%lu\n",err);LocalFree(sd);LocalFree(tu);CloseHandle(token);DeleteFileW(source);return 96;}
 err=SetNamedSecurityInfoW(source,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,NULL,NULL,newDacl,NULL);
 if(err!=ERROR_SUCCESS){wprintf(L"set-dacl-error=%lu\n",err);LocalFree(newDacl);LocalFree(sd);LocalFree(tu);CloseHandle(token);DeleteFileW(source);return 97;}
 n=GetModuleFileNameW(NULL,exe,MAX_PATH);DWORD childExit=999;int launch=run_child(exe,link,source,&childExit);
 wprintf(L"acl-deny-write-attributes fs=%ls share-none childExit=%lu linkExists=%ls\\n",fs,childExit,GetFileAttributesW(link)!=INVALID_FILE_ATTRIBUTES?L"yes":L"no");
 // The owner can ordinarily rewrite its DACL. Characterize same-identity bypass.
 wchar_t relaxCmd[4*MAX_PATH];_snwprintf_s(relaxCmd,_countof(relaxCmd),_TRUNCATE,L"\"%ls\" --relax-link \"%ls\" \"%ls\"",exe,link,source);
 STARTUPINFOW rsi={sizeof(rsi)};PROCESS_INFORMATION rpi={0};DWORD relaxExit=999;int relaxLaunch=0;
 if(!CreateProcessW(NULL,relaxCmd,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&rsi,&rpi))relaxLaunch=(int)GetLastError();
 else {DWORD rw=WaitForSingleObject(rpi.hProcess,10000);if(rw==WAIT_OBJECT_0)GetExitCodeProcess(rpi.hProcess,&relaxExit);else relaxLaunch=ERROR_TIMEOUT;CloseHandle(rpi.hThread);CloseHandle(rpi.hProcess);}
 wprintf(L"same-identity-relax-dacl-then-link childExit=%lu linkExists=%ls launchError=%d\\n",relaxExit,GetFileAttributesW(link)!=INVALID_FILE_ATTRIBUTES?L"yes":L"no",relaxLaunch);
 // Restore the exact original DACL on this disposable fixture before cleanup.
 DWORD restore=SetNamedSecurityInfoW(source,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,NULL,NULL,oldDacl,NULL);
 wprintf(L"restore-original-dacl=%lu\\n",restore);
 DWORD ownerOpen=0;HANDLE check=CreateFileW(source,WRITE_DAC,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
 if(check!=INVALID_HANDLE_VALUE){ownerOpen=1;CloseHandle(check);}
 wprintf(L"same-token-owner-can-open-write-dac=%lu child-launch=%d\\n",ownerOpen,launch);
 LocalFree(newDacl);LocalFree(sd);LocalFree(tu);CloseHandle(token);
 DeleteFileW(link);DeleteFileW(source);
 return (launch||restore!=ERROR_SUCCESS)?1:0;
}