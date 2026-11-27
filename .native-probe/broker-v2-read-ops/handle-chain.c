#include "handle-chain.h"
#include "relative-path.h"
#include <winternl.h>
#include <wchar.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

#ifndef OBJ_DONT_REPARSE
#define OBJ_DONT_REPARSE 0x00001000L
#endif

typedef NTSTATUS (NTAPI *RayaNtCreateFileFn)(
    PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK,
    PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);

static BOOL validate_directory_handle(HANDLE handle)
{
    FILE_ATTRIBUTE_TAG_INFO tag;
    FILE_STANDARD_INFO standard;
    return GetFileType(handle) == FILE_TYPE_DISK &&
        GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag, sizeof(tag)) &&
        GetFileInformationByHandleEx(handle, FileStandardInfo, &standard, sizeof(standard)) &&
        (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
        (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 &&
        standard.Directory && !standard.DeletePending;
}

static HANDLE open_child_directory(RayaNtCreateFileFn nt, HANDLE parent,
                                   const wchar_t *component, size_t chars)
{
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES attributes;
    IO_STATUS_BLOCK io;
    HANDLE child = INVALID_HANDLE_VALUE;
    NTSTATUS status;

    if (chars == 0 || chars > 255 || chars > (USHRT_MAX / sizeof(wchar_t))) {
        SetLastError(ERROR_INVALID_NAME);
        return INVALID_HANDLE_VALUE;
    }

    name.Buffer = (PWSTR)component;
    name.Length = (USHORT)(chars * sizeof(wchar_t));
    name.MaximumLength = name.Length;
    InitializeObjectAttributes(&attributes, &name,
        OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE, parent, NULL);

    status = nt(&child,
        FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        &attributes, &io, NULL, FILE_ATTRIBUTE_DIRECTORY,
        FILE_SHARE_READ, FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT |
            FILE_DIRECTORY_FILE,
        NULL, 0);
    if (status < 0) {
        SetLastError(ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }
    if (!validate_directory_handle(child)) {
        DWORD error = GetLastError();
        CloseHandle(child);
        SetLastError(error ? error : ERROR_REPARSE_TAG_INVALID);
        return INVALID_HANDLE_VALUE;
    }
    return child;
}

void raya_close_handle_chain(RAYA_HANDLE_CHAIN *chain)
{
    if (chain == NULL) return;
    if (chain->handles != NULL) {
        for (size_t i = chain->count; i > 0; --i) {
            if (chain->handles[i - 1] != INVALID_HANDLE_VALUE)
                CloseHandle(chain->handles[i - 1]);
        }
        free(chain->handles);
    }
    chain->handles = NULL;
    chain->count = 0;
}

BOOL raya_open_directory_chain(HANDLE root, const wchar_t *relative_path,
                               RAYA_HANDLE_CHAIN *chain)
{
    HMODULE ntdll;
    RayaNtCreateFileFn nt;
    size_t component_count = 0, length, start, index = 0;
    HANDLE parent = root;

    if (chain == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    chain->handles = NULL;
    chain->count = 0;
    if (root == NULL || root == INVALID_HANDLE_VALUE ||
        !validate_directory_handle(root) ||
        !raya_validate_relative_path(relative_path, &component_count)) {
        SetLastError(ERROR_INVALID_NAME);
        return FALSE;
    }

    ntdll = GetModuleHandleW(L"ntdll.dll");
    nt = ntdll ? (RayaNtCreateFileFn)GetProcAddress(ntdll, "NtCreateFile") : NULL;
    if (nt == NULL) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }

    chain->handles = (HANDLE *)calloc(component_count, sizeof(HANDLE));
    if (chain->handles == NULL) {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
    for (size_t i = 0; i < component_count; ++i)
        chain->handles[i] = INVALID_HANDLE_VALUE;

    length = wcslen(relative_path);
    start = 0;
    for (size_t pos = 0; pos <= length; ++pos) {
        if (pos == length || relative_path[pos] == L'\\') {
            HANDLE child = open_child_directory(nt, parent,
                relative_path + start, pos - start);
            if (child == INVALID_HANDLE_VALUE) {
                raya_close_handle_chain(chain);
                return FALSE;
            }
            chain->handles[index++] = child;
            chain->count = index;
            parent = child;
            start = pos + 1;
        }
    }
    return TRUE;
}