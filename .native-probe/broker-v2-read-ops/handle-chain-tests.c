#include "handle-chain.h"
#include <stdio.h>
#include <wchar.h>

static BOOL make_directory(const wchar_t *path)
{
    return CreateDirectoryW(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

int wmain(void)
{
    wchar_t temp[MAX_PATH], root[MAX_PATH], nested[MAX_PATH], deep[MAX_PATH];
    wchar_t outside[MAX_PATH], junction[MAX_PATH], moved[MAX_PATH];
    wchar_t command[3 * MAX_PATH + 64];
    RAYA_HANDLE_CHAIN chain = {0};
    HANDLE root_handle = INVALID_HANDLE_VALUE;
    BOOL ok = FALSE, junction_created = FALSE;
    DWORD n = GetTempPathW(MAX_PATH, temp);
    if (n == 0 || n >= MAX_PATH || GetTempFileNameW(temp, L"rhc", 0, root) == 0)
        return 2;
    DeleteFileW(root);
    if (!CreateDirectoryW(root, NULL)) return 2;

    _snwprintf_s(nested, MAX_PATH, _TRUNCATE, L"%ls\\a", root);
    _snwprintf_s(deep, MAX_PATH, _TRUNCATE, L"%ls\\a\\b", root);
    _snwprintf_s(outside, MAX_PATH, _TRUNCATE, L"%ls\\outside", root);
    _snwprintf_s(junction, MAX_PATH, _TRUNCATE, L"%ls\\jump", root);
    _snwprintf_s(moved, MAX_PATH, _TRUNCATE, L"%ls\\moved", root);
    if (!make_directory(nested) || !make_directory(deep) || !make_directory(outside))
        goto done;

    root_handle = CreateFileW(root,
        FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        NULL);
    if (root_handle == INVALID_HANDLE_VALUE) goto done;

    if (!raya_open_directory_chain(root_handle, L"a\\b", &chain) || chain.count != 2)
        goto done;
    wprintf(L"PASS nested directory chain\n");
    if (MoveFileW(nested, moved)) goto done;
    wprintf(L"PASS pinned parent blocks rename\n");
    raya_close_handle_chain(&chain);
    if (!MoveFileW(nested, moved) || !MoveFileW(moved, nested)) goto done;

    if (raya_open_directory_chain(root_handle, L"a\\..\\outside", &chain))
        goto done;
    wprintf(L"PASS traversal syntax rejected\n");

    _snwprintf_s(command, _countof(command), _TRUNCATE,
        L"cmd.exe /d /c mklink /J \"%ls\" \"%ls\" >nul", junction, outside);
    if (_wsystem(command) == 0) {
        junction_created = TRUE;
        if (raya_open_directory_chain(root_handle, L"jump", &chain))
            goto done;
        wprintf(L"PASS directory reparse point rejected\n");
    } else {
        wprintf(L"SKIP junction creation unavailable\n");
    }
    ok = TRUE;
done:
    raya_close_handle_chain(&chain);
    if (root_handle != INVALID_HANDLE_VALUE) CloseHandle(root_handle);
    if (junction_created) RemoveDirectoryW(junction);
    RemoveDirectoryW(deep);
    RemoveDirectoryW(nested);
    RemoveDirectoryW(moved);
    RemoveDirectoryW(outside);
    RemoveDirectoryW(root);
    if (ok) wprintf(L"HANDLE_CHAIN_TESTS_PASS\n");
    else wprintf(L"HANDLE_CHAIN_TESTS_FAIL error=%lu\n", GetLastError());
    return ok ? 0 : 1;
}
