#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static int child_link(const wchar_t *link,const wchar_t *source){
  if(CreateHardLinkW(link,source,NULL)) return 0;
  return (int)GetLastError();
}
static int run_case(const wchar_t *label,DWORD access,DWORD share){
  wchar_t temp[MAX_PATH],source[MAX_PATH],link[MAX_PATH],volume[MAX_PATH],fs[64],exe[MAX_PATH],cmd[4*MAX_PATH];
  DWORD n=GetTempPathW(MAX_PATH,temp);
  if(!n||n>=MAX_PATH)return 90;
  _snwprintf_s(source,MAX_PATH,_TRUNCATE,L"%sraya-hardlink-v2-%lu-%ls.src",temp,GetCurrentProcessId(),label);
  _snwprintf_s(link,MAX_PATH,_TRUNCATE,L"%sraya-hardlink-v2-%lu-%ls.link",temp,GetCurrentProcessId(),label);
  DeleteFileW(source);DeleteFileW(link);
  HANDLE created=CreateFileW(source,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
  if(created==INVALID_HANDLE_VALUE){wprintf(L"%ls fixture-error=%lu\n",label,GetLastError());return 91;}
  CloseHandle(created);
  if(!GetVolumePathNameW(source,volume,MAX_PATH)||!GetVolumeInformationW(volume,NULL,0,NULL,NULL,NULL,fs,64)){
    DeleteFileW(source);return 92;
  }
  HANDLE held=CreateFileW(source,access,share,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
  if(held==INVALID_HANDLE_VALUE){wprintf(L"%ls open-error=%lu\n",label,GetLastError());DeleteFileW(source);return 93;}
  BY_HANDLE_FILE_INFORMATION before={0};
  GetFileInformationByHandle(held,&before);
  n=GetModuleFileNameW(NULL,exe,MAX_PATH);
  if(!n||n>=MAX_PATH){CloseHandle(held);DeleteFileW(source);return 94;}
  _snwprintf_s(cmd,_countof(cmd),_TRUNCATE,L"\"%ls\" --link \"%ls\" \"%ls\"",exe,link,source);
  STARTUPINFOW si={sizeof(si)};PROCESS_INFORMATION pi={0};
  if(!CreateProcessW(NULL,cmd,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi)){
    DWORD e=GetLastError();wprintf(L"%ls child-launch-error=%lu\n",label,e);CloseHandle(held);DeleteFileW(source);return 95;
  }
  DWORD wait=WaitForSingleObject(pi.hProcess,10000),exitCode=999;
  if(wait==WAIT_OBJECT_0)GetExitCodeProcess(pi.hProcess,&exitCode);
  CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
  BY_HANDLE_FILE_INFORMATION after={0};GetFileInformationByHandle(held,&after);
  wprintf(L"%ls fs=%ls access=0x%08lx share=0x%08lx childExit=%lu linkExists=%ls linksBefore=%lu linksAfter=%lu\n",
    label,fs,access,share,exitCode,GetFileAttributesW(link)!=INVALID_FILE_ATTRIBUTES?L"yes":L"no",
    before.nNumberOfLinks,after.nNumberOfLinks);
  CloseHandle(held);DeleteFileW(link);DeleteFileW(source);
  return 0;
}
int wmain(int argc,wchar_t**argv){
  if(argc==4&&wcscmp(argv[1],L"--link")==0)return child_link(argv[2],argv[3]);
  int r=0;
  r|=run_case(L"genericread-share0",GENERIC_READ,0);
  r|=run_case(L"readattrs-share0",FILE_READ_ATTRIBUTES|SYNCHRONIZE,0);
  r|=run_case(L"readdata-share0",FILE_READ_DATA|SYNCHRONIZE,0);
  r|=run_case(L"readattrs-share-read",FILE_READ_ATTRIBUTES|SYNCHRONIZE,FILE_SHARE_READ);
  r|=run_case(L"readattrs-share-all",FILE_READ_ATTRIBUTES|SYNCHRONIZE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE);
  return r?1:0;
}
