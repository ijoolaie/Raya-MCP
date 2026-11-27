#include "handle-chain.h"
#include "relative-path.h"
#include <winternl.h>
#include <wchar.h>
#include <stdlib.h>
#include <limits.h>

#ifndef OBJ_DONT_REPARSE
#define OBJ_DONT_REPARSE 0x00001000L
#endif

typedef NTSTATUS (NTAPI *RayaNtCreateFileFn)(
    PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK,
    PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);

static RayaNtCreateFileFn get_nt_create_file(void)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll ? (RayaNtCreateFileFn)GetProcAddress(ntdll, "NtCreateFile") : NULL;
}

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

static BOOL valid_single_component(const wchar_t *name, size_t *chars)
{
    size_t count = 0;
    if (name == NULL || !raya_validate_relative_path(name, &count) || count != 1)
        return FALSE;
    *chars = wcslen(name);
    return *chars <= 255;
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
    RayaNtCreateFileFn nt;
    size_t component_count = 0, length, start, index = 0;
    HANDLE parent = root;

    if (chain == NULL || chain->handles != NULL || chain->count != 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (root == NULL || root == INVALID_HANDLE_VALUE ||
        !validate_directory_handle(root) ||
        !raya_validate_relative_path(relative_path, &component_count)) {
        SetLastError(ERROR_INVALID_NAME);
        return FALSE;
    }
    nt = get_nt_create_file();
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

BOOL raya_open_single_link_regular_file(HANDLE parent, const wchar_t *name,
                                        ULONGLONG maximum_size,
                                        HANDLE *file_handle, ULONGLONG *file_size)
{
    RayaNtCreateFileFn nt;
    UNICODE_STRING object_name;
    OBJECT_ATTRIBUTES attributes;
    IO_STATUS_BLOCK io;
    FILE_ATTRIBUTE_TAG_INFO tag;
    FILE_STANDARD_INFO standard;
    HANDLE file = INVALID_HANDLE_VALUE;
    size_t chars = 0;
    NTSTATUS status;

    if (file_handle == NULL || file_size == NULL ||
        *file_handle != NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *file_size = 0;
    if (parent == NULL || parent == INVALID_HANDLE_VALUE ||
        !validate_directory_handle(parent) ||
        !valid_single_component(name, &chars)) {
        SetLastError(ERROR_INVALID_NAME);
        return FALSE;
    }
    nt = get_nt_create_file();
    if (nt == NULL) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }

    object_name.Buffer = (PWSTR)name;
    object_name.Length = (USHORT)(chars * sizeof(wchar_t));
    object_name.MaximumLength = object_name.Length;
    InitializeObjectAttributes(&attributes, &object_name,
        OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE, parent, NULL);

    status = nt(&file,
        FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        &attributes, &io, NULL, FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ, FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT |
            FILE_NON_DIRECTORY_FILE,
        NULL, 0);
    if (status < 0) {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }

    if (GetFileType(file) != FILE_TYPE_DISK ||
        !GetFileInformationByHandleEx(file, FileAttributeTagInfo, &tag, sizeof(tag)) ||
        !GetFileInformationByHandleEx(file, FileStandardInfo, &standard, sizeof(standard)) ||
        (tag.FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
        standard.Directory || standard.DeletePending || standard.NumberOfLinks != 1 ||
        standard.EndOfFile.QuadPart < 0 ||
        (ULONGLONG)standard.EndOfFile.QuadPart > maximum_size) {
        CloseHandle(file);
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }

    *file_size = (ULONGLONG)standard.EndOfFile.QuadPart;
    *file_handle = file;
    return TRUE;
}
