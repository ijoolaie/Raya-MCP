#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <winternl.h>
#include <node_api.h>
#include <stdint.h>
#include <stdbool.h>

static volatile LONG g_close_failures = 0;
static volatile LONG g_fail_next_close_after_real_close = 0;
static volatile LONG g_fail_next_directory_page = 0;
static volatile LONG g_fail_next_child_open_status = 0;

/* Consume ownership before attempting CloseHandle: never retry a possibly
   consumed/reused numeric HANDLE after a failed close. */
static BOOL close_handle_once(HANDLE *handle) {
  if (!handle || *handle == NULL || *handle == INVALID_HANDLE_VALUE) return TRUE;
  HANDLE value = *handle;
  *handle = INVALID_HANDLE_VALUE;
  if (!CloseHandle(value)) {
    InterlockedIncrement(&g_close_failures);
    return FALSE;
  }
  /* Test-only fault: model a close whose OS effect occurred but whose status
     was reported as failure. Never leave a real HANDLE leaked by injection. */
  if (InterlockedCompareExchange(&g_fail_next_close_after_real_close, 0, 1) == 1) {
    InterlockedIncrement(&g_close_failures);
    SetLastError(ERROR_GEN_FAILURE);
    return FALSE;
  }
  return TRUE;
}
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

/* Test-only stable classification layer; unknown NTSTATUS values stay generic. */
static const char *classify_ntstatus(ULONG code) {
  switch (code) {
    case 0xC0000034UL: /* STATUS_OBJECT_NAME_NOT_FOUND */
    case 0xC000003AUL: /* STATUS_OBJECT_PATH_NOT_FOUND */
    case 0xC000000FUL: /* STATUS_NO_SUCH_FILE */
      return "not_found";
    case 0xC0000022UL: /* STATUS_ACCESS_DENIED */
      return "access_denied";
    case 0xC0000035UL: /* STATUS_OBJECT_NAME_COLLISION */
      return "already_exists";
    case 0xC000000DUL: /* STATUS_INVALID_PARAMETER */
    case 0xC0000033UL: /* STATUS_OBJECT_NAME_INVALID */
    case 0xC000003BUL: /* STATUS_OBJECT_PATH_SYNTAX_BAD */
      return "invalid_name";
    case 0xC00000BAUL: /* STATUS_FILE_IS_A_DIRECTORY */
      return "is_directory";
    case 0xC0000103UL: /* STATUS_NOT_A_DIRECTORY */
      return "not_directory";
    case 0xC000050BUL: /* STATUS_REPARSE_POINT_ENCOUNTERED */
    case 0x8000002DUL: /* STATUS_STOPPED_ON_SYMLINK */
      return "reparse_blocked";
    case 0xC0000043UL: /* STATUS_SHARING_VIOLATION */
      return "sharing_violation";
    case 0xC000007FUL: /* STATUS_DISK_FULL */
      return "storage_full";
    case 0xC000009AUL: /* STATUS_INSUFFICIENT_RESOURCES */
      return "resource_exhausted";
    default:
      return "io_error";
  }
}


static napi_value fail_next_child_open_status_for_test(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1], result;
  uint32_t code = 0;
  if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc != 1 ||
      napi_get_value_uint32(env, argv[0], &code) != napi_ok || code == 0) {
    napi_throw_type_error(env, NULL, "failNextChildOpenStatusForTest(code) requires a nonzero uint32 NTSTATUS");
    return NULL;
  }
  if (InterlockedCompareExchange(&g_fail_next_child_open_status, (LONG)code, 0) != 0) {
    napi_throw_error(env, NULL, "a child-open NTSTATUS fault is already armed");
    return NULL;
  }
  napi_get_boolean(env, true, &result);
  return result;
}

static napi_value classify_ntstatus_for_test(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1], result, value;
  uint32_t code = 0;
  if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc != 1 ||
      napi_get_value_uint32(env, argv[0], &code) != napi_ok) {
    napi_throw_type_error(env, NULL, "classifyNtStatusForTest(code) requires one uint32 status code");
    return NULL;
  }
  if (napi_create_object(env, &result) != napi_ok ||
      napi_create_uint32(env, code, &value) != napi_ok ||
      napi_set_named_property(env, result, "code", value) != napi_ok ||
      napi_create_string_utf8(env, classify_ntstatus((ULONG)code), NAPI_AUTO_LENGTH, &value) != napi_ok ||
      napi_set_named_property(env, result, "category", value) != napi_ok) return NULL;
  return result;
}

static int utf8_to_wide(const char *src, wchar_t **out) {
  int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, NULL, 0);
  if (needed <= 0) return 0;
  wchar_t *buf = (wchar_t *)calloc((size_t)needed, sizeof(wchar_t));
  if (!buf) return 0;
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, buf, needed)) {
    free(buf); return 0;
  }
  *out = buf; return 1;
}

static int get_string(napi_env env, napi_value value, char **out) {
  size_t len = 0;
  if (napi_get_value_string_utf8(env, value, NULL, 0, &len) != napi_ok || len > 32760) return 0;
  char *buf = (char *)calloc(len + 1, 1);
  if (!buf) return 0;
  if (napi_get_value_string_utf8(env, value, buf, len + 1, &len) != napi_ok) {
    free(buf); return 0;
  }
  *out = buf; return 1;
}

static int is_reparse_or_bad(HANDLE h, FILE_ATTRIBUTE_TAG_INFO *tag) {
  if (!GetFileInformationByHandleEx(h, FileAttributeTagInfo, tag, sizeof(*tag))) return 1;
  return (tag->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

static int is_safe_leaf_name(const wchar_t *name, size_t nameLen);

/* Isolated prototype only: walk the configured root from the drive root by held handles. */
static HANDLE open_root_chain(napi_env env, const wchar_t *root, NtCreateFileFn ntCreateFile,
                              napi_value testCallback, char *error, size_t errorCap) {
  size_t rootLen = wcslen(root);
  if (rootLen < 3 || root[1] != L':' || root[2] != L'\\') {
    _snprintf_s(error, errorCap, _TRUNCATE, "root must be drive-absolute");
    return INVALID_HANDLE_VALUE;
  }
  for (size_t i = 0; i < rootLen; ++i) {
    if (root[i] == L'/' || (i > 1 && root[i] == L':')) {
      _snprintf_s(error, errorCap, _TRUNCATE, "invalid_name: root contains unsupported slash or colon syntax");
      return INVALID_HANDLE_VALUE;
    }
  }

  wchar_t driveRoot[] = L"C:\\";
  driveRoot[0] = root[0];
  HANDLE current = CreateFileW(driveRoot, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
  if (current == INVALID_HANDLE_VALUE) {
    _snprintf_s(error, errorCap, _TRUNCATE, "cannot open drive root (Win32 error %lu)", GetLastError());
    return INVALID_HANDLE_VALUE;
  }
  FILE_ATTRIBUTE_TAG_INFO tag;
  if (is_reparse_or_bad(current, &tag) || !(tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
    close_handle_once(&current);
    _snprintf_s(error, errorCap, _TRUNCATE, "drive root is not a plain directory");
    return INVALID_HANDLE_VALUE;
  }

  size_t pos = 3;
  while (pos < rootLen) {
    if (root[pos] == L'\\') {
      if (pos == rootLen - 1) break; /* permit one trailing separator */
      close_handle_once(&current);
      _snprintf_s(error, errorCap, _TRUNCATE, "repeated root separator");
      return INVALID_HANDLE_VALUE;
    }
    size_t end = pos;
    while (end < rootLen && root[end] != L'\\') end++;
    size_t componentLen = end - pos;
    wchar_t component[256];
    if (componentLen == 0 || componentLen > 255) {
      close_handle_once(&current);
      _snprintf_s(error, errorCap, _TRUNCATE, "invalid root path component");
      return INVALID_HANDLE_VALUE;
    }
    wmemcpy(component, root + pos, componentLen);
    component[componentLen] = L'\0';
    if (!is_safe_leaf_name(component, componentLen)) {
      close_handle_once(&current);
      _snprintf_s(error, errorCap, _TRUNCATE, "invalid_name: unsafe root path component");
      return INVALID_HANDLE_VALUE;
    }
    UNICODE_STRING name;
    name.Buffer = component;
    name.Length = (USHORT)(componentLen * sizeof(wchar_t));
    name.MaximumLength = name.Length;
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE, current, NULL);
    IO_STATUS_BLOCK iosb;
    HANDLE next = INVALID_HANDLE_VALUE;
    NTSTATUS status = ntCreateFile(&next,
        FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE, &oa, &iosb, NULL,
        FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | FILE_DIRECTORY_FILE,
        NULL, 0);
    if (status < 0) {
      close_handle_once(&current);
      _snprintf_s(error, errorCap, _TRUNCATE, "%s: root component open blocked (NTSTATUS 0x%08lX)", classify_ntstatus((ULONG)status), (unsigned long)status);
      return INVALID_HANDLE_VALUE;
    }
    if (is_reparse_or_bad(next, &tag) || !(tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
      close_handle_once(&next);
      close_handle_once(&current);
      _snprintf_s(error, errorCap, _TRUNCATE, "root component is not a plain directory");
      return INVALID_HANDLE_VALUE;
    }
    if (!close_handle_once(&current)) {
      (void)close_handle_once(&next);
      _snprintf_s(error, errorCap, _TRUNCATE, "close_failure: failed to release prior root-chain handle");
      return INVALID_HANDLE_VALUE;
    }
    current = next;
    next = INVALID_HANDLE_VALUE;
    if (testCallback) {
      napi_value global, callbackResult, componentValue;
      napi_value callbackArgs[1];
      if (napi_create_string_utf16(env, (const char16_t *)component, componentLen, &componentValue) != napi_ok ||
          napi_get_global(env, &global) != napi_ok) {
        close_handle_once(&current);
        _snprintf_s(error, errorCap, _TRUNCATE, "test-only root-chain callback setup failed");
        return INVALID_HANDLE_VALUE;
      }
      callbackArgs[0] = componentValue;
      if (napi_call_function(env, global, testCallback, 1, callbackArgs, &callbackResult) != napi_ok) {
        close_handle_once(&current);
        _snprintf_s(error, errorCap, _TRUNCATE, "test-only root-chain callback failed");
        return INVALID_HANDLE_VALUE;
      }
    }
    pos = end;
    if (pos < rootLen && root[pos] == L'\\') pos++;
    if (pos < rootLen && root[pos] == L'\\') {
      close_handle_once(&current);
      _snprintf_s(error, errorCap, _TRUNCATE, "repeated root separator");
      return INVALID_HANDLE_VALUE;
    }
  }
  return current;
}

static napi_value open_relative(napi_env env, napi_callback_info info) {
  size_t argc = 6;
  napi_value argv[6], result = NULL, value = NULL;
  if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc < 2 || argc > 6) {
    napi_throw_type_error(env, NULL, "openRelative(rootPath, relativePath[, testPauseMs[, maxReadBytes[, testOnParentHandleOpened[, testOnRootComponentOpened]]]]) accepts two to six arguments");
    return NULL;
  }

  char *root8 = NULL, *rel8 = NULL;
  wchar_t *root = NULL, *rel = NULL;
  HANDLE current = INVALID_HANDLE_VALUE, next = INVALID_HANDLE_VALUE;
  int failed = 0;
  NTSTATUS lastStatus = 0;
  char error[160];
  uint32_t testPauseMs = 0, maxReadBytes = 0;
  napi_value testCallback = NULL, rootCallback = NULL;
  if (argc >= 5) {
    napi_valuetype callbackType;
    if (napi_typeof(env, argv[4], &callbackType) != napi_ok || callbackType != napi_function) {
      napi_throw_type_error(env, NULL, "testOnParentHandleOpened must be a function");
      return NULL;
    }
    testCallback = argv[4];
  }
  if (argc >= 6) {
    napi_valuetype callbackType;
    if (napi_typeof(env, argv[5], &callbackType) != napi_ok || callbackType != napi_function) {
      napi_throw_type_error(env, NULL, "testOnRootComponentOpened must be a function");
      return NULL;
    }
    rootCallback = argv[5];
  }
  if (argc >= 3 && (napi_get_value_uint32(env, argv[2], &testPauseMs) != napi_ok || testPauseMs > 5000)) {
    napi_throw_type_error(env, NULL, "test-only pause must be an integer from 0 to 5000 ms");
    return NULL;
  }
  if (argc >= 4 && (napi_get_value_uint32(env, argv[3], &maxReadBytes) != napi_ok || maxReadBytes == 0 || maxReadBytes > 1048576)) {
    napi_throw_type_error(env, NULL, "maxReadBytes must be an integer from 1 to 1048576");
    return NULL;
  }

  if (!get_string(env, argv[0], &root8) || !get_string(env, argv[1], &rel8) ||
      !utf8_to_wide(root8, &root) || !utf8_to_wide(rel8, &rel)) {
    napi_throw_type_error(env, NULL, "invalid UTF-8 path input");
    goto cleanup;
  }

  size_t rootLen = wcslen(root), relLen = wcslen(rel);
  if (rootLen < 3 || root[1] != L':' || (root[2] != L'\\' && root[2] != L'/') ||
      (relLen == 0) || rel[0] == L'\\' || rel[0] == L'/' ||
      (relLen >= 2 && rel[1] == L':')) {
    napi_throw_type_error(env, NULL, "root must be a drive-absolute path and relativePath must be relative");
    goto cleanup;
  }
  for (size_t i = 0; i < relLen; ++i) {
    if (rel[i] == L':') {
      napi_throw_type_error(env, NULL, "colon is not allowed in relativePath");
      goto cleanup;
    }
  }

  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  NtCreateFileFn ntCreateFile = ntdll ? (NtCreateFileFn)GetProcAddress(ntdll, "NtCreateFile") : NULL;
  if (!ntCreateFile) {
    napi_throw_error(env, NULL, "NtCreateFile unavailable; failing closed");
    goto cleanup;
  }

  current = open_root_chain(env, root, ntCreateFile, rootCallback, error, sizeof(error));
  if (current == INVALID_HANDLE_VALUE) {
    napi_throw_error(env, NULL, error);
    goto cleanup;
  }
  FILE_ATTRIBUTE_TAG_INFO tag;

  wchar_t *cursor = rel;
  for (;;) {
    wchar_t *sep = wcspbrk(cursor, L"\\/");
    int isFinal = (sep == NULL);
    if (sep) *sep = L'\0';
    if (cursor[0] == L'\0' || wcscmp(cursor, L".") == 0 || wcscmp(cursor, L"..") == 0) {
      napi_throw_type_error(env, NULL, "empty, dot, and dot-dot path components are forbidden");
      goto cleanup;
    }
    size_t componentLen = wcslen(cursor);
    if (componentLen > 255) {
      napi_throw_type_error(env, NULL, "path component is too long");
      goto cleanup;
    }

    UNICODE_STRING name;
    name.Buffer = cursor;
    name.Length = (USHORT)(componentLen * sizeof(wchar_t));
    name.MaximumLength = name.Length;
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE, current, NULL);
    IO_STATUS_BLOCK iosb;
    ULONG options = FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT |
                    (isFinal ? FILE_NON_DIRECTORY_FILE : FILE_DIRECTORY_FILE);
    ACCESS_MASK access = FILE_READ_ATTRIBUTES | SYNCHRONIZE |
                         (isFinal ? (maxReadBytes > 0 ? FILE_READ_DATA : 0) : FILE_LIST_DIRECTORY);
    lastStatus = ntCreateFile(&next, access, &oa, &iosb, NULL, FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN, options, NULL, 0);
    if (lastStatus < 0) {
      _snprintf_s(error, sizeof(error), _TRUNCATE, "%s: relative component open blocked (NTSTATUS 0x%08lX)", classify_ntstatus((ULONG)lastStatus), (unsigned long)lastStatus);
      napi_throw_error(env, NULL, error); goto cleanup;
    }
    if (is_reparse_or_bad(next, &tag) || (isFinal ? ((tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                                                   : ((tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0))) {
      napi_throw_error(env, NULL, "opened component has a disallowed type or reparse attribute");
      goto cleanup;
    }
    if (!close_handle_once(&current)) {
      napi_throw_error(env, NULL, "failed to release prior directory-walk handle");
      goto cleanup;
    }
    current = next;
    next = INVALID_HANDLE_VALUE;
    if (isFinal) break;
    if (testCallback) {
      napi_value global, callbackResult;
      if (napi_get_global(env, &global) != napi_ok ||
          napi_call_function(env, global, testCallback, 0, NULL, &callbackResult) != napi_ok) {
        napi_throw_error(env, NULL, "test-only parent-handle synchronization callback failed");
        goto cleanup;
      }
    }
    if (testPauseMs > 0) Sleep(testPauseMs); /* test-only hook: external swapper acts only after callback */
    cursor = sep + 1;
    if (*cursor == L'\0') {
      napi_throw_type_error(env, NULL, "trailing separator is forbidden");
      goto cleanup;
    }
  }

  LARGE_INTEGER fileSize;
  if (!GetFileSizeEx(current, &fileSize)) {
    napi_throw_error(env, NULL, "could not query final handle size");
    goto cleanup;
  }
  if (maxReadBytes > 0) {
    if (fileSize.QuadPart < 0 || (uint64_t)fileSize.QuadPart > maxReadBytes) {
      napi_throw_range_error(env, NULL, "file exceeds the explicit read-size cap");
      goto cleanup;
    }
    size_t bytesToRead = (size_t)fileSize.QuadPart;
    char *data = (char *)calloc(bytesToRead ? bytesToRead : 1, 1);
    if (!data) {
      napi_throw_error(env, NULL, "out of memory while reading bounded file");
      goto cleanup;
    }
    DWORD bytesRead = 0;
    BOOL readOk = ReadFile(current, data, (DWORD)bytesToRead, &bytesRead, NULL);
    if (!readOk || bytesRead != bytesToRead) {
      free(data);
      napi_throw_error(env, NULL, "bounded handle read failed or file changed during read");
      goto cleanup;
    }
    if (napi_create_buffer_copy(env, bytesToRead, data, NULL, &value) != napi_ok) {
      free(data);
      goto cleanup;
    }
    free(data);
  }
  if (napi_create_object(env, &result) != napi_ok) goto cleanup;
  if (maxReadBytes > 0 && napi_set_named_property(env, result, "content", value) != napi_ok) goto cleanup;
  if (napi_get_boolean(env, TRUE, &value) != napi_ok ||
      napi_set_named_property(env, result, "opened", value) != napi_ok) goto cleanup;
  if (napi_get_boolean(env, (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, &value) != napi_ok ||
      napi_set_named_property(env, result, "isDirectory", value) != napi_ok) goto cleanup;
  if (napi_create_double(env, (double)fileSize.QuadPart, &value) != napi_ok ||
      napi_set_named_property(env, result, "size", value) != napi_ok) goto cleanup;

  close_handle_once(&current); current = INVALID_HANDLE_VALUE;
  free(root8); free(rel8); free(root); free(rel);
  return result;

cleanup:
  if (current != INVALID_HANDLE_VALUE) close_handle_once(&current);
  if (next != INVALID_HANDLE_VALUE) close_handle_once(&next);
  free(root8); free(rel8); free(root); free(rel);
  (void)failed;
  return NULL;
}


typedef struct ParentHandleRef {
  HANDLE handle;
  int closed;
} ParentHandleRef;

static void parent_handle_finalize(napi_env env, void *data, void *hint) {
  (void)env; (void)hint;
  ParentHandleRef *ref = (ParentHandleRef *)data;
  if (!ref) return;
  if (!ref->closed && ref->handle != INVALID_HANDLE_VALUE) {
    ref->closed = 1;
    (void)close_handle_once(&ref->handle);
  }
  free(ref);
}

/* Opens the configured root itself and returns an opaque HANDLE owner.
   This is test-only: JavaScript must select rootPath from its configured allowlist. */
static napi_value open_workspace_root(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1], result = NULL, value = NULL;
  char *root8 = NULL;
  wchar_t *root = NULL;
  HANDLE current = INVALID_HANDLE_VALUE;
  char error[192] = "workspace root open failed";
  ParentHandleRef *ref = NULL;
  if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc != 1) {
    napi_throw_type_error(env, NULL, "openWorkspaceRoot(rootPath) requires one argument");
    return NULL;
  }
  if (!get_string(env, argv[0], &root8) || !utf8_to_wide(root8, &root)) {
    napi_throw_type_error(env, NULL, "rootPath must be valid UTF-8");
    goto cleanup;
  }
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  NtCreateFileFn ntCreateFile = ntdll ? (NtCreateFileFn)GetProcAddress(ntdll, "NtCreateFile") : NULL;
  if (!ntCreateFile) {
    napi_throw_error(env, NULL, "NtCreateFile unavailable; failing closed");
    goto cleanup;
  }
  current = open_root_chain(env, root, ntCreateFile, NULL, error, sizeof(error));
  if (current == INVALID_HANDLE_VALUE) {
    napi_throw_error(env, NULL, error);
    goto cleanup;
  }
  ref = (ParentHandleRef *)calloc(1, sizeof(*ref));
  if (!ref) {
    napi_throw_error(env, NULL, "out of memory creating root handle owner");
    goto cleanup;
  }
  ref->handle = current;
  ref->closed = 0;
  current = INVALID_HANDLE_VALUE;
  if (napi_create_object(env, &result) != napi_ok ||
      napi_wrap(env, result, ref, parent_handle_finalize, NULL, NULL) != napi_ok) goto cleanup;
  ref = NULL;
  if (napi_get_boolean(env, TRUE, &value) != napi_ok ||
      napi_set_named_property(env, result, "opened", value) != napi_ok) return NULL;
  free(root8); free(root);
  return result;
cleanup:
  if (current != INVALID_HANDLE_VALUE) (void)close_handle_once(&current);
  if (ref) {
    if (!ref->closed && ref->handle != INVALID_HANDLE_VALUE) {
      ref->closed = 1;
      (void)close_handle_once(&ref->handle);
    }
    free(ref);
  }
  free(root8); free(root);
  return NULL;
}

/* Opens a directory relative to a held configured root and binds HANDLE ownership
   to the returned JS object with napi_wrap (not a writable/aliasable property). */
static napi_value open_parent_relative(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value argv[2], result = NULL, value = NULL;
  char *root8 = NULL, *rel8 = NULL;
  wchar_t *root = NULL, *rel = NULL;
  HANDLE current = INVALID_HANDLE_VALUE, next = INVALID_HANDLE_VALUE;
  char error[160] = "parent directory open failed";
  ParentHandleRef *ref = NULL;
  if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc != 2) {
    napi_throw_type_error(env, NULL, "openParentRelative(rootPath, relativeParentPath) requires two arguments");
    return NULL;
  }
  if (!get_string(env, argv[0], &root8) || !get_string(env, argv[1], &rel8) ||
      !utf8_to_wide(root8, &root) || !utf8_to_wide(rel8, &rel)) {
    napi_throw_type_error(env, NULL, "invalid UTF-8 path input");
    goto cleanup;
  }
  size_t relLen = wcslen(rel);
  if (relLen == 0 || rel[0] == L'\\' || rel[0] == L'/' ||
      (relLen >= 2 && rel[1] == L':')) {
    napi_throw_type_error(env, NULL, "relative parent path must be non-empty and relative");
    goto cleanup;
  }
  for (size_t i = 0; i < relLen; ++i) {
    if (rel[i] == L':') {
      napi_throw_type_error(env, NULL, "colon is not allowed in relative parent path");
      goto cleanup;
    }
  }
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  NtCreateFileFn ntCreateFile = ntdll ? (NtCreateFileFn)GetProcAddress(ntdll, "NtCreateFile") : NULL;
  if (!ntCreateFile) {
    napi_throw_error(env, NULL, "NtCreateFile unavailable; failing closed");
    goto cleanup;
  }
  current = open_root_chain(env, root, ntCreateFile, NULL, error, sizeof(error));
  if (current == INVALID_HANDLE_VALUE) {
    napi_throw_error(env, NULL, error);
    goto cleanup;
  }

  wchar_t *cursor = rel;
  for (;;) {
    wchar_t *sep = wcspbrk(cursor, L"\\/");
    int isFinal = (sep == NULL);
    if (sep) *sep = L'\0';
    size_t componentLen = wcslen(cursor);
    if (!is_safe_leaf_name(cursor, componentLen)) {
      napi_throw_type_error(env, NULL, "parent path component is not a safe Windows filename component");
      goto cleanup;
    }
    UNICODE_STRING name;
    name.Buffer = cursor;
    name.Length = (USHORT)(componentLen * sizeof(wchar_t));
    name.MaximumLength = name.Length;
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE, current, NULL);
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = ntCreateFile(&next,
        FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE, &oa, &iosb, NULL,
        FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | FILE_DIRECTORY_FILE,
        NULL, 0);
    if (status < 0) {
      _snprintf_s(error, sizeof(error), _TRUNCATE,
          "%s: relative parent component open blocked (NTSTATUS 0x%08lX)", classify_ntstatus((ULONG)status), (unsigned long)status);
      napi_throw_error(env, NULL, error);
      goto cleanup;
    }
    FILE_ATTRIBUTE_TAG_INFO tag;
    if (is_reparse_or_bad(next, &tag) || !(tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
      napi_throw_error(env, NULL, "parent component has a disallowed type or reparse attribute");
      goto cleanup;
    }
    if (!close_handle_once(&current)) {
      napi_throw_error(env, NULL, "failed to release prior directory-walk handle");
      goto cleanup;
    }
    current = next;
    next = INVALID_HANDLE_VALUE;
    if (isFinal) break;
    cursor = sep + 1;
    if (*cursor == L'\0') {
      napi_throw_type_error(env, NULL, "trailing separator is forbidden");
      goto cleanup;
    }
  }

  ref = (ParentHandleRef *)calloc(1, sizeof(*ref));
  if (!ref) {
    napi_throw_error(env, NULL, "out of memory creating parent handle owner");
    goto cleanup;
  }
  ref->handle = current;
  ref->closed = 0;
  current = INVALID_HANDLE_VALUE;
  if (napi_create_object(env, &result) != napi_ok ||
      napi_wrap(env, result, ref, parent_handle_finalize, NULL, NULL) != napi_ok) {
    goto cleanup;
  }
  ref = NULL; /* ownership is now tied to this object and its finalizer */
  if (napi_get_boolean(env, TRUE, &value) != napi_ok ||
      napi_set_named_property(env, result, "opened", value) != napi_ok) {
    return NULL; /* wrapped object finalizer retains and closes the HANDLE */
  }
  free(root8); free(rel8); free(root); free(rel);
  return result;

cleanup:
  if (current != INVALID_HANDLE_VALUE) close_handle_once(&current);
  if (next != INVALID_HANDLE_VALUE) close_handle_once(&next);
  if (ref) {
    if (!ref->closed && ref->handle != INVALID_HANDLE_VALUE) {
      ref->closed = 1;
      (void)close_handle_once(&ref->handle);
    }
    free(ref);
  }
  free(root8); free(rel8); free(root); free(rel);
  return NULL;
}

static ParentHandleRef *get_parent_ref(napi_env env, napi_value value) {
  void *data = NULL;
  if (napi_unwrap(env, value, &data) != napi_ok) return NULL;
  return (ParentHandleRef *)data;
}

static int is_reserved_device_basename(const wchar_t *name) {
  wchar_t base[256];
  size_t i = 0;
  while (name[i] != L'\0' && name[i] != L'.' && i < 255) {
    base[i] = name[i];
    ++i;
  }
  while (i > 0 && (base[i - 1] == L' ' || base[i - 1] == L'.')) --i;
  base[i] = L'\0';
  if (_wcsicmp(base, L"CON") == 0 || _wcsicmp(base, L"PRN") == 0 ||
      _wcsicmp(base, L"AUX") == 0 || _wcsicmp(base, L"NUL") == 0 ||
      _wcsicmp(base, L"CONIN$") == 0 || _wcsicmp(base, L"CONOUT$") == 0) return 1;
  if (i == 4 && (_wcsnicmp(base, L"COM", 3) == 0 || _wcsnicmp(base, L"LPT", 3) == 0)) {
    if (base[3] >= L'1' && base[3] <= L'9') return 1;
    if (base[3] == 0x00B9 || base[3] == 0x00B2 || base[3] == 0x00B3) return 1;
  }
  return 0;
}

static int is_safe_leaf_name(const wchar_t *name, size_t nameLen) {
  if (nameLen == 0 || nameLen > 255 || name[nameLen - 1] == L'.' ||
      name[nameLen - 1] == L' ' || is_reserved_device_basename(name)) return 0;
  for (size_t i = 0; i < nameLen; ++i) {
    if (name[i] < 32 || wcschr(L"\\/:|?*<>\"", name[i]) != NULL) return 0;
  }
  return wcscmp(name, L".") != 0 && wcscmp(name, L"..") != 0;
}

static napi_value inspect_parent_handle(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1], result, value;
  if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc != 1) {
    napi_throw_type_error(env, NULL, "inspectParentHandle(parent) requires one argument");
    return NULL;
  }
  ParentHandleRef *ref = get_parent_ref(env, argv[0]);
  if (!ref || ref->closed || ref->handle == INVALID_HANDLE_VALUE) {
    napi_throw_error(env, NULL, "parent handle is closed or invalid");
    return NULL;
  }
  FILE_ID_INFO id;
  FILE_ATTRIBUTE_TAG_INFO tag;
  FILE_STANDARD_INFO standard;
  FILE_BASIC_INFO basic;
  if (!GetFileInformationByHandleEx(ref->handle, FileIdInfo, &id, sizeof(id)) ||
      !GetFileInformationByHandleEx(ref->handle, FileAttributeTagInfo, &tag, sizeof(tag)) ||
      !GetFileInformationByHandleEx(ref->handle, FileStandardInfo, &standard, sizeof(standard)) ||
      !GetFileInformationByHandleEx(ref->handle, FileBasicInfo, &basic, sizeof(basic)) ||
      standard.EndOfFile.QuadPart < 0 || standard.AllocationSize.QuadPart < 0) {
    napi_throw_error(env, NULL, "could not inspect held parent handle metadata");
    return NULL;
  }
  char fileId[33], sizeText[32], allocationText[32];
  char creationText[32], accessText[32], writeText[32], changeText[32];
  for (int i = 0; i < 16; ++i) _snprintf_s(fileId + i * 2, 3, _TRUNCATE, "%02X", id.FileId.Identifier[i]);
  _i64toa_s(standard.EndOfFile.QuadPart, sizeText, sizeof(sizeText), 10);
  _i64toa_s(standard.AllocationSize.QuadPart, allocationText, sizeof(allocationText), 10);
  _i64toa_s(basic.CreationTime.QuadPart, creationText, sizeof(creationText), 10);
  _i64toa_s(basic.LastAccessTime.QuadPart, accessText, sizeof(accessText), 10);
  _i64toa_s(basic.LastWriteTime.QuadPart, writeText, sizeof(writeText), 10);
  _i64toa_s(basic.ChangeTime.QuadPart, changeText, sizeof(changeText), 10);
  if (napi_create_object(env, &result) != napi_ok ||
      napi_create_double(env, (double)id.VolumeSerialNumber, &value) != napi_ok ||
      napi_set_named_property(env, result, "volumeSerialNumber", value) != napi_ok ||
      napi_create_string_utf8(env, fileId, NAPI_AUTO_LENGTH, &value) != napi_ok ||
      napi_set_named_property(env, result, "fileId", value) != napi_ok ||
      napi_create_string_utf8(env, sizeText, NAPI_AUTO_LENGTH, &value) != napi_ok ||
      napi_set_named_property(env, result, "size", value) != napi_ok ||
      napi_create_string_utf8(env, allocationText, NAPI_AUTO_LENGTH, &value) != napi_ok ||
      napi_set_named_property(env, result, "allocationSize", value) != napi_ok ||
      napi_create_uint32(env, tag.FileAttributes, &value) != napi_ok ||
      napi_set_named_property(env, result, "attributes", value) != napi_ok ||
      napi_create_uint32(env, tag.ReparseTag, &value) != napi_ok ||
      napi_set_named_property(env, result, "reparseTag", value) != napi_ok ||
      napi_get_boolean(env, (tag.FileAttributes & FILE_ATTRIBUTE_READONLY) != 0, &value) != napi_ok ||
      napi_set_named_property(env, result, "readonlyAttribute", value) != napi_ok ||
      napi_get_boolean(env, (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, &value) != napi_ok ||
      napi_set_named_property(env, result, "isDirectory", value) != napi_ok ||
      napi_get_boolean(env, (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0, &value) != napi_ok ||
      napi_set_named_property(env, result, "isReparsePoint", value) != napi_ok ||
      napi_create_string_utf8(env, creationText, NAPI_AUTO_LENGTH, &value) != napi_ok ||
      napi_set_named_property(env, result, "creationTime100ns", value) != napi_ok ||
      napi_create_string_utf8(env, accessText, NAPI_AUTO_LENGTH, &value) != napi_ok ||
      napi_set_named_property(env, result, "lastAccessTime100ns", value) != napi_ok ||
      napi_create_string_utf8(env, writeText, NAPI_AUTO_LENGTH, &value) != napi_ok ||
      napi_set_named_property(env, result, "lastWriteTime100ns", value) != napi_ok ||
      napi_create_string_utf8(env, changeText, NAPI_AUTO_LENGTH, &value) != napi_ok ||
      napi_set_named_property(env, result, "changeTime100ns", value) != napi_ok) return NULL;
  return result;
}


static napi_value inspect_child_from_parent(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value argv[2], result, value;
  char *name8 = NULL;
  wchar_t *nameWide = NULL;
  HANDLE child = INVALID_HANDLE_VALUE;
  char error[192];
  if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc != 2) {
    napi_throw_type_error(env, NULL, "inspectChildFromParent(parent, childName) requires two arguments");
    return NULL;
  }
  ParentHandleRef *parent = get_parent_ref(env, argv[0]);
  if (!parent || parent->closed || parent->handle == INVALID_HANDLE_VALUE) {
    napi_throw_type_error(env, NULL, "valid open parent handle is required");
    return NULL;
  }
  if (!get_string(env, argv[1], &name8) || !utf8_to_wide(name8, &nameWide)) {
    napi_throw_type_error(env, NULL, "childName must be valid UTF-8");
    goto cleanup;
  }
  size_t nameLen = wcslen(nameWide);
  if (!is_safe_leaf_name(nameWide, nameLen)) {
    napi_throw_type_error(env, NULL, "invalid_name: childName must be one safe filename component");
    goto cleanup;
  }
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  NtCreateFileFn ntCreateFile = ntdll ? (NtCreateFileFn)GetProcAddress(ntdll, "NtCreateFile") : NULL;
  if (!ntCreateFile) {
    napi_throw_error(env, NULL, "NtCreateFile unavailable; failing closed");
    goto cleanup;
  }
  UNICODE_STRING name;
  name.Buffer = nameWide;
  name.Length = (USHORT)(nameLen * sizeof(wchar_t));
  name.MaximumLength = name.Length;
  OBJECT_ATTRIBUTES oa;
  InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE, parent->handle, NULL);
  IO_STATUS_BLOCK iosb;
  NTSTATUS status = ntCreateFile(&child,
      FILE_READ_ATTRIBUTES | SYNCHRONIZE, &oa, &iosb, NULL,
      FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | FILE_NON_DIRECTORY_FILE,
      NULL, 0);
  if (status < 0) {
    _snprintf_s(error, sizeof(error), _TRUNCATE,
        "%s: child identity open relative to held parent blocked (NTSTATUS 0x%08lX)", classify_ntstatus((ULONG)status), (unsigned long)status);
    napi_throw_error(env, NULL, error);
    goto cleanup;
  }
  FILE_ID_INFO id;
  FILE_ATTRIBUTE_TAG_INFO tag;
  LARGE_INTEGER fileSize;
  if (!GetFileInformationByHandleEx(child, FileIdInfo, &id, sizeof(id)) ||
      !GetFileInformationByHandleEx(child, FileAttributeTagInfo, &tag, sizeof(tag)) ||
      !GetFileSizeEx(child, &fileSize) || fileSize.QuadPart < 0 ||
      (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
      (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
    napi_throw_error(env, NULL, "child identity metadata unavailable or child is not a regular non-reparse file");
    goto cleanup;
  }
  char fileId[33];
  for (int i = 0; i < 16; ++i) _snprintf_s(fileId + i * 2, 3, _TRUNCATE, "%02X", id.FileId.Identifier[i]);
  if (napi_create_object(env, &result) != napi_ok ||
      napi_create_double(env, (double)id.VolumeSerialNumber, &value) != napi_ok ||
      napi_set_named_property(env, result, "volumeSerialNumber", value) != napi_ok ||
      napi_create_string_utf8(env, fileId, NAPI_AUTO_LENGTH, &value) != napi_ok ||
      napi_set_named_property(env, result, "fileId", value) != napi_ok ||
      napi_create_double(env, (double)fileSize.QuadPart, &value) != napi_ok ||
      napi_set_named_property(env, result, "size", value) != napi_ok ||
      napi_get_boolean(env, TRUE, &value) != napi_ok ||
      napi_set_named_property(env, result, "isRegularFile", value) != napi_ok ||
      napi_get_boolean(env, (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0, &value) != napi_ok ||
      napi_set_named_property(env, result, "isReparsePoint", value) != napi_ok) goto cleanup;
  if (!close_handle_once(&child)) {
    napi_throw_error(env, NULL, "close_failure: CloseHandle failed after child identity readback; ownership consumed");
    goto cleanup;
  }
  free(name8); free(nameWide);
  return result;

cleanup:
  if (child != INVALID_HANDLE_VALUE) (void)close_handle_once(&child);
  free(name8); free(nameWide);
  return NULL;
}

static napi_value close_parent_handle(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1], result;
  if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc != 1) {
    napi_throw_type_error(env, NULL, "closeParentHandle(parent) requires one argument");
    return NULL;
  }
  ParentHandleRef *ref = get_parent_ref(env, argv[0]);
  if (!ref) {
    napi_throw_type_error(env, NULL, "invalid parent handle object");
    return NULL;
  }
  if (!ref->closed && ref->handle != INVALID_HANDLE_VALUE) {
    ref->closed = 1;
    if (!close_handle_once(&ref->handle)) {
      napi_throw_error(env, NULL, "close_failure: CloseHandle failed for parent handle; ownership consumed and will not be retried");
      return NULL;
    }
  }
  if (napi_get_boolean(env, TRUE, &result) != napi_ok) return NULL;
  return result;
}



/* Candidate-only policy gate. This detects pre-existing multi-link regular files
   on the same HANDLE used by the operation. It is not an OS-level exclusion:
   another process with sufficient rights may add a link after this query. */
static BOOL enforce_single_link_regular_file(HANDLE file) {
  FILE_ATTRIBUTE_TAG_INFO tag;
  FILE_STANDARD_INFO standard;
  if (GetFileType(file) != FILE_TYPE_DISK ||
      !GetFileInformationByHandleEx(file, FileAttributeTagInfo, &tag, sizeof(tag)) ||
      !GetFileInformationByHandleEx(file, FileStandardInfo, &standard, sizeof(standard))) {
    return FALSE;
  }
  if ((tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
      (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return TRUE;
  return standard.NumberOfLinks == 1;
}

/* Re-open each enumerated regular file relative to the held directory and
   compare its file index with the enumeration record before accepting it. */
static BOOL validate_enumerated_regular_file(HANDLE directory, const WCHAR *leaf,
                                              USHORT leafBytes, ULONGLONG expectedFileId) {
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  NtCreateFileFn ntCreateFile = ntdll ? (NtCreateFileFn)GetProcAddress(ntdll, "NtCreateFile") : NULL;
  if (!ntCreateFile || !leaf || leafBytes == 0 || (leafBytes % sizeof(WCHAR)) != 0) return FALSE;
  UNICODE_STRING name;
  name.Buffer = (PWSTR)leaf;
  name.Length = leafBytes;
  name.MaximumLength = leafBytes;
  OBJECT_ATTRIBUTES oa;
  InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE, directory, NULL);
  IO_STATUS_BLOCK iosb;
  HANDLE child = INVALID_HANDLE_VALUE;
  NTSTATUS status = ntCreateFile(&child, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &oa, &iosb,
      NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | FILE_NON_DIRECTORY_FILE,
      NULL, 0);
  if (status < 0) return FALSE;
  BOOL ok = FALSE;
  FILE_ATTRIBUTE_TAG_INFO tag;
  FILE_STANDARD_INFO standard;
  BY_HANDLE_FILE_INFORMATION legacy;
  if (GetFileType(child) == FILE_TYPE_DISK &&
      GetFileInformationByHandleEx(child, FileAttributeTagInfo, &tag, sizeof(tag)) &&
      GetFileInformationByHandleEx(child, FileStandardInfo, &standard, sizeof(standard)) &&
      GetFileInformationByHandle(child, &legacy)) {
    ULONGLONG actualFileId = ((ULONGLONG)legacy.nFileIndexHigh << 32) | legacy.nFileIndexLow;
    ok = !(tag.FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
         standard.NumberOfLinks == 1 && actualFileId == expectedFileId;
  }
  if (!close_handle_once(&child)) return FALSE;
  return ok;
}

static napi_value read_child_from_parent(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value argv[3], result, value, contentValue;
  char *name8 = NULL;
  wchar_t *nameWide = NULL;
  HANDLE child = INVALID_HANDLE_VALUE;
  char error[160];
  uint32_t maxReadBytes = 0;
  if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc != 3) {
    napi_throw_type_error(env, NULL, "readChildFromParent(parent, childName, maxReadBytes) requires three arguments");
    return NULL;
  }
  ParentHandleRef *parent = get_parent_ref(env, argv[0]);
  if (!parent || parent->closed || parent->handle == INVALID_HANDLE_VALUE) {
    napi_throw_error(env, NULL, "parent handle is closed or invalid");
    return NULL;
  }
  if (!get_string(env, argv[1], &name8) || !utf8_to_wide(name8, &nameWide) ||
      napi_get_value_uint32(env, argv[2], &maxReadBytes) != napi_ok ||
      maxReadBytes == 0 || maxReadBytes > 1048576) {
    napi_throw_type_error(env, NULL, "childName must be valid UTF-8 and maxReadBytes must be 1..1048576");
    goto cleanup;
  }
  size_t nameLen = wcslen(nameWide);
  if (!is_safe_leaf_name(nameWide, nameLen)) {
    napi_throw_type_error(env, NULL, "invalid_name: childName must be one safe filename component");
    goto cleanup;
  }
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  NtCreateFileFn ntCreateFile = ntdll ? (NtCreateFileFn)GetProcAddress(ntdll, "NtCreateFile") : NULL;
  if (!ntCreateFile) {
    napi_throw_error(env, NULL, "NtCreateFile unavailable; failing closed");
    goto cleanup;
  }
  UNICODE_STRING name;
  name.Buffer = nameWide;
  name.Length = (USHORT)(nameLen * sizeof(wchar_t));
  name.MaximumLength = name.Length;
  OBJECT_ATTRIBUTES oa;
  InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE, parent->handle, NULL);
  IO_STATUS_BLOCK iosb;
  LONG injectedStatus = InterlockedExchange(&g_fail_next_child_open_status, 0);
  NTSTATUS status = injectedStatus
      ? (NTSTATUS)injectedStatus
      : ntCreateFile(&child, FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
          &oa, &iosb, NULL, FILE_ATTRIBUTE_NORMAL,
          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
          FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | FILE_NON_DIRECTORY_FILE,
          NULL, 0);
  if (status < 0) {
    _snprintf_s(error, sizeof(error), _TRUNCATE,
        "%s: child open relative to held parent blocked (NTSTATUS 0x%08lX)", classify_ntstatus((ULONG)status), (unsigned long)status);
    napi_throw_error(env, NULL, error);
    goto cleanup;
  }
  FILE_ATTRIBUTE_TAG_INFO tag;
  LARGE_INTEGER fileSize;
  if (is_reparse_or_bad(child, &tag) ||
      (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
      !GetFileSizeEx(child, &fileSize) || fileSize.QuadPart < 0 ||
      (uint64_t)fileSize.QuadPart > maxReadBytes) {
    napi_throw_range_error(env, NULL, "child is reparse/directory, unreadable, or exceeds the read cap");
    goto cleanup;
  }
  if (!enforce_single_link_regular_file(child)) {
    napi_throw_error(env, NULL, "hardlink_policy: regular file is multi-linked or link metadata is unavailable");
    goto cleanup;
  }
  size_t bytesToRead = (size_t)fileSize.QuadPart;
  char *data = (char *)calloc(bytesToRead ? bytesToRead : 1, 1);
  if (!data) {
    napi_throw_error(env, NULL, "out of memory during bounded child read");
    goto cleanup;
  }
  DWORD bytesRead = 0;
  BOOL readOk = ReadFile(child, data, (DWORD)bytesToRead, &bytesRead, NULL);
  if (!readOk || bytesRead != bytesToRead) {
    free(data);
    napi_throw_error(env, NULL, "bounded child read failed or file changed during read");
    goto cleanup;
  }
  if (napi_create_buffer_copy(env, bytesToRead, data, NULL, &contentValue) != napi_ok) {
    free(data);
    goto cleanup;
  }
  free(data);
  if (napi_create_object(env, &result) != napi_ok ||
      napi_set_named_property(env, result, "content", contentValue) != napi_ok ||
      napi_create_double(env, (double)fileSize.QuadPart, &value) != napi_ok ||
      napi_set_named_property(env, result, "size", value) != napi_ok) goto cleanup;
  if (!close_handle_once(&child)) {
    napi_throw_error(env, NULL, "close_failure: CloseHandle failed after bounded child read; ownership consumed and will not be retried");
    goto cleanup;
  }
  free(name8); free(nameWide);
  return result;

cleanup:
  if (child != INVALID_HANDLE_VALUE) close_handle_once(&child);
  free(name8); free(nameWide);
  return NULL;
}

static napi_value read_tail_child_from_parent(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value argv[3], result, value, contentValue;
  char *name8 = NULL;
  wchar_t *nameWide = NULL;
  HANDLE child = INVALID_HANDLE_VALUE;
  char error[160];
  uint32_t tailBytes = 0;
  if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc != 3) {
    napi_throw_type_error(env, NULL, "readTailChildFromParent(parent, childName, tailBytes) requires three arguments");
    return NULL;
  }
  ParentHandleRef *parent = get_parent_ref(env, argv[0]);
  if (!parent || parent->closed || parent->handle == INVALID_HANDLE_VALUE) {
    napi_throw_error(env, NULL, "parent handle is closed or invalid");
    return NULL;
  }
  if (!get_string(env, argv[1], &name8) || !utf8_to_wide(name8, &nameWide) ||
      napi_get_value_uint32(env, argv[2], &tailBytes) != napi_ok ||
      tailBytes == 0 || tailBytes > 1048576) {
    napi_throw_type_error(env, NULL, "childName must be valid UTF-8 and tailBytes must be 1..1048576");
    goto cleanup;
  }
  size_t nameLen = wcslen(nameWide);
  if (!is_safe_leaf_name(nameWide, nameLen)) {
    napi_throw_type_error(env, NULL, "invalid_name: childName must be one safe filename component");
    goto cleanup;
  }
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  NtCreateFileFn ntCreateFile = ntdll ? (NtCreateFileFn)GetProcAddress(ntdll, "NtCreateFile") : NULL;
  if (!ntCreateFile) {
    napi_throw_error(env, NULL, "NtCreateFile unavailable; failing closed");
    goto cleanup;
  }
  UNICODE_STRING name;
  name.Buffer = nameWide;
  name.Length = (USHORT)(nameLen * sizeof(wchar_t));
  name.MaximumLength = name.Length;
  OBJECT_ATTRIBUTES oa;
  InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE, parent->handle, NULL);
  IO_STATUS_BLOCK iosb;
  NTSTATUS status = ntCreateFile(&child, FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
      &oa, &iosb, NULL, FILE_ATTRIBUTE_NORMAL,
      FILE_SHARE_READ, FILE_OPEN,
      FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | FILE_NON_DIRECTORY_FILE,
      NULL, 0);
  if (status < 0) {
    _snprintf_s(error, sizeof(error), _TRUNCATE,
        "%s: child open relative to held parent blocked (NTSTATUS 0x%08lX)", classify_ntstatus((ULONG)status), (unsigned long)status);
    napi_throw_error(env, NULL, error);
    goto cleanup;
  }
  FILE_ATTRIBUTE_TAG_INFO tag;
  LARGE_INTEGER fileSize;
  if (is_reparse_or_bad(child, &tag) ||
      (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
      !GetFileSizeEx(child, &fileSize) || fileSize.QuadPart < 0) {
    napi_throw_range_error(env, NULL, "child is reparse/directory, unreadable, or exceeds the read cap");
    goto cleanup;
  }
  if (!enforce_single_link_regular_file(child)) {
    napi_throw_error(env, NULL, "hardlink_policy: regular file is multi-linked or link metadata is unavailable");
    goto cleanup;
  }
  size_t bytesToRead = (size_t)fileSize.QuadPart;
  if (bytesToRead > tailBytes) bytesToRead = tailBytes;
  LARGE_INTEGER offset;
  offset.QuadPart = fileSize.QuadPart - (LONGLONG)bytesToRead;
  if (!SetFilePointerEx(child, offset, NULL, FILE_BEGIN)) {
    napi_throw_error(env, NULL, "could not seek to bounded child tail");
    goto cleanup;
  }
  char *data = (char *)calloc(bytesToRead ? bytesToRead : 1, 1);
  if (!data) {
    napi_throw_error(env, NULL, "out of memory during bounded child tail read");
    goto cleanup;
  }
  DWORD bytesRead = 0;
  BOOL readOk = ReadFile(child, data, (DWORD)bytesToRead, &bytesRead, NULL);
  LARGE_INTEGER finalSize;
  BOOL sizeStable = GetFileSizeEx(child, &finalSize) && finalSize.QuadPart == fileSize.QuadPart;
  if (!readOk || bytesRead != bytesToRead || !sizeStable) {
    free(data);
    napi_throw_error(env, NULL, "bounded child tail read failed or file changed during read");
    goto cleanup;
  }
  if (!close_handle_once(&child)) {
    napi_throw_error(env, NULL, "close_failure: CloseHandle failed after bounded child tail read; ownership consumed and will not be retried");
    goto cleanup;
  }
  if (napi_create_buffer_copy(env, bytesToRead, data, NULL, &contentValue) != napi_ok) {
    free(data);
    goto cleanup;
  }
  free(data);
  if (napi_create_object(env, &result) != napi_ok ||
      napi_set_named_property(env, result, "content", contentValue) != napi_ok ||
      napi_create_double(env, (double)fileSize.QuadPart, &value) != napi_ok ||
      napi_set_named_property(env, result, "size", value) != napi_ok) goto cleanup;
  if (napi_create_double(env, (double)(fileSize.QuadPart - (LONGLONG)bytesToRead), &value) != napi_ok ||
      napi_set_named_property(env, result, "offset", value) != napi_ok ||
      napi_create_double(env, (double)bytesToRead, &value) != napi_ok ||
      napi_set_named_property(env, result, "bytesRead", value) != napi_ok) goto cleanup;
  free(name8); free(nameWide);
  return result;

cleanup:
  if (child != INVALID_HANDLE_VALUE) close_handle_once(&child);
  free(name8); free(nameWide);
  return NULL;
}

static napi_value close_failure_count(napi_env env, napi_callback_info info) {
  (void)info;
  napi_value result;
  if (napi_create_uint32(env, (uint32_t)InterlockedCompareExchange(&g_close_failures, 0, 0), &result) != napi_ok) return NULL;
  return result;
}

/* Isolated native-probe harness hook; not compiled into production handlers. */
static napi_value fail_next_close_for_test(napi_env env, napi_callback_info info) {
  (void)info;
  InterlockedExchange(&g_fail_next_close_after_real_close, 1);
  napi_value result;
  if (napi_get_boolean(env, TRUE, &result) != napi_ok) return NULL;
  return result;
}

/* Isolated native-probe fault injection; never compiled into production handlers. */
static napi_value fail_next_directory_page_for_test(napi_env env, napi_callback_info info) {
  (void)info;
  InterlockedExchange(&g_fail_next_directory_page, 1);
  napi_value result;
  if (napi_get_boolean(env, TRUE, &result) != napi_ok) return NULL;
  return result;
}

static napi_value create_child_exclusive(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value argv[2], result = NULL, value;
  ParentHandleRef *parent = NULL;
  char *name8 = NULL;
  wchar_t *nameWide = NULL;
  HANDLE child = INVALID_HANDLE_VALUE;
  char error[256] = {0};
  if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc != 2) {
    napi_throw_type_error(env, NULL, "createChildExclusive(parent, childName) requires two arguments");
    return NULL;
  }
  parent = get_parent_ref(env, argv[0]);
  if (!parent || parent->closed || parent->handle == INVALID_HANDLE_VALUE) {
    napi_throw_type_error(env, NULL, "valid open parent handle is required");
    return NULL;
  }
  if (!get_string(env, argv[1], &name8) || !utf8_to_wide(name8, &nameWide)) {
    napi_throw_type_error(env, NULL, "childName must be valid UTF-8");
    goto cleanup;
  }
  size_t nameLen = wcslen(nameWide);
  if (!is_safe_leaf_name(nameWide, nameLen)) {
    napi_throw_type_error(env, NULL, "invalid_name: childName must be one safe filename component");
    goto cleanup;
  }
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  NtCreateFileFn ntCreateFile = ntdll ? (NtCreateFileFn)GetProcAddress(ntdll, "NtCreateFile") : NULL;
  if (!ntCreateFile) {
    napi_throw_error(env, NULL, "NtCreateFile unavailable; failing closed");
    goto cleanup;
  }
  UNICODE_STRING name;
  name.Buffer = nameWide;
  name.Length = (USHORT)(nameLen * sizeof(wchar_t));
  name.MaximumLength = name.Length;
  OBJECT_ATTRIBUTES oa;
  InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE, parent->handle, NULL);
  IO_STATUS_BLOCK iosb;
  NTSTATUS status = ntCreateFile(&child,
      FILE_WRITE_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE, &oa, &iosb, NULL,
      FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      FILE_CREATE, FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | FILE_NON_DIRECTORY_FILE,
      NULL, 0);
  if (status < 0) {
    _snprintf_s(error, sizeof(error), _TRUNCATE,
        "%s: exclusive child creation blocked (NTSTATUS 0x%08lX)", classify_ntstatus((ULONG)status), (unsigned long)status);
    napi_throw_error(env, NULL, error);
    goto cleanup;
  }
  FILE_ATTRIBUTE_TAG_INFO tag;
  if (is_reparse_or_bad(child, &tag) || (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
    napi_throw_error(env, NULL, "created child has a disallowed type or reparse attribute");
    goto cleanup;
  }
  if (!close_handle_once(&child)) {
    napi_throw_error(env, NULL, "close_failure: CloseHandle failed after exclusive child creation; ownership consumed");
    goto cleanup;
  }
  if (napi_create_object(env, &result) != napi_ok ||
      napi_get_boolean(env, TRUE, &value) != napi_ok ||
      napi_set_named_property(env, result, "created", value) != napi_ok ||
      napi_create_uint32(env, 0, &value) != napi_ok ||
      napi_set_named_property(env, result, "size", value) != napi_ok) goto cleanup;
  free(name8); free(nameWide);
  return result;

cleanup:
  if (child != INVALID_HANDLE_VALUE) (void)close_handle_once(&child);
  free(name8); free(nameWide);
  return NULL;
}

static napi_value create_directory_child_exclusive(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value argv[2], result = NULL, value;
  ParentHandleRef *parent = NULL;
  char *name8 = NULL;
  wchar_t *nameWide = NULL;
  HANDLE child = INVALID_HANDLE_VALUE;
  char error[256] = {0};
  if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc != 2) {
    napi_throw_type_error(env, NULL, "createDirectoryChildExclusive(parent, childName) requires two arguments");
    return NULL;
  }
  parent = get_parent_ref(env, argv[0]);
  if (!parent || parent->closed || parent->handle == INVALID_HANDLE_VALUE) {
    napi_throw_type_error(env, NULL, "valid open parent handle is required");
    return NULL;
  }
  if (!get_string(env, argv[1], &name8) || !utf8_to_wide(name8, &nameWide)) {
    napi_throw_type_error(env, NULL, "childName must be valid UTF-8");
    goto cleanup;
  }
  size_t nameLen = wcslen(nameWide);
  if (!is_safe_leaf_name(nameWide, nameLen)) {
    napi_throw_type_error(env, NULL, "invalid_name: childName must be one safe filename component");
    goto cleanup;
  }
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  NtCreateFileFn ntCreateFile = ntdll ? (NtCreateFileFn)GetProcAddress(ntdll, "NtCreateFile") : NULL;
  if (!ntCreateFile) {
    napi_throw_error(env, NULL, "NtCreateFile unavailable; failing closed");
    goto cleanup;
  }
  UNICODE_STRING name;
  name.Buffer = nameWide;
  name.Length = (USHORT)(nameLen * sizeof(wchar_t));
  name.MaximumLength = name.Length;
  OBJECT_ATTRIBUTES oa;
  InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE, parent->handle, NULL);
  IO_STATUS_BLOCK iosb;
  NTSTATUS status = ntCreateFile(&child,
      FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE, &oa, &iosb, NULL,
      FILE_ATTRIBUTE_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      FILE_CREATE, FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | FILE_DIRECTORY_FILE,
      NULL, 0);
  if (status < 0) {
    _snprintf_s(error, sizeof(error), _TRUNCATE,
        "%s: exclusive child directory creation blocked (NTSTATUS 0x%08lX)", classify_ntstatus((ULONG)status), (unsigned long)status);
    napi_throw_error(env, NULL, error);
    goto cleanup;
  }
  FILE_ATTRIBUTE_TAG_INFO tag;
  if (is_reparse_or_bad(child, &tag) || !(tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
    napi_throw_error(env, NULL, "created child directory has a disallowed type or reparse attribute");
    goto cleanup;
  }
  if (!close_handle_once(&child)) {
    napi_throw_error(env, NULL, "close_failure: CloseHandle failed after exclusive child directory creation; ownership consumed");
    goto cleanup;
  }
  if (napi_create_object(env, &result) != napi_ok ||
      napi_get_boolean(env, TRUE, &value) != napi_ok ||
      napi_set_named_property(env, result, "created", value) != napi_ok ||
      napi_get_boolean(env, TRUE, &value) != napi_ok ||
      napi_set_named_property(env, result, "isDirectory", value) != napi_ok) goto cleanup;
  free(name8); free(nameWide);
  return result;

cleanup:
  if (child != INVALID_HANDLE_VALUE) (void)close_handle_once(&child);
  free(name8); free(nameWide);
  return NULL;
}

static napi_value process_handle_count(napi_env env, napi_callback_info info) {
  DWORD count = 0;
  (void)info;
  if (!GetProcessHandleCount(GetCurrentProcess(), &count)) {
    napi_throw_error(env, NULL, "GetProcessHandleCount failed");
    return NULL;
  }
  napi_value result;
  if (napi_create_uint32(env, count, &result) != napi_ok) return NULL;
  return result;
}



/* Opens one directory component relative to an already-held directory handle.
   The returned object owns the new HANDLE using the same external finalizer. */
static napi_value open_directory_child(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value argv[2], result = NULL, value;
  ParentHandleRef *parent = NULL, *ref = NULL;
  char *name8 = NULL;
  wchar_t *nameWide = NULL;
  HANDLE child = INVALID_HANDLE_VALUE;
  char error[192];
  if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc != 2) {
    napi_throw_type_error(env, NULL, "openDirectoryChild(parent, childName) requires two arguments");
    return NULL;
  }
  parent = get_parent_ref(env, argv[0]);
  if (!parent || parent->closed || parent->handle == INVALID_HANDLE_VALUE) {
    napi_throw_error(env, NULL, "parent handle is closed or invalid");
    return NULL;
  }
  if (!get_string(env, argv[1], &name8) || !utf8_to_wide(name8, &nameWide)) {
    napi_throw_type_error(env, NULL, "childName must be valid UTF-8");
    goto cleanup;
  }
  size_t nameLen = wcslen(nameWide);
  if (!is_safe_leaf_name(nameWide, nameLen)) {
    napi_throw_type_error(env, NULL, "invalid_name: childName must be one safe filename component");
    goto cleanup;
  }
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  NtCreateFileFn ntCreateFile = ntdll ? (NtCreateFileFn)GetProcAddress(ntdll, "NtCreateFile") : NULL;
  if (!ntCreateFile) {
    napi_throw_error(env, NULL, "NtCreateFile unavailable; failing closed");
    goto cleanup;
  }
  UNICODE_STRING name;
  name.Buffer = nameWide;
  name.Length = (USHORT)(nameLen * sizeof(wchar_t));
  name.MaximumLength = name.Length;
  OBJECT_ATTRIBUTES oa;
  InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE, parent->handle, NULL);
  IO_STATUS_BLOCK iosb;
  NTSTATUS status = ntCreateFile(&child,
      FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE, &oa, &iosb, NULL,
      FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | FILE_DIRECTORY_FILE,
      NULL, 0);
  if (status < 0) {
    _snprintf_s(error, sizeof(error), _TRUNCATE,
        "%s: relative child directory open blocked (NTSTATUS 0x%08lX)", classify_ntstatus((ULONG)status), (unsigned long)status);
    napi_throw_error(env, NULL, error);
    goto cleanup;
  }
  FILE_ATTRIBUTE_TAG_INFO tag;
  if (is_reparse_or_bad(child, &tag) || !(tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
    napi_throw_error(env, NULL, "opened child directory is a reparse point or unexpected type");
    goto cleanup;
  }
  ref = (ParentHandleRef *)calloc(1, sizeof(*ref));
  if (!ref) {
    napi_throw_error(env, NULL, "out of memory creating child directory handle owner");
    goto cleanup;
  }
  ref->handle = child;
  ref->closed = 0;
  child = INVALID_HANDLE_VALUE;
  if (napi_create_object(env, &result) != napi_ok ||
      napi_wrap(env, result, ref, parent_handle_finalize, NULL, NULL) != napi_ok) goto cleanup;
  ref = NULL;
  if (napi_get_boolean(env, TRUE, &value) != napi_ok ||
      napi_set_named_property(env, result, "opened", value) != napi_ok) return NULL;
  free(name8); free(nameWide);
  return result;
cleanup:
  if (child != INVALID_HANDLE_VALUE) (void)close_handle_once(&child);
  if (ref) {
    if (!ref->closed && ref->handle != INVALID_HANDLE_VALUE) {
      ref->closed = 1;
      (void)close_handle_once(&ref->handle);
    }
    free(ref);
  }
  free(name8); free(nameWide);
  return NULL;
}

/* One bounded directory page from the held directory HANDLE. It never follows
   entries. A buffer that cannot hold a complete OS page fails closed. */
static napi_value enumerate_directory_page(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value argv[3], result = NULL, entries = NULL, item, value;
  uint32_t maxEntries = 0;
  bool restart = false;
  ParentHandleRef *dir = NULL;
  BYTE *buffer = NULL;
  const DWORD bufferSize = 65536;
  if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc < 2 || argc > 3) {
    napi_throw_type_error(env, NULL, "enumerateDirectoryPage(directoryHandle, maxEntries, restart?) requires two or three arguments");
    return NULL;
  }
  dir = get_parent_ref(env, argv[0]);
  if (!dir || dir->closed || dir->handle == INVALID_HANDLE_VALUE) {
    napi_throw_error(env, NULL, "directory handle is closed or invalid");
    return NULL;
  }
  if (napi_get_value_uint32(env, argv[1], &maxEntries) != napi_ok || maxEntries == 0 || maxEntries > 1024) {
    napi_throw_range_error(env, NULL, "maxEntries must be 1..1024");
    return NULL;
  }
  if (argc == 3 && napi_get_value_bool(env, argv[2], &restart) != napi_ok) {
    napi_throw_type_error(env, NULL, "restart must be a boolean");
    return NULL;
  }
  FILE_ATTRIBUTE_TAG_INFO dirTag;
  if (is_reparse_or_bad(dir->handle, &dirTag) || !(dirTag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
    napi_throw_error(env, NULL, "enumeration handle is not a plain directory");
    return NULL;
  }
  buffer = (BYTE *)calloc(bufferSize, 1);
  if (!buffer) {
    napi_throw_error(env, NULL, "out of memory allocating bounded directory page");
    return NULL;
  }
  FILE_INFO_BY_HANDLE_CLASS infoClass = restart ? FileIdBothDirectoryRestartInfo : FileIdBothDirectoryInfo;
  if (InterlockedCompareExchange(&g_fail_next_directory_page, 0, 1) == 1) {
    free(buffer);
    napi_throw_error(env, NULL, "injected directory enumeration page failure");
    return NULL;
  }
  if (!GetFileInformationByHandleEx(dir->handle, infoClass, buffer, bufferSize)) {
    DWORD e = GetLastError();
    if (e == ERROR_NO_MORE_FILES) {
      if (napi_create_array(env, &entries) != napi_ok ||
          napi_create_object(env, &result) != napi_ok ||
          napi_set_named_property(env, result, "entries", entries) != napi_ok ||
          napi_get_boolean(env, FALSE, &value) != napi_ok ||
          napi_set_named_property(env, result, "pageRead", value) != napi_ok ||
          napi_get_boolean(env, TRUE, &value) != napi_ok ||
          napi_set_named_property(env, result, "done", value) != napi_ok) {
        free(buffer);
        return NULL;
      }
      free(buffer);
      return result;
    }
    free(buffer);
    char message[128];
    _snprintf_s(message, sizeof(message), _TRUNCATE, "bounded directory enumeration failed (Win32 error %lu)", e);
    napi_throw_error(env, NULL, message);
    return NULL;
  }
  if (napi_create_array(env, &entries) != napi_ok) goto cleanup;
  size_t count = 0;
  int pageOverflow = 0;
  DWORD offset = 0;
  for (;;) {
    if (offset > bufferSize - sizeof(FILE_ID_BOTH_DIR_INFO)) {
      napi_throw_error(env, NULL, "directory enumeration returned an invalid record offset");
      goto cleanup;
    }
    FILE_ID_BOTH_DIR_INFO *entry = (FILE_ID_BOTH_DIR_INFO *)(buffer + offset);
    if (entry->FileNameLength % sizeof(WCHAR) != 0 ||
        entry->FileNameLength > bufferSize - offset - FIELD_OFFSET(FILE_ID_BOTH_DIR_INFO, FileName)) {
      napi_throw_error(env, NULL, "directory enumeration returned an invalid filename length");
      goto cleanup;
    }
    size_t chars = entry->FileNameLength / sizeof(WCHAR);
    if (!((chars == 1 && entry->FileName[0] == L'.') ||
          (chars == 2 && entry->FileName[0] == L'.' && entry->FileName[1] == L'.'))) {
      if (count >= maxEntries) {
        pageOverflow = 1;
      } else if (!(entry->FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
          !validate_enumerated_regular_file(dir->handle, entry->FileName,
              (USHORT)entry->FileNameLength, (ULONGLONG)entry->FileId.QuadPart)) {
        napi_throw_error(env, NULL, "hardlink_policy: enumerated regular file failed single-link or identity validation");
        goto cleanup;
      } else if (napi_create_object(env, &item) != napi_ok ||
          napi_create_string_utf16(env, (const char16_t *)entry->FileName, chars, &value) != napi_ok ||
          napi_set_named_property(env, item, "name", value) != napi_ok ||
          napi_create_double(env, (double)entry->FileId.QuadPart, &value) != napi_ok ||
          napi_set_named_property(env, item, "fileId", value) != napi_ok ||
          napi_create_uint32(env, entry->FileAttributes, &value) != napi_ok ||
          napi_set_named_property(env, item, "attributes", value) != napi_ok ||
          napi_get_boolean(env, (entry->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, &value) != napi_ok ||
          napi_set_named_property(env, item, "isDirectory", value) != napi_ok ||
          napi_get_boolean(env, (entry->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0, &value) != napi_ok ||
          napi_set_named_property(env, item, "isReparsePoint", value) != napi_ok ||
          napi_set_element(env, entries, (uint32_t)count, item) != napi_ok) goto cleanup;
      if (!pageOverflow) count++;
    }
    if (entry->NextEntryOffset == 0) break;
    if (entry->NextEntryOffset < (DWORD)FIELD_OFFSET(FILE_ID_BOTH_DIR_INFO, FileName) ||
        entry->NextEntryOffset > bufferSize - offset ||
        entry->FileNameLength > entry->NextEntryOffset - FIELD_OFFSET(FILE_ID_BOTH_DIR_INFO, FileName)) {
      napi_throw_error(env, NULL, "directory enumeration returned an invalid next-entry offset");
      goto cleanup;
    }
    offset += entry->NextEntryOffset;
  }
  if (pageOverflow) {
    napi_throw_range_error(env, NULL, "directory page exceeds maxEntries; the current enumeration position was consumed, reopen the directory handle to restart");
    goto cleanup;
  }
  if (napi_create_object(env, &result) != napi_ok ||
      napi_set_named_property(env, result, "entries", entries) != napi_ok ||
      napi_get_boolean(env, TRUE, &value) != napi_ok ||
      napi_set_named_property(env, result, "pageRead", value) != napi_ok ||
      napi_get_boolean(env, FALSE, &value) != napi_ok ||
      napi_set_named_property(env, result, "done", value) != napi_ok) goto cleanup;
  free(buffer);
  return result;
cleanup:
  free(buffer);
  return NULL;
}



/* Reads metadata from one child opened relative to the held parent. A leaf
   reparse point is opened as the reparse object itself and reported as such;
   it is never followed. */
static napi_value inspect_entry_metadata(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value argv[2], result = NULL, value;
  ParentHandleRef *parent = NULL;
  char *name8 = NULL;
  wchar_t *nameWide = NULL;
  HANDLE child = INVALID_HANDLE_VALUE;
  FILE_ATTRIBUTE_TAG_INFO tag;
  FILE_STANDARD_INFO standard;
  FILE_BASIC_INFO basic;
  FILE_ID_INFO id;
  BY_HANDLE_FILE_INFORMATION legacy;
  ULARGE_INTEGER legacySize;
  BYTE reparseBuffer[16 * 1024];
  DWORD reparseBytes = 0;
  DWORD reparseLogicalSize = 0;
  char error[192];
  char fileId[33], sizeText[32], allocationText[32], legacySizeText[32], reparseSizeText[32], reparsePrintNameUtf8[1024] = {0};
  char creationText[32], accessText[32], writeText[32], changeText[32];
  if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc != 2) {
    napi_throw_type_error(env, NULL, "inspectEntryMetadata(parent, childName) requires two arguments");
    return NULL;
  }
  parent = get_parent_ref(env, argv[0]);
  if (!parent || parent->closed || parent->handle == INVALID_HANDLE_VALUE) {
    napi_throw_error(env, NULL, "parent handle is closed or invalid");
    return NULL;
  }
  if (!get_string(env, argv[1], &name8) || !utf8_to_wide(name8, &nameWide)) {
    napi_throw_type_error(env, NULL, "childName must be valid UTF-8");
    goto cleanup;
  }
  size_t nameLen = wcslen(nameWide);
  if (!is_safe_leaf_name(nameWide, nameLen)) {
    napi_throw_type_error(env, NULL, "invalid_name: childName must be one safe filename component");
    goto cleanup;
  }
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  NtCreateFileFn ntCreateFile = ntdll ? (NtCreateFileFn)GetProcAddress(ntdll, "NtCreateFile") : NULL;
  if (!ntCreateFile) {
    napi_throw_error(env, NULL, "NtCreateFile unavailable; failing closed");
    goto cleanup;
  }
  UNICODE_STRING name;
  name.Buffer = nameWide;
  name.Length = (USHORT)(nameLen * sizeof(wchar_t));
  name.MaximumLength = name.Length;
  OBJECT_ATTRIBUTES oa;
  InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE, parent->handle, NULL);
  IO_STATUS_BLOCK iosb;
  NTSTATUS status = ntCreateFile(&child,
      FILE_READ_ATTRIBUTES | SYNCHRONIZE, &oa, &iosb, NULL,
      FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT,
      NULL, 0);
  if (status < 0) {
    _snprintf_s(error, sizeof(error), _TRUNCATE,
        "%s: relative metadata open blocked (NTSTATUS 0x%08lX)", classify_ntstatus((ULONG)status), (unsigned long)status);
    napi_throw_error(env, NULL, error);
    goto cleanup;
  }
  if (GetFileType(child) != FILE_TYPE_DISK ||
      !GetFileInformationByHandleEx(child, FileAttributeTagInfo, &tag, sizeof(tag)) ||
      !GetFileInformationByHandleEx(child, FileStandardInfo, &standard, sizeof(standard)) ||
      !GetFileInformationByHandleEx(child, FileBasicInfo, &basic, sizeof(basic)) ||
      !GetFileInformationByHandleEx(child, FileIdInfo, &id, sizeof(id)) ||
      !GetFileInformationByHandle(child, &legacy) ||
      standard.EndOfFile.QuadPart < 0 || standard.AllocationSize.QuadPart < 0) {
    napi_throw_error(env, NULL, "child metadata unavailable or unsupported file type");
    goto cleanup;
  }
  if (!(tag.FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
      standard.NumberOfLinks != 1) {
    napi_throw_error(env, NULL, "hardlink_policy: regular file must have exactly one link");
    goto cleanup;
  }
  for (int i = 0; i < 16; ++i)
    _snprintf_s(fileId + i * 2, 3, _TRUNCATE, "%02X", id.FileId.Identifier[i]);
  _i64toa_s(standard.EndOfFile.QuadPart, sizeText, sizeof(sizeText), 10);
  _i64toa_s(standard.AllocationSize.QuadPart, allocationText, sizeof(allocationText), 10);
  legacySize.HighPart = legacy.nFileSizeHigh;
  legacySize.LowPart = legacy.nFileSizeLow;
  _ui64toa_s(legacySize.QuadPart, legacySizeText, sizeof(legacySizeText), 10);
  if ((tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
      !DeviceIoControl(child, FSCTL_GET_REPARSE_POINT, NULL, 0, reparseBuffer,
                       (DWORD)sizeof(reparseBuffer), &reparseBytes, NULL)) {
    napi_throw_error(env, NULL, "could not read reparse metadata from held reparse HANDLE");
    goto cleanup;
  }
  if (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
    USHORT reparseDataLength = 0;
    USHORT printNameLength = 0;
    USHORT printNameOffset = 0;
    DWORD fixedDataBytes;
    size_t pathBufferOffset;
    if (tag.ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
      fixedDataBytes = 8;
      pathBufferOffset = 16;
    } else if (tag.ReparseTag == IO_REPARSE_TAG_SYMLINK) {
      fixedDataBytes = 12;
      pathBufferOffset = 20;
    } else {
      /* Unknown reparse formats remain opaque; callers must reject unsupported tags. */
      goto reparse_metadata_done;
    }
    if (reparseBytes < pathBufferOffset) {
      napi_throw_error(env, NULL, "reparse buffer header is truncated");
      goto cleanup;
    }
    memcpy(&reparseDataLength, reparseBuffer + 4, sizeof(reparseDataLength));
    memcpy(&printNameOffset, reparseBuffer + 12, sizeof(printNameOffset));
    memcpy(&printNameLength, reparseBuffer + 14, sizeof(printNameLength));
    if ((DWORD)reparseDataLength > reparseBytes - 8 ||
        (DWORD)reparseDataLength < fixedDataBytes ||
        (printNameOffset & 1) != 0 || (printNameLength & 1) != 0 ||
        (DWORD)printNameOffset > (DWORD)reparseDataLength - fixedDataBytes ||
        (DWORD)printNameLength > (DWORD)reparseDataLength - fixedDataBytes - (DWORD)printNameOffset ||
        pathBufferOffset + (size_t)printNameOffset + (size_t)printNameLength > reparseBytes) {
      napi_throw_error(env, NULL, "reparse buffer print-name bounds are invalid");
      goto cleanup;
    }
    {
      const WCHAR *pathBuffer = (const WCHAR *)(reparseBuffer + pathBufferOffset);
      const WCHAR *printName = pathBuffer + (printNameOffset / sizeof(WCHAR));
      int converted = WideCharToMultiByte(CP_UTF8, 0, printName, printNameLength / 2, reparsePrintNameUtf8, (int)sizeof(reparsePrintNameUtf8) - 1, NULL, NULL);
      if (converted <= 0) {
        napi_throw_error(env, NULL, "could not convert reparse print name within adapter bound");
        goto cleanup;
      }
      reparsePrintNameUtf8[converted] = '\0';
      reparseLogicalSize = (DWORD)strlen(reparsePrintNameUtf8);
      if (reparseLogicalSize >= 4 && (unsigned char)reparsePrintNameUtf8[0] == 0x5c &&
          (unsigned char)reparsePrintNameUtf8[1] == 0x5c && (unsigned char)reparsePrintNameUtf8[2] == 0x3f &&
          (unsigned char)reparsePrintNameUtf8[3] == 0x5c) reparseLogicalSize -= 4;
    }
  }
reparse_metadata_done:
  _ultoa_s(reparseLogicalSize, reparseSizeText, sizeof(reparseSizeText), 10);
  _i64toa_s(basic.CreationTime.QuadPart, creationText, sizeof(creationText), 10);
  _i64toa_s(basic.LastAccessTime.QuadPart, accessText, sizeof(accessText), 10);
  _i64toa_s(basic.LastWriteTime.QuadPart, writeText, sizeof(writeText), 10);
  _i64toa_s(basic.ChangeTime.QuadPart, changeText, sizeof(changeText), 10);
  if (!close_handle_once(&child)) {
    napi_throw_error(env, NULL, "close_failure: CloseHandle failed after metadata read; ownership consumed");
    goto cleanup;
  }
  if (napi_create_object(env, &result) != napi_ok ||
      napi_create_string_utf8(env, fileId, NAPI_AUTO_LENGTH, &value) != napi_ok ||
      napi_set_named_property(env, result, "fileId", value) != napi_ok ||
      napi_create_double(env, (double)id.VolumeSerialNumber, &value) != napi_ok ||
      napi_set_named_property(env, result, "volumeSerialNumber", value) != napi_ok ||
      napi_create_string_utf8(env, sizeText, NAPI_AUTO_LENGTH, &value) != napi_ok ||
      napi_set_named_property(env, result, "size", value) != napi_ok ||
      napi_create_string_utf8(env, allocationText, NAPI_AUTO_LENGTH, &value) != napi_ok ||
      napi_set_named_property(env, result, "allocationSize", value) != napi_ok ||
      napi_create_string_utf8(env, legacySizeText, NAPI_AUTO_LENGTH, &value) != napi_ok ||
      napi_set_named_property(env, result, "legacySize", value) != napi_ok ||
      napi_create_string_utf8(env, reparseSizeText, NAPI_AUTO_LENGTH, &value) != napi_ok ||
      napi_set_named_property(env, result, "reparseLogicalSize", value) != napi_ok ||
      napi_create_string_utf8(env, reparsePrintNameUtf8, NAPI_AUTO_LENGTH, &value) != napi_ok ||
      napi_set_named_property(env, result, "reparsePrintName", value) != napi_ok ||
      napi_create_uint32(env, standard.NumberOfLinks, &value) != napi_ok ||
      napi_set_named_property(env, result, "numberOfLinks", value) != napi_ok ||
      napi_get_boolean(env, standard.Directory != FALSE, &value) != napi_ok ||
      napi_set_named_property(env, result, "isDirectory", value) != napi_ok ||
      napi_get_boolean(env, standard.DeletePending != FALSE, &value) != napi_ok ||
      napi_set_named_property(env, result, "deletePending", value) != napi_ok ||
      napi_create_uint32(env, tag.FileAttributes, &value) != napi_ok ||
      napi_set_named_property(env, result, "attributes", value) != napi_ok ||
      napi_create_uint32(env, tag.ReparseTag, &value) != napi_ok ||
      napi_set_named_property(env, result, "reparseTag", value) != napi_ok ||
      napi_get_boolean(env, (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0, &value) != napi_ok ||
      napi_set_named_property(env, result, "isReparsePoint", value) != napi_ok ||
      napi_create_string_utf8(env, creationText, NAPI_AUTO_LENGTH, &value) != napi_ok ||
      napi_set_named_property(env, result, "creationTime100ns", value) != napi_ok ||
      napi_create_string_utf8(env, accessText, NAPI_AUTO_LENGTH, &value) != napi_ok ||
      napi_set_named_property(env, result, "lastAccessTime100ns", value) != napi_ok ||
      napi_create_string_utf8(env, writeText, NAPI_AUTO_LENGTH, &value) != napi_ok ||
      napi_set_named_property(env, result, "lastWriteTime100ns", value) != napi_ok ||
      napi_create_string_utf8(env, changeText, NAPI_AUTO_LENGTH, &value) != napi_ok ||
      napi_set_named_property(env, result, "changeTime100ns", value) != napi_ok) goto cleanup;
  free(name8); free(nameWide);
  return result;
cleanup:
  if (child != INVALID_HANDLE_VALUE) (void)close_handle_once(&child);
  free(name8); free(nameWide);
  return NULL;
}

static napi_value init(napi_env env, napi_value exports) {
  napi_value fn;
  if (napi_create_function(env, "openRelative", NAPI_AUTO_LENGTH, open_relative, NULL, &fn) != napi_ok) return NULL;
  if (napi_set_named_property(env, exports, "openRelative", fn) != napi_ok) return NULL;
  if (napi_create_function(env, "processHandleCount", NAPI_AUTO_LENGTH, process_handle_count, NULL, &fn) != napi_ok) return NULL;
  if (napi_set_named_property(env, exports, "processHandleCount", fn) != napi_ok) return NULL;
  if (napi_create_function(env, "closeFailureCount", NAPI_AUTO_LENGTH, close_failure_count, NULL, &fn) != napi_ok) return NULL;
  if (napi_set_named_property(env, exports, "closeFailureCount", fn) != napi_ok) return NULL;
  if (napi_create_function(env, "failNextCloseForTest", NAPI_AUTO_LENGTH, fail_next_close_for_test, NULL, &fn) != napi_ok) return NULL;
  if (napi_set_named_property(env, exports, "failNextCloseForTest", fn) != napi_ok) return NULL;
  if (napi_create_function(env, "openWorkspaceRoot", NAPI_AUTO_LENGTH, open_workspace_root, NULL, &fn) != napi_ok) return NULL;
  if (napi_set_named_property(env, exports, "openWorkspaceRoot", fn) != napi_ok) return NULL;
  if (napi_create_function(env, "openParentRelative", NAPI_AUTO_LENGTH, open_parent_relative, NULL, &fn) != napi_ok) return NULL;
  if (napi_set_named_property(env, exports, "openParentRelative", fn) != napi_ok) return NULL;
  if (napi_create_function(env, "inspectParentHandle", NAPI_AUTO_LENGTH, inspect_parent_handle, NULL, &fn) != napi_ok) return NULL;
  if (napi_set_named_property(env, exports, "inspectParentHandle", fn) != napi_ok) return NULL;
  if (napi_create_function(env, "inspectChildFromParent", NAPI_AUTO_LENGTH, inspect_child_from_parent, NULL, &fn) != napi_ok) return NULL;
  if (napi_set_named_property(env, exports, "inspectChildFromParent", fn) != napi_ok) return NULL;
  if (napi_create_function(env, "closeParentHandle", NAPI_AUTO_LENGTH, close_parent_handle, NULL, &fn) != napi_ok) return NULL;
  if (napi_set_named_property(env, exports, "closeParentHandle", fn) != napi_ok) return NULL;
  if (napi_create_function(env, "readChildFromParent", NAPI_AUTO_LENGTH, read_child_from_parent, NULL, &fn) != napi_ok) return NULL;
  if (napi_set_named_property(env, exports, "readChildFromParent", fn) != napi_ok) return NULL;
  if (napi_create_function(env, "readTailChildFromParent", NAPI_AUTO_LENGTH, read_tail_child_from_parent, NULL, &fn) != napi_ok) return NULL;
  if (napi_set_named_property(env, exports, "readTailChildFromParent", fn) != napi_ok) return NULL;
  if (napi_create_function(env, "createChildExclusive", NAPI_AUTO_LENGTH, create_child_exclusive, NULL, &fn) != napi_ok) return NULL;
  if (napi_set_named_property(env, exports, "createChildExclusive", fn) != napi_ok) return NULL;
  if (napi_create_function(env, "createDirectoryChildExclusive", NAPI_AUTO_LENGTH, create_directory_child_exclusive, NULL, &fn) != napi_ok) return NULL;
  if (napi_set_named_property(env, exports, "createDirectoryChildExclusive", fn) != napi_ok) return NULL;
  if (napi_create_function(env, "openDirectoryChild", NAPI_AUTO_LENGTH, open_directory_child, NULL, &fn) != napi_ok) return NULL;
  if (napi_set_named_property(env, exports, "openDirectoryChild", fn) != napi_ok) return NULL;
  if (napi_create_function(env, "enumerateDirectoryPage", NAPI_AUTO_LENGTH, enumerate_directory_page, NULL, &fn) != napi_ok) return NULL;
  if (napi_set_named_property(env, exports, "enumerateDirectoryPage", fn) != napi_ok) return NULL;
  if (napi_create_function(env, "failNextDirectoryPageForTest", NAPI_AUTO_LENGTH, fail_next_directory_page_for_test, NULL, &fn) != napi_ok) return NULL;
  if (napi_set_named_property(env, exports, "failNextDirectoryPageForTest", fn) != napi_ok) return NULL;
  if (napi_create_function(env, "inspectEntryMetadata", NAPI_AUTO_LENGTH, inspect_entry_metadata, NULL, &fn) != napi_ok) return NULL;
  if (napi_set_named_property(env, exports, "inspectEntryMetadata", fn) != napi_ok) return NULL;
  if (napi_create_function(env, "classifyNtStatusForTest", NAPI_AUTO_LENGTH, classify_ntstatus_for_test, NULL, &fn) != napi_ok) return NULL;
  if (napi_set_named_property(env, exports, "classifyNtStatusForTest", fn) != napi_ok) return NULL;
  if (napi_create_function(env, "failNextChildOpenStatusForTest", NAPI_AUTO_LENGTH, fail_next_child_open_status_for_test, NULL, &fn) != napi_ok) return NULL;
  if (napi_set_named_property(env, exports, "failNextChildOpenStatusForTest", fn) != napi_ok) return NULL;
  return exports;
}
NAPI_MODULE(NODE_GYP_MODULE_NAME, init)