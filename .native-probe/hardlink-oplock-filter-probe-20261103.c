#define UNICODE
#define _UNICODE
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <wchar.h>

static int child_link(const wchar_t *linkPath, const wchar_t *sourcePath) {
  if (CreateHardLinkW(linkPath, sourcePath, NULL)) return 0;
  return (int)GetLastError();
}
static void cleanup(HANDLE *h) {
  if (h && *h && *h != INVALID_HANDLE_VALUE) {
    CloseHandle(*h);
    *h = INVALID_HANDLE_VALUE;
  }
}
int wmain(int argc, wchar_t **argv) {
  if (argc == 4 && wcscmp(argv[1], L"--link") == 0)
    return child_link(argv[2], argv[3]);

  wchar_t temp[MAX_PATH], source[MAX_PATH], linkPath[MAX_PATH], exe[MAX_PATH], cmd[4 * MAX_PATH];
  DWORD n = GetTempPathW(MAX_PATH, temp);
  if (!n || n >= MAX_PATH) return 90;
  _snwprintf_s(source, MAX_PATH, _TRUNCATE, L"%sraya-filter-oplock-%lu.src", temp, GetCurrentProcessId());
  _snwprintf_s(linkPath, MAX_PATH, _TRUNCATE, L"%sraya-filter-oplock-%lu.link", temp, GetCurrentProcessId());
  DeleteFileW(source); DeleteFileW(linkPath);

  HANDLE created = CreateFileW(source, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
  if (created == INVALID_HANDLE_VALUE) { wprintf(L"fixture-create-error=%lu\n", GetLastError()); return 91; }
  CloseHandle(created);

  HANDLE held = CreateFileW(source, FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
  if (held == INVALID_HANDLE_VALUE) { wprintf(L"held-open-error=%lu\n", GetLastError()); DeleteFileW(source); return 92; }

  OVERLAPPED ov = {0};
  ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
  if (!ov.hEvent) { cleanup(&held); DeleteFileW(source); return 93; }
  DWORD bytes = 0;
  BOOL requestOk = DeviceIoControl(held, FSCTL_REQUEST_FILTER_OPLOCK,
      NULL, 0, NULL, 0, &bytes, &ov);
  DWORD requestError = requestOk ? ERROR_SUCCESS : GetLastError();
  if (requestOk || requestError != ERROR_IO_PENDING) {
    wprintf(L"filter-oplock-grant-failed error=%lu granted=false\n", requestError);
    CloseHandle(ov.hEvent); cleanup(&held); DeleteFileW(source); return 94;
  }

  n = GetModuleFileNameW(NULL, exe, MAX_PATH);
  if (!n || n >= MAX_PATH) { CloseHandle(ov.hEvent); cleanup(&held); DeleteFileW(source); return 95; }
  _snwprintf_s(cmd, _countof(cmd), _TRUNCATE, L"\"%ls\" --link \"%ls\" \"%ls\"", exe, linkPath, source);
  STARTUPINFOW si = { sizeof(si) };
  PROCESS_INFORMATION pi = {0};
  if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
    wprintf(L"child-launch-error=%lu\n", GetLastError());
    CloseHandle(ov.hEvent); cleanup(&held); DeleteFileW(source); return 96;
  }

  DWORD breakWait = WaitForSingleObject(ov.hEvent, 5000);
  DWORD overlappedStatus = ERROR_SUCCESS;
  if (breakWait == WAIT_OBJECT_0 && !GetOverlappedResult(held, &ov, &bytes, FALSE))
    overlappedStatus = GetLastError();
  DWORD childBeforeClose = WaitForSingleObject(pi.hProcess, 750);
  DWORD childExitBeforeClose = 999;
  if (childBeforeClose == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &childExitBeforeClose);
  DWORD linkBeforeClose = GetFileAttributesW(linkPath);

  wprintf(L"filesystem-local-temp; oplock=FILTER; breakObserved=%ls breakWait=%lu overlappedStatus=%lu "
      L"childBeforeClose=%ls childExitBeforeClose=%lu linkExistsBeforeClose=%ls\n",
      breakWait == WAIT_OBJECT_0 ? L"true" : L"false", breakWait, overlappedStatus,
      childBeforeClose == WAIT_OBJECT_0 ? L"exited" : (childBeforeClose == WAIT_TIMEOUT ? L"pending" : L"wait-error"),
      childExitBeforeClose, linkBeforeClose != INVALID_FILE_ATTRIBUTES ? L"true" : L"false");

  cleanup(&held);
  DWORD childAfterClose = WaitForSingleObject(pi.hProcess, 5000);
  DWORD childExitAfterClose = 999;
  if (childAfterClose == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &childExitAfterClose);
  DWORD linkAfterClose = GetFileAttributesW(linkPath);
  wprintf(L"childAfterClose=%ls childExitAfterClose=%lu linkExistsAfterClose=%ls\n",
      childAfterClose == WAIT_OBJECT_0 ? L"exited" : (childAfterClose == WAIT_TIMEOUT ? L"pending" : L"wait-error"),
      childExitAfterClose, linkAfterClose != INVALID_FILE_ATTRIBUTES ? L"true" : L"false");

  CloseHandle(pi.hThread); CloseHandle(pi.hProcess); CloseHandle(ov.hEvent);
  DeleteFileW(linkPath); DeleteFileW(source);
  if (breakWait != WAIT_OBJECT_0 || childBeforeClose != WAIT_TIMEOUT ||
      linkBeforeClose != INVALID_FILE_ATTRIBUTES || childAfterClose != WAIT_OBJECT_0 ||
      childExitAfterClose != 0 || linkAfterClose == INVALID_FILE_ATTRIBUTES) return 97;
  return 0;
}
