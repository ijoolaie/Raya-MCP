#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static int run_case(const wchar_t *label, DWORD share) {
  wchar_t dir[MAX_PATH], source[MAX_PATH], link[MAX_PATH];
  DWORD n = GetTempPathW(MAX_PATH, dir);
  if (!n || n >= MAX_PATH) return 90;
  _snwprintf_s(source, MAX_PATH, _TRUNCATE, L"%sraya-hardlink-%lu-source.tmp", dir, GetCurrentProcessId());
  _snwprintf_s(link, MAX_PATH, _TRUNCATE, L"%sraya-hardlink-%lu-link.tmp", dir, GetCurrentProcessId());
  DeleteFileW(source); DeleteFileW(link);
  HANDLE create = CreateFileW(source, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
  if (create == INVALID_HANDLE_VALUE) { wprintf(L"%ls: fixture create failed %lu\n", label, GetLastError()); return 91; }
  CloseHandle(create);
  HANDLE held = CreateFileW(source, GENERIC_READ, share, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (held == INVALID_HANDLE_VALUE) { wprintf(L"%ls: held open failed %lu\n", label, GetLastError()); DeleteFileW(source); return 92; }
  SetLastError(0);
  BOOL ok = CreateHardLinkW(link, source, NULL);
  DWORD error = ok ? 0 : GetLastError();
  wprintf(L"%ls: share=0x%08lx hardlink=%ls error=%lu\n", label, share, ok ? L"SUCCESS" : L"BLOCKED", error);
  CloseHandle(held);
  DeleteFileW(link); DeleteFileW(source);
  return 0;
}
int wmain(void) {
  int a = run_case(L"share-none", 0);
  int b = run_case(L"share-all", FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
  return (a || b) ? 1 : 0;
}
