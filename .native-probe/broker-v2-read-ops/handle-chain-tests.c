#include "handle-chain.h"
#include <stdio.h>
#include <wchar.h>
#include <string.h>

static BOOL make_directory(const wchar_t *path)
{
    return CreateDirectoryW(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static BOOL create_file(const wchar_t *path, const char *data)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0;
    size_t length = strlen(data);
    BOOL ok;
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    ok = WriteFile(h, data, (DWORD)length, &written, NULL) && written == length;
    CloseHandle(h);
    return ok;
}

int wmain(void)
{
    wchar_t temp[MAX_PATH], root[MAX_PATH], nested[MAX_PATH], deep[MAX_PATH];
    wchar_t outside[MAX_PATH], outside_file[MAX_PATH], junction[MAX_PATH], moved[MAX_PATH];
    wchar_t file_path[MAX_PATH], hardlink_path[MAX_PATH], symlink_path[MAX_PATH];
    wchar_t command[3 * MAX_PATH + 64];
    RAYA_HANDLE_CHAIN chain = {0};
    HANDLE root_handle = INVALID_HANDLE_VALUE, file_handle = NULL;
    BOOL ok = FALSE, junction_created = FALSE, hardlink_created = FALSE;
    BOOL symlink_created = FALSE;
    DWORD n, read_count = 0;
    char readback[4] = {0};
    LARGE_INTEGER zero;
    FILE_STANDARD_INFO standard;
    ULONGLONG file_size = 0;

    n = GetTempPathW(MAX_PATH, temp);
    if (n == 0 || n >= MAX_PATH || GetTempFileNameW(temp, L"rhc", 0, root) == 0)
        return 2;
    DeleteFileW(root);
    if (!CreateDirectoryW(root, NULL)) return 2;

    _snwprintf_s(nested, MAX_PATH, _TRUNCATE, L"%ls\\a", root);
    _snwprintf_s(deep, MAX_PATH, _TRUNCATE, L"%ls\\a\\b", root);
    _snwprintf_s(outside, MAX_PATH, _TRUNCATE, L"%ls\\outside", root);
    _snwprintf_s(outside_file, MAX_PATH, _TRUNCATE, L"%ls\\outside.txt", root);
    _snwprintf_s(junction, MAX_PATH, _TRUNCATE, L"%ls\\jump", root);
    _snwprintf_s(moved, MAX_PATH, _TRUNCATE, L"%ls\\moved", root);
    _snwprintf_s(file_path, MAX_PATH, _TRUNCATE, L"%ls\\file.txt", root);
    _snwprintf_s(hardlink_path, MAX_PATH, _TRUNCATE, L"%ls\\hardlink.txt", root);
    _snwprintf_s(symlink_path, MAX_PATH, _TRUNCATE, L"%ls\\symlink.txt", root);
    if (!make_directory(nested) || !make_directory(deep) ||
        !make_directory(outside) || !create_file(file_path, "abc") ||
        !create_file(outside_file, "outside")) goto done;

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

    if (!raya_open_single_link_regular_file(root_handle, L"file.txt", 3,
                                             &file_handle, &file_size) ||
        file_size != 3) goto done;
    wprintf(L"PASS single-link regular file opened\n");

    if (!CreateHardLinkW(hardlink_path, file_path, NULL)) goto done;
    hardlink_created = TRUE;
    if (!GetFileInformationByHandleEx(file_handle, FileStandardInfo,
                                       &standard, sizeof(standard)) ||
        standard.NumberOfLinks != 2) goto done;
    zero.QuadPart = 0;
    if (!SetFilePointerEx(file_handle, zero, NULL, FILE_BEGIN) ||
        !ReadFile(file_handle, readback, 3, &read_count, NULL) ||
        read_count != 3 || memcmp(readback, "abc", 3) != 0) goto done;
    wprintf(L"PASS pinned file handle remains object-stable after link-count change\n");
    CloseHandle(file_handle);
    file_handle = NULL;

    if (raya_open_single_link_regular_file(root_handle, L"file.txt", 3,
                                            &file_handle, &file_size))
        goto done;
    wprintf(L"PASS subsequent open rejects hard-linked file\n");

    if (raya_open_single_link_regular_file(root_handle, L"file.txt", 2,
                                            &file_handle, &file_size))
        goto done;
    wprintf(L"PASS file size cap enforced\n");

    if (raya_open_single_link_regular_file(root_handle, L"a", 1024,
                                            &file_handle, &file_size))
        goto done;
    wprintf(L"PASS directory rejected as regular file\n");

    if (CreateSymbolicLinkW(symlink_path, outside_file, 0x2)) {
        symlink_created = TRUE;
        if (raya_open_single_link_regular_file(root_handle, L"symlink.txt", 1024,
                                                &file_handle, &file_size))
            goto done;
        wprintf(L"PASS file reparse point rejected\n");
    } else {
        wprintf(L"SKIP file symlink creation unavailable\n");
    }

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
    if (file_handle != NULL && file_handle != INVALID_HANDLE_VALUE) CloseHandle(file_handle);
    raya_close_handle_chain(&chain);
    if (root_handle != INVALID_HANDLE_VALUE) CloseHandle(root_handle);
    if (junction_created) RemoveDirectoryW(junction);
    if (symlink_created) DeleteFileW(symlink_path);
    if (hardlink_created) DeleteFileW(hardlink_path);
    DeleteFileW(file_path);
    DeleteFileW(outside_file);
    RemoveDirectoryW(deep);
    RemoveDirectoryW(nested);
    RemoveDirectoryW(moved);
    RemoveDirectoryW(outside);
    RemoveDirectoryW(root);
    if (ok) wprintf(L"HANDLE_CHAIN_TESTS_PASS\n");
    else wprintf(L"HANDLE_CHAIN_TESTS_FAIL error=%lu\n", GetLastError());
    return ok ? 0 : 1;
}
