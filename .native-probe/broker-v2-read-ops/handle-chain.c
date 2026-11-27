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

BOOL raya_reopen_pinned_directory(HANDLE directory, HANDLE *reopened)
{
    FILE_ID_INFO original_id;
    FILE_ID_INFO reopened_id;
    HANDLE candidate = INVALID_HANDLE_VALUE;
    wchar_t *path = NULL;
    DWORD needed;
    DWORD written;
    DWORD error;

    if (directory == NULL || directory == INVALID_HANDLE_VALUE ||
        reopened == NULL || *reopened != NULL ||
        !validate_directory_handle(directory)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    ZeroMemory(&original_id, sizeof(original_id));
    ZeroMemory(&reopened_id, sizeof(reopened_id));
    if (!GetFileInformationByHandleEx(directory, FileIdInfo,
            &original_id, sizeof(original_id)))
        return FALSE;

    needed = GetFinalPathNameByHandleW(directory, NULL, 0,
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (needed == 0)
        return FALSE;
    path = (wchar_t *)calloc((size_t)needed + 1u, sizeof(wchar_t));
    if (path == NULL) {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }

    written = GetFinalPathNameByHandleW(directory, path, needed + 1u,
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (written == 0 || written > needed) {
        error = GetLastError();
        free(path);
        SetLastError(error != ERROR_SUCCESS ? error : ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    /* The path is obtained from the already-pinned HANDLE, never from request
       data. Reopen is accepted only if the resulting HANDLE has the exact same
       volume serial and 128-bit file ID as the pinned object. */
    candidate = CreateFileW(path,
        FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ,
        NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        NULL);
    error = GetLastError();
    free(path);
    if (candidate == INVALID_HANDLE_VALUE) {
        SetLastError(error);
        return FALSE;
    }

    if (!validate_directory_handle(candidate) ||
        !GetFileInformationByHandleEx(candidate, FileIdInfo,
            &reopened_id, sizeof(reopened_id)) ||
        original_id.VolumeSerialNumber != reopened_id.VolumeSerialNumber ||
        memcmp(&original_id.FileId, &reopened_id.FileId,
            sizeof(original_id.FileId)) != 0) {
        error = GetLastError();
        CloseHandle(candidate);
        SetLastError(error != ERROR_SUCCESS ? error : ERROR_FILE_INVALID);
        return FALSE;
    }

    *reopened = candidate;
    return TRUE;
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


#define RAYA_DIRECTORY_PAGE_BYTES 65536u
#define RAYA_DIRECTORY_MAX_ENTRIES 1024u

static BOOL directory_record_name(
    const FILE_ID_BOTH_DIR_INFO *record,
    wchar_t name[256],
    size_t *name_chars)
{
    size_t chars;

    if ((record->FileNameLength % sizeof(wchar_t)) != 0)
        return FALSE;

    chars = record->FileNameLength / sizeof(wchar_t);
    if (chars == 0 || chars > 255)
        return FALSE;

    for (size_t i = 0; i < chars; ++i) {
        if (record->FileName[i] == L'\0' ||
            record->FileName[i] == L'\\' ||
            record->FileName[i] == L'/' ||
            record->FileName[i] == L':')
            return FALSE;
    }

    memcpy(name, record->FileName, chars * sizeof(wchar_t));
    name[chars] = L'\0';

    if (wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0) {
        *name_chars = 0;
        return TRUE;
    }

    size_t components = 0;
    if (!raya_validate_relative_path(name, &components) || components != 1)
        return FALSE;

    *name_chars = chars;
    return TRUE;
}

static BOOL validate_directory_page_buffer(
    const BYTE *buffer,
    size_t buffer_size,
    size_t *entry_count)
{
    size_t offset = 0;
    size_t count = 0;
    const size_t name_offset = FIELD_OFFSET(FILE_ID_BOTH_DIR_INFO, FileName);

    if (buffer == NULL || entry_count == NULL)
        return FALSE;

    for (;;) {
        const FILE_ID_BOTH_DIR_INFO *record;
        size_t minimum_size;
        size_t span;
        size_t chars = 0;
        wchar_t name[256];

        if (offset > buffer_size ||
            buffer_size - offset < name_offset) {
            SetLastError(ERROR_INVALID_DATA);
            return FALSE;
        }

        record = (const FILE_ID_BOTH_DIR_INFO *)(const void *)(buffer + offset);
        if ((record->FileNameLength % sizeof(wchar_t)) != 0 ||
            record->FileNameLength > 255u * sizeof(wchar_t) ||
            record->EndOfFile.QuadPart < 0 || record->AllocationSize.QuadPart < 0) {
            SetLastError(ERROR_INVALID_DATA);
            return FALSE;
        }

        minimum_size = name_offset + record->FileNameLength;
        if (minimum_size > buffer_size - offset) {
            SetLastError(ERROR_INVALID_DATA);
            return FALSE;
        }

        if (record->NextEntryOffset == 0) {
            span = minimum_size;
        } else {
            span = record->NextEntryOffset;
            if ((span % 8u) != 0 || span < minimum_size ||
                span > buffer_size - offset) {
                SetLastError(ERROR_INVALID_DATA);
                return FALSE;
            }
        }

        if (!directory_record_name(record, name, &chars)) {
            SetLastError(ERROR_INVALID_NAME);
            return FALSE;
        }
        if (chars != 0)
            ++count;

        if (record->NextEntryOffset == 0)
            break;

        offset += span;
    }

    *entry_count = count;
    return TRUE;
}

static BOOL copy_directory_page_entries(
    const BYTE *buffer,
    size_t buffer_size,
    RAYA_DIRECTORY_ENTRY *entries,
    size_t expected_count)
{
    size_t offset = 0;
    size_t index = 0;
    const size_t name_offset = FIELD_OFFSET(FILE_ID_BOTH_DIR_INFO, FileName);

    for (;;) {
        const FILE_ID_BOTH_DIR_INFO *record =
            (const FILE_ID_BOTH_DIR_INFO *)(const void *)(buffer + offset);
        size_t span = record->NextEntryOffset == 0
            ? name_offset + record->FileNameLength
            : record->NextEntryOffset;
        size_t chars = 0;
        wchar_t name[256];

        if (!directory_record_name(record, name, &chars))
            return FALSE;

        if (chars != 0) {
            RAYA_DIRECTORY_ENTRY *entry;
            if (index >= expected_count) {
                SetLastError(ERROR_INVALID_DATA);
                return FALSE;
            }

            entry = &entries[index++];
            memcpy(entry->name, name, (chars + 1) * sizeof(wchar_t));
            entry->attributes = record->FileAttributes;
            entry->file_id = (ULONGLONG)record->FileId.QuadPart;
            entry->size = record->EndOfFile.QuadPart < 0
                ? 0 : (ULONGLONG)record->EndOfFile.QuadPart;
            entry->allocation_size = record->AllocationSize.QuadPart < 0
                ? 0 : (ULONGLONG)record->AllocationSize.QuadPart;
            entry->creation_time = record->CreationTime.QuadPart;
            entry->last_access_time = record->LastAccessTime.QuadPart;
            entry->last_write_time = record->LastWriteTime.QuadPart;
            entry->change_time = record->ChangeTime.QuadPart;
        }

        if (record->NextEntryOffset == 0)
            break;

        if (span == 0 || span > buffer_size - offset) {
            SetLastError(ERROR_INVALID_DATA);
            return FALSE;
        }
        offset += span;
    }

    if (index != expected_count) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    return TRUE;
}

BOOL raya_enumerate_directory_page(
    HANDLE directory,
    size_t max_entries,
    BOOL restart,
    RAYA_DIRECTORY_PAGE *page)
{
    BYTE *buffer;
    size_t count = 0;
    FILE_INFO_BY_HANDLE_CLASS info_class;

    if (page == NULL || page->entries != NULL || page->count != 0 ||
        page->done || directory == NULL || directory == INVALID_HANDLE_VALUE ||
        max_entries == 0 || max_entries > RAYA_DIRECTORY_MAX_ENTRIES) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!validate_directory_handle(directory)) {
        DWORD error = GetLastError();
        SetLastError(error ? error : ERROR_INVALID_HANDLE);
        return FALSE;
    }

    buffer = (BYTE *)calloc(RAYA_DIRECTORY_PAGE_BYTES, 1);
    if (buffer == NULL) {
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }

    info_class = restart ? FileIdBothDirectoryRestartInfo : FileIdBothDirectoryInfo;
    if (!GetFileInformationByHandleEx(directory, info_class, buffer,
            RAYA_DIRECTORY_PAGE_BYTES)) {
        DWORD error = GetLastError();
        free(buffer);
        if (error == ERROR_NO_MORE_FILES) {
            page->done = TRUE;
            return TRUE;
        }
        SetLastError(error);
        return FALSE;
    }

    if (!validate_directory_page_buffer(buffer, RAYA_DIRECTORY_PAGE_BYTES, &count)) {
        DWORD error = GetLastError();
        free(buffer);
        SetLastError(error);
        return FALSE;
    }

    if (count > max_entries) {
        free(buffer);
        SetLastError(ERROR_MORE_DATA);
        return FALSE;
    }

    if (count != 0) {
        page->entries = (RAYA_DIRECTORY_ENTRY *)calloc(count,
            sizeof(RAYA_DIRECTORY_ENTRY));
        if (page->entries == NULL) {
            free(buffer);
            SetLastError(ERROR_OUTOFMEMORY);
            return FALSE;
        }

        if (!copy_directory_page_entries(buffer, RAYA_DIRECTORY_PAGE_BYTES,
                page->entries, count)) {
            DWORD error = GetLastError();
            free(page->entries);
            page->entries = NULL;
            free(buffer);
            SetLastError(error);
            return FALSE;
        }
    }

    page->count = count;
    free(buffer);
    return TRUE;
}

void raya_free_directory_page(RAYA_DIRECTORY_PAGE *page)
{
    if (page == NULL)
        return;

    free(page->entries);
    page->entries = NULL;
    page->count = 0;
    page->done = FALSE;
}
