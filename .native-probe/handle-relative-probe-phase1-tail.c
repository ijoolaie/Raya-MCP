#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <node_api.h>
#include <stdint.h>

static volatile LONG g_close_failures = 0;
static volatile LONG g_fail_next_close_after_real_close = 0;

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
      _snprintf_s(error, errorCap, _TRUNCATE, "root contains unsupported slash or colon syntax");
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
    if (componentLen == 0 || componentLen > 255 ||
        (componentLen == 1 && root[pos] == L'.') ||
        (componentLen == 2 && root[pos] == L'.' && root[pos + 1] == L'.')) {
      close_handle_once(&current);
      _snprintf_s(error, errorCap, _TRUNCATE, "invalid root path component");
      return INVALID_HANDLE_VALUE;
    }

    wchar_t component[256];
    wmemcpy(component, root + pos, componentLen);
    component[componentLen] = L'\0';
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
      _snprintf_s(error, errorCap, _TRUNCATE, "root component open blocked (NTSTATUS 0x%08lX)", (unsigned long)status);
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
      _snprintf_s(error, errorCap, _TRUNCATE, "failed to release prior root-chain handle");
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
      _snprintf_s(error, sizeof(error), _TRUNCATE, "relative component open blocked (NTSTATUS 0x%08lX)", (unsigned long)lastStatus);
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
    if (cursor[0] == L'\0' || wcscmp(cursor, L".") == 0 || wcscmp(cursor, L"..") == 0) {
      napi_throw_type_error(env, NULL, "empty, dot, and dot-dot parent components are forbidden");
      goto cleanup;
    }
    size_t componentLen = wcslen(cursor);
    if (componentLen > 255) {
      napi_throw_type_error(env, NULL, "parent path component is too long");
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
          "relative parent component open blocked (NTSTATUS 0x%08lX)", (unsigned long)status);
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
  if (!GetFileInformationByHandleEx(ref->handle, FileIdInfo, &id, sizeof(id)) ||
      !GetFileInformationByHandleEx(ref->handle, FileAttributeTagInfo, &tag, sizeof(tag))) {
    napi_throw_error(env, NULL, "could not inspect held parent handle");
    return NULL;
  }
  char fileId[33];
  for (int i = 0; i < 16; ++i) _snprintf_s(fileId + i * 2, 3, _TRUNCATE, "%02X", id.FileId.Identifier[i]);
  if (napi_create_object(env, &result) != napi_ok ||
      napi_create_double(env, (double)id.VolumeSerialNumber, &value) != napi_ok ||
      napi_set_named_property(env, result, "volumeSerialNumber", value) != napi_ok ||
      napi_create_string_utf8(env, fileId, NAPI_AUTO_LENGTH, &value) != napi_ok ||
      napi_set_named_property(env, result, "fileId", value) != napi_ok ||
      napi_get_boolean(env, (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, &value) != napi_ok ||
      napi_set_named_property(env, result, "isDirectory", value) != napi_ok ||
      napi_get_boolean(env, (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0, &value) != napi_ok ||
      napi_set_named_property(env, result, "isReparsePoint", value) != napi_ok) return NULL;
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
    napi_throw_type_error(env, NULL, "childName must be one safe filename component");
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
        "child identity open relative to held parent blocked (NTSTATUS 0x%08lX)", (unsigned long)status);
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
    napi_throw_error(env, NULL, "CloseHandle failed after child identity readback; ownership consumed");
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
      napi_throw_error(env, NULL, "CloseHandle failed for parent handle; ownership consumed and will not be retried");
      return NULL;
    }
  }
  if (napi_get_boolean(env, TRUE, &result) != napi_ok) return NULL;
  return result;
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
    napi_throw_type_error(env, NULL, "childName must be one safe filename component");
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
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
      FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | FILE_NON_DIRECTORY_FILE,
      NULL, 0);
  if (status < 0) {
    _snprintf_s(error, sizeof(error), _TRUNCATE,
        "child open relative to held parent blocked (NTSTATUS 0x%08lX)", (unsigned long)status);
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
    napi_throw_error(env, NULL, "CloseHandle failed after bounded child read; ownership consumed and will not be retried");
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
    napi_throw_type_error(env, NULL, "childName must be one safe filename component");
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
        "child open relative to held parent blocked (NTSTATUS 0x%08lX)", (unsigned long)status);
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
    napi_throw_error(env, NULL, "CloseHandle failed after bounded child tail read; ownership consumed and will not be retried");
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
    napi_throw_type_error(env, NULL, "childName must be one safe filename component");
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
        "exclusive child creation blocked (NTSTATUS 0x%08lX)", (unsigned long)status);
    napi_throw_error(env, NULL, error);
    goto cleanup;
  }
  FILE_ATTRIBUTE_TAG_INFO tag;
  if (is_reparse_or_bad(child, &tag) || (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
    napi_throw_error(env, NULL, "created child has a disallowed type or reparse attribute");
    goto cleanup;
  }
  if (!close_handle_once(&child)) {
    napi_throw_error(env, NULL, "CloseHandle failed after exclusive child creation; ownership consumed");
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
    napi_throw_type_error(env, NULL, "childName must be one safe filename component");
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
        "exclusive child directory creation blocked (NTSTATUS 0x%08lX)", (unsigned long)status);
    napi_throw_error(env, NULL, error);
    goto cleanup;
  }
  FILE_ATTRIBUTE_TAG_INFO tag;
  if (is_reparse_or_bad(child, &tag) || !(tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
    napi_throw_error(env, NULL, "created child directory has a disallowed type or reparse attribute");
    goto cleanup;
  }
  if (!close_handle_once(&child)) {
    napi_throw_error(env, NULL, "CloseHandle failed after exclusive child directory creation; ownership consumed");
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
  return exports;
}
NAPI_MODULE(NODE_GYP_MODULE_NAME, init)