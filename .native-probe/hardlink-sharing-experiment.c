#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static int child_link(const wchar_t *link, const wchar_t *source) {
  if (CreateHardLinkW(link, source, NULL)) return 0;
  return (int)GetLastError();
}

static int run_case(const wchar_t *label, DWORD share) {
  wchar_t dir[MAX_PATH], source[MAX_PATH], link[MAX_PATH], exe[MAX_PATH], cmd[4 * MAX_PATH];
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

  n = GetModuleFileNameW(NULL, exe, MAX_PATH);
  if (!n || n >= MAX_PATH) { CloseHandle(held); DeleteFileW(source); return 93; }
  _snwprintf_s(cmd, _countof(cmd), _TRUNCATE, L"\"%ls\" --link \"%ls\" \"%ls\"", exe, link, source);
  STARTUPINFOW si = { sizeof(si) };
  PROCESS_INFORMATION pi = {0};
  BOOL started = CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
  if (!started) {
    DWORD e = GetLastError();
    wprintf(L"%ls: child launch failed %lu\n", label, e);
    CloseHandle(held); DeleteFileW(source); return 94;
  }
  WaitForSingleObject(pi.hProcess, 10000);
  DWORD childExit = 999;
  GetExitCodeProcess(pi.hProcess, &childExit);
  CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
  wprintf(L"%ls: cross-process share=0x%08lx hardlink=%ls childExit=%lu\n",
    label, share, childExit == 0 ? L"SUCCESS" : L"BLOCKED", childExit);
  CloseHandle(held);
  DeleteFileW(link); DeleteFileW(source);
  return 0;
}

int wmain(int argc, wchar_t **argv) {
  if (argc == 4 && wcscmp(argv[1], L"--link") == 0)
    return child_link(argv[2], argv[3]);
  int a = run_case(L"share-none", 0);
  int b = run_case(L"share-read", FILE_SHARE_READ);
  int c = run_case(L"share-all", FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
  return (a || b || c) ? 1 : 0;
}
