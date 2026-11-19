#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <node_api.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifndef OBJ_DONT_REPARSE
#define OBJ_DONT_REPARSE 0x00001000L
#endif
#define RAYA_MAX_WRITE_BYTES (1024u * 1024u)

typedef NTSTATUS (NTAPI *NtCreateFileFn)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
typedef NTSTATUS (NTAPI *NtSetInformationFileFn)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);

typedef struct RAYA_FILE_RENAME_INFORMATION {
  BOOLEAN ReplaceIfExists;
  HANDLE RootDirectory;
  ULONG FileNameLength;
  WCHAR FileName[1];
} RAYA_FILE_RENAME_INFORMATION;

typedef struct ParentHandleRef { HANDLE handle; int closed; } ParentHandleRef;

static volatile LONG g_fail_write = 0;
static volatile LONG g_fail_flush = 0;
static volatile LONG g_fail_close_after_commit = 0;

static ParentHandleRef *unwrap_parent(napi_env env, napi_value value) {
  void *data = NULL;
  if (napi_unwrap(env, value, &data) != napi_ok || !data) return NULL;
  return (ParentHandleRef *)data;
}
static int get_string(napi_env env, napi_value value, char **out) {
  size_t len=0;
  if (napi_get_value_string_utf8(env,value,NULL,0,&len)!=napi_ok || len>1024) return 0;
  char *buf=(char*)calloc(len+1,1); if(!buf) return 0;
  if(napi_get_value_string_utf8(env,value,buf,len+1,&len)!=napi_ok){free(buf);return 0;}
  *out=buf;return 1;
}
static int utf8_to_wide(const char *src,wchar_t **out) {
  int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,src,-1,NULL,0); if(n<=0)return 0;
  wchar_t *p=(wchar_t*)calloc((size_t)n,sizeof(wchar_t)); if(!p)return 0;
  if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,src,-1,p,n)){free(p);return 0;}
  *out=p;return 1;
}
static int safe_leaf(const wchar_t *name,size_t len) {
  if(!name||len==0||len>255||name[len-1]==L'.'||name[len-1]==L' ')return 0;
  if(!_wcsicmp(name,L".")||!_wcsicmp(name,L".."))return 0;
  wchar_t base[256];size_t n=0;while(n<len&&name[n]!=L'.'){base[n]=name[n];n++;}
  while(n&&(base[n-1]==L'.'||base[n-1]==L' '))n--;base[n]=0;
  if(!_wcsicmp(base,L"CON")||!_wcsicmp(base,L"PRN")||!_wcsicmp(base,L"AUX")||!_wcsicmp(base,L"NUL")||!_wcsicmp(base,L"CONIN$")||!_wcsicmp(base,L"CONOUT$"))return 0;
  if(n==4&&(!_wcsnicmp(base,L"COM",3)||!_wcsnicmp(base,L"LPT",3))&&((base[3]>=L'1'&&base[3]<=L'9')||base[3]==0x00B9||base[3]==0x00B2||base[3]==0x00B3))return 0;
  for(size_t i=0;i<len;i++)if(name[i]<32||wcschr(L"\\/:|?*<>\"",name[i]))return 0;
  return 1;
}
static void throw_status(napi_env env,const char *prefix,NTSTATUS status) {
  char msg[256];_snprintf_s(msg,sizeof(msg),_TRUNCATE,"%s (NTSTATUS 0x%08lX)",prefix,(unsigned long)status);napi_throw_error(env,NULL,msg);
}
static napi_value arm(napi_env env,napi_callback_info info,volatile LONG *slot) {
  size_t argc=0;if(napi_get_cb_info(env,info,&argc,NULL,NULL,NULL)!=napi_ok||argc){napi_throw_type_error(env,NULL,"fault hook takes no arguments");return NULL;}
  if(InterlockedCompareExchange(slot,1,0)){napi_throw_error(env,NULL,"fault already armed");return NULL;}
  napi_value v;napi_get_boolean(env,TRUE,&v);return v;
}
static napi_value fail_write(napi_env e,napi_callback_info i){return arm(e,i,&g_fail_write);}
static napi_value fail_flush(napi_env e,napi_callback_info i){return arm(e,i,&g_fail_flush);}
static napi_value fail_close(napi_env e,napi_callback_info i){return arm(e,i,&g_fail_close_after_commit);}

static void discard_temp(HANDLE *h) {
  if(!h||*h==INVALID_HANDLE_VALUE)return;
  FILE_DISPOSITION_INFO disposition;disposition.DeleteFile=TRUE;
  (void)SetFileInformationByHandle(*h,FileDispositionInfo,&disposition,sizeof(disposition));
  HANDLE owned=*h;*h=INVALID_HANDLE_VALUE;(void)CloseHandle(owned);
}

/* Publish bytes as a new sibling only. Existing destinations are never replaced. */
static napi_value publish_child_exclusive_relative(napi_env env,napi_callback_info info) {
  size_t argc=5;napi_value argv[5],result=NULL,value;
  ParentHandleRef *parent=NULL;
  char *temp8=NULL,*dest8=NULL;wchar_t *tempName=NULL,*destName=NULL;
  void *data=NULL;size_t dataLen=0;HANDLE temp=INVALID_HANDLE_VALUE;
  int committed=0;
  HMODULE ntdll=GetModuleHandleW(L"ntdll.dll");
  NtCreateFileFn ntCreate=ntdll?(NtCreateFileFn)GetProcAddress(ntdll,"NtCreateFile"):NULL;
  NtSetInformationFileFn ntSet=ntdll?(NtSetInformationFileFn)GetProcAddress(ntdll,"NtSetInformationFile"):NULL;
  if(napi_get_cb_info(env,info,&argc,argv,NULL,NULL)!=napi_ok||(argc!=4&&argc!=5)){napi_throw_type_error(env,NULL,"publishChildExclusiveRelative(parent,tempLeaf,destinationLeaf,buffer[,testAfterTempOpen]) requires four or five arguments");return NULL;}
  if(argc==5){napi_valuetype callbackType;if(napi_typeof(env,argv[4],&callbackType)!=napi_ok||callbackType!=napi_function){napi_throw_type_error(env,NULL,"testAfterTempOpen must be a function");return NULL;}}
  parent=unwrap_parent(env,argv[0]);
  if(!parent||parent->closed||parent->handle==INVALID_HANDLE_VALUE){napi_throw_error(env,NULL,"parent HANDLE owner is closed or invalid");return NULL;}
  if(!get_string(env,argv[1],&temp8)||!get_string(env,argv[2],&dest8)||!utf8_to_wide(temp8,&tempName)||!utf8_to_wide(dest8,&destName)){
    napi_throw_type_error(env,NULL,"leaf names must be valid UTF-8");goto cleanup;
  }
  size_t tempLen=wcslen(tempName),destLen=wcslen(destName);
  if(!safe_leaf(tempName,tempLen)||!safe_leaf(destName,destLen)||_wcsicmp(tempName,destName)==0){napi_throw_type_error(env,NULL,"invalid_name: temporary and destination leaves must be distinct safe components");goto cleanup;}
  bool isBuffer=false;if(napi_is_buffer(env,argv[3],&isBuffer)!=napi_ok||!isBuffer||napi_get_buffer_info(env,argv[3],&data,&dataLen)!=napi_ok||dataLen>RAYA_MAX_WRITE_BYTES){napi_throw_range_error(env,NULL,"content must be a Buffer no larger than 1 MiB");goto cleanup;}
  FILE_ATTRIBUTE_TAG_INFO tag;FILE_STANDARD_INFO standard;
  if(!GetFileInformationByHandleEx(parent->handle,FileAttributeTagInfo,&tag,sizeof(tag))||(tag.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||!GetFileInformationByHandleEx(parent->handle,FileStandardInfo,&standard,sizeof(standard))||!standard.Directory){napi_throw_error(env,NULL,"parent is not a plain directory HANDLE");goto cleanup;}
  if(!ntCreate||!ntSet){napi_throw_error(env,NULL,"required NT file APIs unavailable; failing closed");goto cleanup;}

  UNICODE_STRING name;name.Buffer=tempName;name.Length=(USHORT)(tempLen*sizeof(wchar_t));name.MaximumLength=name.Length;
  OBJECT_ATTRIBUTES oa;InitializeObjectAttributes(&oa,&name,OBJ_CASE_INSENSITIVE|OBJ_DONT_REPARSE,parent->handle,NULL);
  IO_STATUS_BLOCK iosb;
  NTSTATUS status=ntCreate(&temp,DELETE|FILE_WRITE_DATA|FILE_READ_ATTRIBUTES|SYNCHRONIZE,&oa,&iosb,NULL,FILE_ATTRIBUTE_NORMAL,0,FILE_CREATE,FILE_SYNCHRONOUS_IO_NONALERT|FILE_OPEN_REPARSE_POINT|FILE_NON_DIRECTORY_FILE,NULL,0);
  if(status<0){throw_status(env,"temporary_create_failed",status);goto cleanup;}
  /* Test-only synchronization point to create a hard link while the new temp HANDLE is held. */
  if(argc==5){napi_value global,ignored;if(napi_get_global(env,&global)!=napi_ok||napi_call_function(env,global,argv[4],0,NULL,&ignored)!=napi_ok){napi_throw_error(env,NULL,"test_after_temp_open callback failed");goto cleanup;}}
  if(InterlockedCompareExchange(&g_fail_write,0,1)==1){napi_throw_error(env,NULL,"injected_write_failure");goto cleanup;}
  DWORD written=0;
  if(dataLen){if(!WriteFile(temp,data,(DWORD)dataLen,&written,NULL)||written!=dataLen){throw_status(env,"temporary_write_failed",(NTSTATUS)HRESULT_FROM_WIN32(GetLastError()));goto cleanup;}}
  if(InterlockedCompareExchange(&g_fail_flush,0,1)==1){napi_throw_error(env,NULL,"injected_flush_failure");goto cleanup;}
  if(!FlushFileBuffers(temp)){throw_status(env,"temporary_flush_failed",(NTSTATUS)HRESULT_FROM_WIN32(GetLastError()));goto cleanup;}

  size_t bytes=FIELD_OFFSET(RAYA_FILE_RENAME_INFORMATION,FileName)+(destLen*sizeof(wchar_t));
  RAYA_FILE_RENAME_INFORMATION *renameInfo=(RAYA_FILE_RENAME_INFORMATION*)calloc(bytes,1);
  if(!renameInfo){napi_throw_error(env,NULL,"out of memory building publish information");goto cleanup;}
  renameInfo->ReplaceIfExists=FALSE;renameInfo->RootDirectory=parent->handle;renameInfo->FileNameLength=(ULONG)(destLen*sizeof(wchar_t));memcpy(renameInfo->FileName,destName,destLen*sizeof(wchar_t));
  IO_STATUS_BLOCK renameIosb;NTSTATUS renameStatus=ntSet(temp,&renameIosb,renameInfo,(ULONG)bytes,(FILE_INFORMATION_CLASS)10);free(renameInfo);
  if(renameStatus<0){throw_status(env,"publish_failed_without_overwrite",renameStatus);goto cleanup;}
  committed=1;
  if(!CloseHandle(temp)){temp=INVALID_HANDLE_VALUE;napi_throw_error(env,NULL,"committed_but_cleanup_failed: publish committed but temporary HANDLE close failed");goto cleanup;}
  temp=INVALID_HANDLE_VALUE;
  if(InterlockedCompareExchange(&g_fail_close_after_commit,0,1)==1){napi_throw_error(env,NULL,"committed_but_cleanup_failed: injected close-status failure after real HANDLE close");goto cleanup;}
  if(napi_create_object(env,&result)!=napi_ok||napi_get_boolean(env,TRUE,&value)!=napi_ok||napi_set_named_property(env,result,"committed",value)!=napi_ok||napi_create_double(env,(double)dataLen,&value)!=napi_ok||napi_set_named_property(env,result,"bytesWritten",value)!=napi_ok||napi_get_boolean(env,FALSE,&value)!=napi_ok||napi_set_named_property(env,result,"replacedExisting",value)!=napi_ok)goto cleanup;
  free(temp8);free(dest8);free(tempName);free(destName);return result;
cleanup:
  if(temp!=INVALID_HANDLE_VALUE){if(committed){HANDLE owned=temp;temp=INVALID_HANDLE_VALUE;(void)CloseHandle(owned);}else discard_temp(&temp);}
  free(temp8);free(dest8);free(tempName);free(destName);return NULL;
}
static napi_value init(napi_env env,napi_value exports){
  napi_value fn;
  if(napi_create_function(env,"publishChildExclusiveRelative",NAPI_AUTO_LENGTH,publish_child_exclusive_relative,NULL,&fn)!=napi_ok||napi_set_named_property(env,exports,"publishChildExclusiveRelative",fn)!=napi_ok)return NULL;
  if(napi_create_function(env,"failNextWriteForTest",NAPI_AUTO_LENGTH,fail_write,NULL,&fn)!=napi_ok||napi_set_named_property(env,exports,"failNextWriteForTest",fn)!=napi_ok)return NULL;
  if(napi_create_function(env,"failNextFlushForTest",NAPI_AUTO_LENGTH,fail_flush,NULL,&fn)!=napi_ok||napi_set_named_property(env,exports,"failNextFlushForTest",fn)!=napi_ok)return NULL;
  if(napi_create_function(env,"failNextCloseAfterCommitForTest",NAPI_AUTO_LENGTH,fail_close,NULL,&fn)!=napi_ok||napi_set_named_property(env,exports,"failNextCloseAfterCommitForTest",fn)!=napi_ok)return NULL;
  return exports;
}
NAPI_MODULE(NODE_GYP_MODULE_NAME,init)