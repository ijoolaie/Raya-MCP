#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <node_api.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <wchar.h>

#ifndef OBJ_DONT_REPARSE
#define OBJ_DONT_REPARSE 0x00001000L
#endif

typedef NTSTATUS (NTAPI *NtCreateFileFn)(
  PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK,
  PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
typedef NTSTATUS (NTAPI *NtSetInformationFileFn)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);
typedef struct RAYA_FILE_RENAME_INFORMATION {
  BOOLEAN ReplaceIfExists;
  HANDLE RootDirectory;
  ULONG FileNameLength;
  WCHAR FileName[1];
} RAYA_FILE_RENAME_INFORMATION;

typedef struct ParentHandleRef {
  HANDLE handle;
  int closed;
} ParentHandleRef;

static volatile LONG g_fail_before_rename = 0;
static volatile LONG g_fail_close_after_commit = 0;

static ParentHandleRef *unwrap_parent(napi_env env, napi_value value) {
  void *data = NULL;
  if (napi_unwrap(env, value, &data) != napi_ok || !data) return NULL;
  return (ParentHandleRef *)data;
}

static int get_utf8(napi_env env, napi_value value, char **out) {
  size_t len = 0;
  if (napi_get_value_string_utf8(env, value, NULL, 0, &len) != napi_ok || len > 1024) return 0;
  char *buf = (char *)calloc(len + 1, 1);
  if (!buf) return 0;
  if (napi_get_value_string_utf8(env, value, buf, len + 1, &len) != napi_ok) {
    free(buf); return 0;
  }
  *out = buf;
  return 1;
}

static int utf8_to_wide(const char *src, wchar_t **out) {
  int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, NULL, 0);
  if (needed <= 0) return 0;
  wchar_t *buf = (wchar_t *)calloc((size_t)needed, sizeof(wchar_t));
  if (!buf) return 0;
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, buf, needed)) {
    free(buf); return 0;
  }
  *out = buf;
  return 1;
}

static int safe_leaf(const wchar_t *name, size_t len) {
  if (!name || len == 0 || len > 255 || name[len - 1] == L'.' || name[len - 1] == L' ') return 0;
  if (!_wcsicmp(name, L".") || !_wcsicmp(name, L"..")) return 0;
  wchar_t base[256]; size_t n = 0;
  while (n < len && name[n] != L'.') { base[n] = name[n]; ++n; }
  while (n && (base[n - 1] == L'.' || base[n - 1] == L' ')) --n;
  base[n] = 0;
  if (!_wcsicmp(base,L"CON") || !_wcsicmp(base,L"PRN") || !_wcsicmp(base,L"AUX") ||
      !_wcsicmp(base,L"NUL") || !_wcsicmp(base,L"CONIN$") || !_wcsicmp(base,L"CONOUT$")) return 0;
  if (n == 4 && (!_wcsnicmp(base,L"COM",3) || !_wcsnicmp(base,L"LPT",3)) &&
      ((base[3] >= L'1' && base[3] <= L'9') || base[3] == 0x00B9 || base[3] == 0x00B2 || base[3] == 0x00B3)) return 0;
  for (size_t i=0;i<len;i++) if (name[i] < 32 || wcschr(L"\\/:|?*<>\"",name[i])) return 0;
  return 1;
}

static void throw_win32(napi_env env, const char *prefix, DWORD error) {
  char msg[256];
  _snprintf_s(msg,sizeof(msg),_TRUNCATE,"%s (Win32 error %lu)",prefix,(unsigned long)error);
  napi_throw_error(env,NULL,msg);
}

static napi_value arm_fault(napi_env env, napi_callback_info info, volatile LONG *slot, const char *name) {
  size_t argc=0; (void)info;
  if (napi_get_cb_info(env,info,&argc,NULL,NULL,NULL)!=napi_ok || argc!=0) {
    napi_throw_type_error(env,NULL,"fault arming function takes no arguments"); return NULL;
  }
  if (InterlockedCompareExchange(slot,1,0)!=0) {
    napi_throw_error(env,NULL,"fault already armed"); return NULL;
  }
  napi_value v; napi_get_boolean(env,TRUE,&v); (void)name; return v;
}
static napi_value fail_before(napi_env e,napi_callback_info i) {
  return arm_fault(e,i,&g_fail_before_rename,"before");
}
static napi_value fail_close_after(napi_env e,napi_callback_info i) {
  return arm_fault(e,i,&g_fail_close_after_commit,"close");
}

static napi_value rename_child_relative(napi_env env, napi_callback_info info) {
  size_t argc=4; napi_value argv[4], result=NULL, value;
  ParentHandleRef *srcParent=NULL,*dstParent=NULL;
  char *src8=NULL,*dst8=NULL; wchar_t *srcName=NULL,*dstName=NULL;
  HANDLE source=INVALID_HANDLE_VALUE;
  FILE_ID_INFO srcVolume,dstVolume;
  FILE_ATTRIBUTE_TAG_INFO tag;
  FILE_STANDARD_INFO standard;
  HMODULE ntdll=GetModuleHandleW(L"ntdll.dll");
  NtCreateFileFn ntCreateFile=ntdll?(NtCreateFileFn)GetProcAddress(ntdll,"NtCreateFile"):NULL;
  NtSetInformationFileFn ntSetInformationFile=ntdll?(NtSetInformationFileFn)GetProcAddress(ntdll,"NtSetInformationFile"):NULL;
  if (napi_get_cb_info(env,info,&argc,argv,NULL,NULL)!=napi_ok || argc!=4) {
    napi_throw_type_error(env,NULL,"renameChildRelative(sourceParent, sourceLeaf, destinationParent, destinationLeaf) requires four arguments");
    return NULL;
  }
  srcParent=unwrap_parent(env,argv[0]); dstParent=unwrap_parent(env,argv[2]);
  if (!srcParent || !dstParent || srcParent->closed || dstParent->closed ||
      srcParent->handle==INVALID_HANDLE_VALUE || dstParent->handle==INVALID_HANDLE_VALUE) {
    napi_throw_error(env,NULL,"parent HANDLE owner is closed or invalid"); return NULL;
  }
  if (!get_utf8(env,argv[1],&src8) || !get_utf8(env,argv[3],&dst8) ||
      !utf8_to_wide(src8,&srcName) || !utf8_to_wide(dst8,&dstName)) {
    napi_throw_type_error(env,NULL,"leaf names must be valid UTF-8"); goto cleanup;
  }
  size_t srcLen=wcslen(srcName),dstLen=wcslen(dstName);
  if (!safe_leaf(srcName,srcLen) || !safe_leaf(dstName,dstLen)) {
    napi_throw_type_error(env,NULL,"invalid_name: each leaf must be one safe Windows filename component"); goto cleanup;
  }
  if (!ntCreateFile || !ntSetInformationFile) {
    napi_throw_error(env,NULL,"required NT file APIs unavailable; failing closed"); goto cleanup;
  }
  if (!GetFileInformationByHandleEx(srcParent->handle,FileIdInfo,&srcVolume,sizeof(srcVolume)) ||
      !GetFileInformationByHandleEx(dstParent->handle,FileIdInfo,&dstVolume,sizeof(dstVolume)) ||
      !GetFileInformationByHandleEx(srcParent->handle,FileAttributeTagInfo,&tag,sizeof(tag)) ||
      (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
      !GetFileInformationByHandleEx(srcParent->handle,FileStandardInfo,&standard,sizeof(standard)) ||
      !standard.Directory) {
    napi_throw_error(env,NULL,"source parent is not a valid plain directory HANDLE"); goto cleanup;
  }
  if (!GetFileInformationByHandleEx(dstParent->handle,FileAttributeTagInfo,&tag,sizeof(tag)) ||
      (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
      !GetFileInformationByHandleEx(dstParent->handle,FileStandardInfo,&standard,sizeof(standard)) ||
      !standard.Directory) {
    napi_throw_error(env,NULL,"destination parent is not a valid plain directory HANDLE"); goto cleanup;
  }
  if (srcVolume.VolumeSerialNumber != dstVolume.VolumeSerialNumber) {
    napi_throw_error(env,NULL,"cross_volume: source and destination parents are on different volumes"); goto cleanup;
  }

  UNICODE_STRING sourceUnicode;
  sourceUnicode.Buffer=srcName;
  sourceUnicode.Length=(USHORT)(srcLen*sizeof(wchar_t));
  sourceUnicode.MaximumLength=sourceUnicode.Length;
  OBJECT_ATTRIBUTES oa;
  InitializeObjectAttributes(&oa,&sourceUnicode,OBJ_CASE_INSENSITIVE|OBJ_DONT_REPARSE,srcParent->handle,NULL);
  IO_STATUS_BLOCK iosb;
  NTSTATUS status=ntCreateFile(&source,DELETE|FILE_READ_ATTRIBUTES|SYNCHRONIZE,&oa,&iosb,NULL,
      FILE_ATTRIBUTE_NORMAL,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,FILE_OPEN,
      FILE_SYNCHRONOUS_IO_NONALERT|FILE_OPEN_REPARSE_POINT|FILE_NON_DIRECTORY_FILE,NULL,0);
  if (status < 0) {
    char msg[256]; _snprintf_s(msg,sizeof(msg),_TRUNCATE,"source_open_failed (NTSTATUS 0x%08lX)",(unsigned long)status);
    napi_throw_error(env,NULL,msg); goto cleanup;
  }
  if (!GetFileInformationByHandleEx(source,FileAttributeTagInfo,&tag,sizeof(tag)) ||
      (tag.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) ||
      !GetFileInformationByHandleEx(source,FileStandardInfo,&standard,sizeof(standard)) ||
      standard.Directory) {
    napi_throw_error(env,NULL,"source is not a plain regular file; reparse points and directories are rejected"); goto cleanup;
  }
  if (InterlockedCompareExchange(&g_fail_before_rename,0,1)==1) {
    napi_throw_error(env,NULL,"injected_pre_commit_failure"); goto cleanup;
  }

  size_t bytes=FIELD_OFFSET(RAYA_FILE_RENAME_INFORMATION,FileName)+(dstLen*sizeof(wchar_t));
  RAYA_FILE_RENAME_INFORMATION *renameInfo=(RAYA_FILE_RENAME_INFORMATION*)calloc(bytes,1);
  if (!renameInfo) { napi_throw_error(env,NULL,"out of memory building relative rename information"); goto cleanup; }
  renameInfo->ReplaceIfExists=FALSE;
  renameInfo->RootDirectory=dstParent->handle;
  renameInfo->FileNameLength=(ULONG)(dstLen*sizeof(wchar_t));
  memcpy(renameInfo->FileName,dstName,dstLen*sizeof(wchar_t));
  IO_STATUS_BLOCK renameIosb;
  NTSTATUS renameStatus=ntSetInformationFile(source,&renameIosb,renameInfo,(ULONG)bytes,(FILE_INFORMATION_CLASS)10);
  free(renameInfo);
  if (renameStatus < 0) {
    char msg[256];
    _snprintf_s(msg,sizeof(msg),_TRUNCATE,"relative rename failed without overwrite (NTSTATUS 0x%08lX)",(unsigned long)renameStatus);
    napi_throw_error(env,NULL,msg); goto cleanup;
  }

  if (!CloseHandle(source)) {
    source=INVALID_HANDLE_VALUE;
    napi_throw_error(env,NULL,"committed_but_cleanup_failed: rename committed but source HANDLE close failed");
    goto cleanup;
  }
  source=INVALID_HANDLE_VALUE;
  if (InterlockedCompareExchange(&g_fail_close_after_commit,0,1)==1) {
    napi_throw_error(env,NULL,"committed_but_cleanup_failed: injected close-status failure after real HANDLE close");
    goto cleanup;
  }
  if (napi_create_object(env,&result)!=napi_ok ||
      napi_get_boolean(env,TRUE,&value)!=napi_ok ||
      napi_set_named_property(env,result,"committed",value)!=napi_ok ||
      napi_get_boolean(env,FALSE,&value)!=napi_ok ||
      napi_set_named_property(env,result,"overwroteDestination",value)!=napi_ok) goto cleanup;
  free(src8);free(dst8);free(srcName);free(dstName);
  return result;

cleanup:
  if (source!=INVALID_HANDLE_VALUE) CloseHandle(source);
  free(src8);free(dst8);free(srcName);free(dstName);
  return NULL;
}

static napi_value init(napi_env env,napi_value exports) {
  napi_value fn;
  if (napi_create_function(env,"renameChildRelative",NAPI_AUTO_LENGTH,rename_child_relative,NULL,&fn)!=napi_ok ||
      napi_set_named_property(env,exports,"renameChildRelative",fn)!=napi_ok) return NULL;
  if (napi_create_function(env,"failNextBeforeRenameForTest",NAPI_AUTO_LENGTH,fail_before,NULL,&fn)!=napi_ok ||
      napi_set_named_property(env,exports,"failNextBeforeRenameForTest",fn)!=napi_ok) return NULL;
  if (napi_create_function(env,"failNextCloseAfterCommitForTest",NAPI_AUTO_LENGTH,fail_close_after,NULL,&fn)!=napi_ok ||
      napi_set_named_property(env,exports,"failNextCloseAfterCommitForTest",fn)!=napi_ok) return NULL;
  return exports;
}
NAPI_MODULE(NODE_GYP_MODULE_NAME,init)
