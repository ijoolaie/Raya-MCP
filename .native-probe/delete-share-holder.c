#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
int wmain(int argc, wchar_t **argv) {
  if (argc != 2) return 2;
  HANDLE h = CreateFileW(argv[1], FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | DELETE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    printf("OPEN_FAILED:%lu\n", GetLastError());
    return 3;
  }
  printf("READY\n");
  fflush(stdout);
  (void)getchar();
  if (!CloseHandle(h)) {
    printf("CLOSE_FAILED:%lu\n", GetLastError());
    return 4;
  }
  printf("CLOSED\n");
  return 0;
}
