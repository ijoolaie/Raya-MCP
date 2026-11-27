#define _WIN32_WINNT 0x0602
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <wchar.h>
#include <stdint.h>
#include <string.h>
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#ifndef OBJ_DONT_REPARSE
#define OBJ_DONT_REPARSE 0x00001000L
#endif
typedef NTSTATUS (NTAPI *NtCreateFileFn)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
static unsigned g_pass, g_fail;
static void check(const wchar_t *label, BOOL ok, DWORD error) { if(ok){++g_pass;wprintf(L"PASS %ls\n",label);}else{++g_fail;wprintf(L"FAIL %ls (error=%lu)\n",label,(unsigned long)error);} }
static HANDLE open_relative(NtCreateFileFn nt,HANDLE parent,const wchar_t *leaf,BOOL directory){
 UNICODE_STRING name;OBJECT_ATTRIBUTES oa;IO_STATUS_BLOCK iosb;HANDLE child=INVALID_HANDLE_VALUE;size_t chars=wcslen(leaf);
 if(chars==0||chars>255||wcschr(leaf,L'\\')||wcschr(leaf,L'/')||!wcscmp(leaf,L".")||!wcscmp(leaf,L".."))return INVALID_HANDLE_VALUE;
 name.Buffer=(PWSTR)leaf;name.Length=(USHORT)(chars*sizeof(wchar_t));name.MaximumLength=name.Length;
 InitializeObjectAttributes(&oa,&name,OBJ_CASE_INSENSITIVE|OBJ_DONT_REPARSE,parent,NULL);
 NTSTATUS status=nt(&child,directory?(FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES|SYNCHRONIZE):(FILE_READ_DATA|FILE_READ_ATTRIBUTES|SYNCHRONIZE),&oa,&iosb,NULL,FILE_ATTRIBUTE_NORMAL,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,FILE_OPEN,FILE_SYNCHRONOUS_IO_NONALERT|FILE_OPEN_REPARSE_POINT|(directory?FILE_DIRECTORY_FILE:FILE_NON_DIRECTORY_FILE),NULL,0);
 if(status<0){SetLastError((DWORD)status);return INVALID_HANDLE_VALUE;}return child;
}
static BOOL read_expected(HANDLE parent,NtCreateFileFn nt,const wchar_t *leaf,const char *expected){
 HANDLE file=open_relative(nt,parent,leaf,FALSE);FILE_ATTRIBUTE_TAG_INFO tag;FILE_STANDARD_INFO standard;char data[128]={0};DWORD got=0;BOOL ok=FALSE;size_t len=strlen(expected);
 if(file==INVALID_HANDLE_VALUE)return FALSE;
 if(!GetFileInformationByHandleEx(file,FileAttributeTagInfo,&tag,sizeof(tag))||!GetFileInformationByHandleEx(file,FileStandardInfo,&standard,sizeof(standard))||(tag.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||standard.Directory||standard.NumberOfLinks!=1||standard.EndOfFile.QuadPart<0||(uint64_t)standard.EndOfFile.QuadPart>sizeof(data)){CloseHandle(file);return FALSE;}
 ok=ReadFile(file,data,(DWORD)standard.EndOfFile.QuadPart,&got,NULL)&&got==len&&got==(DWORD)standard.EndOfFile.QuadPart&&!memcmp(data,expected,len);CloseHandle(file);return ok;
}
static BOOL write_text(const wchar_t *path,const char *value){HANDLE file=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);DWORD written=0;BOOL ok;if(file==INVALID_HANDLE_VALUE)return FALSE;ok=WriteFile(file,value,(DWORD)strlen(value),&written,NULL)&&written==strlen(value);CloseHandle(file);return ok;}
int wmain(void){
 HMODULE ntdll=GetModuleHandleW(L"ntdll.dll");NtCreateFileFn nt=ntdll?(NtCreateFileFn)GetProcAddress(ntdll,"NtCreateFile"):NULL;
 wchar_t temp[MAX_PATH],fixture[MAX_PATH],root[MAX_PATH],root_old[MAX_PATH],sub[MAX_PATH],sub_old[MAX_PATH],decoy[MAX_PATH],path[MAX_PATH];DWORD n;
 HANDLE root_handle=INVALID_HANDLE_VALUE,sub_handle=INVALID_HANDLE_VALUE;BOOL root_renamed=FALSE,sub_renamed=FALSE;int rc=0;
 if(!nt){wprintf(L"FATAL NtCreateFile unavailable\n");return 2;}n=GetTempPathW(_countof(temp),temp);if(!n||n>=_countof(temp))return 2;
 if(swprintf_s(fixture,_countof(fixture),L"%lsRayaRootPin-%lu-%lu",temp,(unsigned long)GetCurrentProcessId(),(unsigned long)GetTickCount())<0)return 2;
 if(!CreateDirectoryW(fixture,NULL))return 2;
 if(swprintf_s(root,_countof(root),L"%ls\\root",fixture)<0||swprintf_s(root_old,_countof(root_old),L"%ls\\root-old",fixture)<0||swprintf_s(sub,_countof(sub),L"%ls\\root\\sub",fixture)<0||swprintf_s(sub_old,_countof(sub_old),L"%ls\\root\\sub-old",fixture)<0||swprintf_s(decoy,_countof(decoy),L"%ls\\decoy",fixture)<0){rc=2;goto cleanup;}
 if(!CreateDirectoryW(root,NULL)||!CreateDirectoryW(sub,NULL)||!CreateDirectoryW(decoy,NULL)){rc=2;goto cleanup;}
 if(swprintf_s(path,_countof(path),L"%ls\\root.txt",root)<0||!write_text(path,"ROOT-PINNED-CONTENT")){rc=2;goto cleanup;}
 if(swprintf_s(path,_countof(path),L"%ls\\sub.txt",sub)<0||!write_text(path,"PARENT-PINNED-CONTENT")){rc=2;goto cleanup;}
 if(swprintf_s(path,_countof(path),L"%ls\\root.txt",decoy)<0||!write_text(path,"ROOT-DECOY-CONTENT")){rc=2;goto cleanup;}
 if(swprintf_s(path,_countof(path),L"%ls\\sub.txt",decoy)<0||!write_text(path,"PARENT-DECOY-CONTENT")){rc=2;goto cleanup;}
 root_handle=CreateFileW(root,FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES|SYNCHRONIZE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
 check(L"open and pin configured root directory HANDLE",root_handle!=INVALID_HANDLE_VALUE,root_handle!=INVALID_HANDLE_VALUE?ERROR_SUCCESS:GetLastError());if(root_handle==INVALID_HANDLE_VALUE){rc=1;goto cleanup;}
 SetLastError(ERROR_SUCCESS);{BOOL moved=MoveFileExW(root,root_old,MOVEFILE_WRITE_THROUGH);check(L"rename root path while root HANDLE remains pinned",moved,moved?ERROR_SUCCESS:GetLastError());if(!moved){rc=1;goto cleanup;}}root_renamed=TRUE;
 check(L"create replacement directory at original root path",CreateDirectoryW(root,NULL),GetLastError());
 check(L"create decoy beneath replacement root",(swprintf_s(path,_countof(path),L"%ls\\root.txt",root)>=0)&&write_text(path,"ROOT-REPLACEMENT-CONTENT"),GetLastError());
 swprintf_s(sub,_countof(sub),L"%ls\\sub",root_old);swprintf_s(sub_old,_countof(sub_old),L"%ls\\sub-old",root_old);
 sub_handle=open_relative(nt,root_handle,L"sub",TRUE);check(L"open child directory relative to pinned root HANDLE",sub_handle!=INVALID_HANDLE_VALUE,sub_handle!=INVALID_HANDLE_VALUE?ERROR_SUCCESS:GetLastError());if(sub_handle==INVALID_HANDLE_VALUE){rc=1;goto cleanup;}
 SetLastError(ERROR_SUCCESS);{BOOL moved=MoveFileExW(sub,sub_old,MOVEFILE_WRITE_THROUGH);check(L"rename child path while child HANDLE remains pinned",moved,moved?ERROR_SUCCESS:GetLastError());if(!moved){rc=1;goto cleanup;}}sub_renamed=TRUE;
 check(L"create replacement child directory",CreateDirectoryW(sub,NULL),GetLastError());
 check(L"create decoy beneath replacement child",(swprintf_s(path,_countof(path),L"%ls\\sub.txt",sub)>=0)&&write_text(path,"PARENT-REPLACEMENT-CONTENT"),GetLastError());
 check(L"root HANDLE-relative open reads original root object, not replacement",read_expected(root_handle,nt,L"root.txt","ROOT-PINNED-CONTENT"),GetLastError());
 check(L"parent HANDLE-relative open reads original child object, not replacement",read_expected(sub_handle,nt,L"sub.txt","PARENT-PINNED-CONTENT"),GetLastError());
cleanup:
 if(sub_handle!=INVALID_HANDLE_VALUE)CloseHandle(sub_handle);if(root_handle!=INVALID_HANDLE_VALUE)CloseHandle(root_handle);
 if(sub_renamed){swprintf_s(path,_countof(path),L"%ls\\sub.txt",sub);DeleteFileW(path);RemoveDirectoryW(sub);swprintf_s(path,_countof(path),L"%ls\\sub.txt",sub_old);DeleteFileW(path);RemoveDirectoryW(sub_old);}else if(GetFileAttributesW(sub)!=INVALID_FILE_ATTRIBUTES){swprintf_s(path,_countof(path),L"%ls\\sub.txt",sub);DeleteFileW(path);RemoveDirectoryW(sub);}
 if(root_renamed){swprintf_s(path,_countof(path),L"%ls\\root.txt",root);DeleteFileW(path);RemoveDirectoryW(root);swprintf_s(path,_countof(path),L"%ls\\root.txt",root_old);DeleteFileW(path);RemoveDirectoryW(root_old);}else if(GetFileAttributesW(root)!=INVALID_FILE_ATTRIBUTES){swprintf_s(path,_countof(path),L"%ls\\root.txt",root);DeleteFileW(path);RemoveDirectoryW(root);}
 swprintf_s(path,_countof(path),L"%ls\\root.txt",decoy);DeleteFileW(path);swprintf_s(path,_countof(path),L"%ls\\sub.txt",decoy);DeleteFileW(path);RemoveDirectoryW(decoy);
 if(GetFileAttributesW(root)!=INVALID_FILE_ATTRIBUTES){BOOL removed=RemoveDirectoryW(root);check(L"cleanup replacement root directory",removed,removed?ERROR_SUCCESS:GetLastError());}
 if(GetFileAttributesW(root_old)!=INVALID_FILE_ATTRIBUTES){BOOL removed=RemoveDirectoryW(root_old);check(L"cleanup renamed original root directory",removed,removed?ERROR_SUCCESS:GetLastError());}
 {BOOL removed=RemoveDirectoryW(fixture);check(L"cleanup unique fixture directory",removed,removed?ERROR_SUCCESS:GetLastError());}
 wprintf(L"RESULT pass=%u fail=%u\n",g_pass,g_fail);return(rc==0&&g_fail==0)?0:1;
}
