#define _WIN32_WINNT 0x0602
#define WIN32_LEAN_AND_MEAN
#include "root-registry.h"

#include "relative-path.h"

#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <winternl.h>

typedef NTSTATUS (NTAPI *RayaNtReadFileFn)(
    HANDLE, HANDLE, PIO_APC_ROUTINE, PVOID, PIO_STATUS_BLOCK,
    PVOID, ULONG, PLARGE_INTEGER, PULONG);

static RayaNtReadFileFn get_nt_read_file(void)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll
        ? (RayaNtReadFileFn)GetProcAddress(ntdll, "NtReadFile")
        : NULL;
}
static size_t bounded_root_id_length(const char *value)
{
    size_t length = 0;
    while (length < RAYA_MAX_ROOT_ID_BYTES + 1u && value[length] != '\0')
        ++length;
    return length;
}

static size_t bounded_path_length(const wchar_t *value)
{
    size_t length = 0;
    while (length < RAYA_MAX_REQUEST_PATH_CHARS + 1u &&
        value[length] != L'\0')
        ++length;
    return length;
}

static BOOL root_id_is_valid(const char *value, size_t length)
{
    if (value == NULL || length == 0 || length > RAYA_MAX_ROOT_ID_BYTES)
        return FALSE;

    for (size_t i = 0; i < length; ++i) {
        char ch = value[i];
        if (!((ch >= 'A' && ch <= 'Z') ||
              (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') ||
              ch == '_' || ch == '-'))
            return FALSE;
    }
    return TRUE;
}

static BOOL request_path_utf16_valid(const wchar_t *path, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        uint16_t code = (uint16_t)path[i];
        if (code == 0)
            return FALSE;
        if (code >= 0xd800u && code <= 0xdbffu) {
            if (i + 1 >= length)
                return FALSE;
            uint16_t low = (uint16_t)path[i + 1];
            if (low < 0xdc00u || low > 0xdfffu)
                return FALSE;
            ++i;
        } else if (code >= 0xdc00u && code <= 0xdfffu) {
            return FALSE;
        }
    }
    return TRUE;
}
static BOOL request_file_operation(uint16_t operation)
{
    return operation == RAYA_OPERATION_READ_FILE ||
        operation == RAYA_OPERATION_GET_FILE_INFO ||
        operation == RAYA_OPERATION_READ_FILE_CHUNK ||
        operation == RAYA_OPERATION_READ_FILE_TAIL;
}

static BOOL request_arguments_valid(const raya_request *request)
{
    if (request == NULL)
        return FALSE;

    switch (request->operation) {
    case RAYA_OPERATION_READ_FILE:
    case RAYA_OPERATION_LIST_DIRECTORY:
    case RAYA_OPERATION_GET_FILE_INFO:
        return request->offset == 0 && request->length == 0;
    case RAYA_OPERATION_READ_FILE_CHUNK:
        return request->length != 0 &&
            request->length <= RAYA_MAX_RESPONSE_BYTES &&
            request->offset <= UINT64_MAX - (uint64_t)request->length;
    case RAYA_OPERATION_READ_FILE_TAIL:
        return request->offset == 0 && request->length != 0 &&
            request->length <= RAYA_MAX_RESPONSE_BYTES;
    default:
        return FALSE;
    }
}
static BOOL configured_path_is_absolute(const wchar_t *path)
{
    size_t length;

    if (path == NULL)
        return FALSE;

    length = wcslen(path);
    if (length >= 3 &&
        ((path[0] >= L'A' && path[0] <= L'Z') ||
         (path[0] >= L'a' && path[0] <= L'z')) &&
        path[1] == L':' && (path[2] == L'\\' || path[2] == L'/'))
        return TRUE;

    return length >= 3 && path[0] == L'\\' && path[1] == L'\\';
}

static HANDLE open_configured_root(const wchar_t *path)
{
    HANDLE root;
    FILE_ATTRIBUTE_TAG_INFO tag_info;
    FILE_STANDARD_INFO standard_info;

    if (!configured_path_is_absolute(path)) {
        SetLastError(ERROR_BAD_PATHNAME);
        return INVALID_HANDLE_VALUE;
    }

    root = CreateFileW(path,
        FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES |
            READ_CONTROL | SYNCHRONIZE,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        NULL);
    if (root == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;

    if (GetFileType(root) != FILE_TYPE_DISK) {
        DWORD error = GetLastError();
        CloseHandle(root);
        SetLastError(error == ERROR_SUCCESS ? ERROR_INVALID_DATA : error);
        return INVALID_HANDLE_VALUE;
    }
    if (!GetFileInformationByHandleEx(root, FileAttributeTagInfo,
            &tag_info, sizeof(tag_info)) ||
        !GetFileInformationByHandleEx(root, FileStandardInfo,
            &standard_info, sizeof(standard_info))) {
        DWORD error = GetLastError();
        CloseHandle(root);
        SetLastError(error);
        return INVALID_HANDLE_VALUE;
    }
    if ((tag_info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (tag_info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        !standard_info.Directory || standard_info.DeletePending) {
        CloseHandle(root);
        SetLastError(ERROR_INVALID_DATA);
        return INVALID_HANDLE_VALUE;
    }

    return root;
}

static void release_registry_reference(RAYA_ROOT_REGISTRY *registry)
{
    AcquireSRWLockExclusive(&registry->lock);
    if (registry->active_contexts != 0)
        --registry->active_contexts;
    ReleaseSRWLockExclusive(&registry->lock);
}

BOOL raya_root_registry_init(
    RAYA_ROOT_REGISTRY *registry,
    const RAYA_ROOT_CONFIG *configs,
    size_t config_count)
{
    if (registry == NULL || configs == NULL || config_count == 0 ||
        config_count > RAYA_ROOT_REGISTRY_MAX_ROOTS) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    memset(registry, 0, sizeof(*registry));
    InitializeSRWLock(&registry->lock);

    for (size_t i = 0; i < config_count; ++i) {
        size_t id_length;
        HANDLE root;

        if (configs[i].root_id == NULL || configs[i].absolute_path == NULL) {
            SetLastError(ERROR_INVALID_PARAMETER);
            goto fail;
        }

        id_length = bounded_root_id_length(configs[i].root_id);
        if (!root_id_is_valid(configs[i].root_id, id_length)) {
            SetLastError(ERROR_INVALID_NAME);
            goto fail;
        }

        for (size_t j = 0; j < registry->count; ++j) {
            if (strcmp(registry->entries[j].root_id, configs[i].root_id) == 0) {
                SetLastError(ERROR_DUP_NAME);
                goto fail;
            }
        }

        root = open_configured_root(configs[i].absolute_path);
        if (root == INVALID_HANDLE_VALUE)
            goto fail;

        memcpy(registry->entries[registry->count].root_id,
            configs[i].root_id, id_length);
        registry->entries[registry->count].root_id[id_length] = '\0';
        registry->entries[registry->count].root_handle = root;
        ++registry->count;
    }

    registry->initialized = true;
    return TRUE;

fail:
    for (size_t i = 0; i < registry->count; ++i) {
        if (registry->entries[i].root_handle != NULL &&
            registry->entries[i].root_handle != INVALID_HANDLE_VALUE)
            CloseHandle(registry->entries[i].root_handle);
    }
    memset(registry->entries, 0, sizeof(registry->entries));
    registry->count = 0;
    registry->active_contexts = 0;
    registry->initialized = false;
    return FALSE;
}

BOOL raya_root_registry_encode_root_ids(
    RAYA_ROOT_REGISTRY *registry,
    uint8_t *payload,
    size_t payload_capacity,
    size_t *written)
{
    size_t required = 2u;
    size_t cursor = 2u;

    if (written != NULL)
        *written = 0;
    if (registry == NULL || payload == NULL || written == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    AcquireSRWLockShared(&registry->lock);
    if (!registry->initialized || registry->closing) {
        ReleaseSRWLockShared(&registry->lock);
        SetLastError(ERROR_SHUTDOWN_IN_PROGRESS);
        return FALSE;
    }

    for (size_t i = 0; i < registry->count; ++i) {
        size_t length = strlen(registry->entries[i].root_id);
        if (length == 0 || length > RAYA_MAX_ROOT_ID_BYTES ||
            required > SIZE_MAX - 1u - length) {
            ReleaseSRWLockShared(&registry->lock);
            SetLastError(ERROR_INVALID_DATA);
            return FALSE;
        }
        required += 1u + length;
    }

    if (payload_capacity < required) {
        ReleaseSRWLockShared(&registry->lock);
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    payload[0] = (uint8_t)registry->count;
    payload[1] = (uint8_t)(registry->count >> 8);
    for (size_t i = 0; i < registry->count; ++i) {
        size_t length = strlen(registry->entries[i].root_id);
        payload[cursor++] = (uint8_t)length;
        memcpy(payload + cursor, registry->entries[i].root_id, length);
        cursor += length;
    }

    ReleaseSRWLockShared(&registry->lock);
    *written = cursor;
    return TRUE;
}
BOOL raya_root_registry_close(RAYA_ROOT_REGISTRY *registry)
{
    BOOL success = TRUE;
    DWORD first_error = ERROR_SUCCESS;

    if (registry == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    AcquireSRWLockExclusive(&registry->lock);
    if (!registry->initialized) {
        ReleaseSRWLockExclusive(&registry->lock);
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (registry->active_contexts != 0) {
        ReleaseSRWLockExclusive(&registry->lock);
        SetLastError(ERROR_BUSY);
        return FALSE;
    }

    registry->closing = true;
    for (size_t i = 0; i < registry->count; ++i) {
        HANDLE root = registry->entries[i].root_handle;
        if (root != NULL && root != INVALID_HANDLE_VALUE) {
            if (!CloseHandle(root)) {
                if (first_error == ERROR_SUCCESS)
                    first_error = GetLastError();
                success = FALSE;
            } else {
                registry->entries[i].root_handle = NULL;
            }
        }
    }

    if (success) {
        memset(registry->entries, 0, sizeof(registry->entries));
        registry->count = 0;
        registry->initialized = false;
        registry->closing = false;
    }
    ReleaseSRWLockExclusive(&registry->lock);

    if (!success)
        SetLastError(first_error);
    return success;
}

static BOOL split_read_path(
    const wchar_t *path,
    wchar_t parent_path[RAYA_MAX_REQUEST_PATH_CHARS + 1u],
    wchar_t leaf_name[256])
{
    size_t length = bounded_path_length(path);
    size_t leaf_start = 0;

    if (length == 0 || length > RAYA_MAX_REQUEST_PATH_CHARS)
        return FALSE;

    for (size_t i = length; i > 0; --i) {
        if (path[i - 1] == L'\\') {
            leaf_start = i;
            break;
        }
    }

    size_t parent_length = leaf_start == 0 ? 0 : leaf_start - 1;
    size_t leaf_length = length - leaf_start;

    if (leaf_length == 0 || leaf_length > 255)
        return FALSE;

    if (parent_length != 0)
        memcpy(parent_path, path, parent_length * sizeof(wchar_t));
    parent_path[parent_length] = L'\0';
    memcpy(leaf_name, path + leaf_start, leaf_length * sizeof(wchar_t));
    leaf_name[leaf_length] = L'\0';
    return TRUE;
}

static uint32_t workspace_list_error_code(DWORD error)
{
    switch (error) {
    case ERROR_ACCESS_DENIED:
    case ERROR_PRIVILEGE_NOT_HELD:
        return RAYA_ERROR_ACCESS_DENIED;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
        return RAYA_ERROR_NOT_FOUND;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
    case ERROR_MORE_DATA:
        return RAYA_ERROR_RESOURCE_LIMIT;
    default:
        return RAYA_ERROR_INTERNAL;
    }
}

void raya_workspace_list_free(RAYA_WORKSPACE_LIST *result)
{
    if (result == NULL)
        return;
    for (size_t i = 0; i < RAYA_ROOT_REGISTRY_MAX_ROOTS; ++i)
        raya_free_directory_page(&result->roots[i].page);
    memset(result, 0, sizeof(*result));
}

BOOL raya_root_registry_list_roots(
    RAYA_ROOT_REGISTRY *registry,
    size_t max_entries_per_root,
    RAYA_WORKSPACE_LIST *result)
{
    char root_ids[RAYA_ROOT_REGISTRY_MAX_ROOTS][RAYA_MAX_ROOT_ID_BYTES + 1u];
    HANDLE root_handles[RAYA_ROOT_REGISTRY_MAX_ROOTS];
    size_t root_count;

    if (registry == NULL || result == NULL ||
        max_entries_per_root == 0 ||
        max_entries_per_root > RAYA_WORKSPACE_LIST_MAX_ENTRIES_PER_ROOT) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    memset(result, 0, sizeof(*result));
    memset(root_ids, 0, sizeof(root_ids));
    memset(root_handles, 0, sizeof(root_handles));

    AcquireSRWLockExclusive(&registry->lock);
    if (!registry->initialized || registry->closing ||
        registry->count == 0 ||
        registry->count > RAYA_ROOT_REGISTRY_MAX_ROOTS) {
        ReleaseSRWLockExclusive(&registry->lock);
        SetLastError(ERROR_SHUTDOWN_IN_PROGRESS);
        return FALSE;
    }

    root_count = registry->count;
    for (size_t i = 0; i < root_count; ++i) {
        memcpy(root_ids[i], registry->entries[i].root_id,
            sizeof(root_ids[i]));
        root_handles[i] = registry->entries[i].root_handle;
    }
    ++registry->active_contexts;
    ReleaseSRWLockExclusive(&registry->lock);

    result->count = root_count;
    for (size_t i = 0; i < root_count; ++i) {
        RAYA_WORKSPACE_ROOT_LIST *root_result = &result->roots[i];
        HANDLE root_view = NULL;
        RAYA_DIRECTORY_PAGE page = { 0 };
        RAYA_DIRECTORY_PAGE next_page = { 0 };
        BOOL opened;
        BOOL enumerated;

        memcpy(root_result->root_id, root_ids[i], sizeof(root_result->root_id));
        opened = raya_reopen_pinned_directory(root_handles[i], &root_view);
        if (!opened) {
            root_result->error_code = workspace_list_error_code(GetLastError());
            continue;
        }

        enumerated = raya_enumerate_directory_page(root_view,
            1024u, TRUE, &page);
        if (!enumerated) {
            root_result->error_code = workspace_list_error_code(GetLastError());
            raya_free_directory_page(&page);
            CloseHandle(root_view);
            continue;
        }

        root_result->truncated = page.count > max_entries_per_root;
        if (!root_result->truncated && !page.done) {
            if (!raya_enumerate_directory_page(root_view, 1024u,
                    FALSE, &next_page)) {
                root_result->error_code =
                    workspace_list_error_code(GetLastError());
                raya_free_directory_page(&page);
                raya_free_directory_page(&next_page);
                CloseHandle(root_view);
                continue;
            }
            root_result->truncated = next_page.count != 0 || !next_page.done;
            raya_free_directory_page(&next_page);
        }
        CloseHandle(root_view);

        if (page.count > max_entries_per_root)
            page.count = max_entries_per_root;
        page.done = !root_result->truncated;
        root_result->page = page;
    }

    release_registry_reference(registry);
    return TRUE;
}
BOOL raya_request_context_begin(
    RAYA_ROOT_REGISTRY *registry,
    const raya_request *request,
    ULONGLONG maximum_file_size,
    RAYA_REQUEST_CONTEXT *context)
{
    HANDLE root = NULL;
    size_t root_length;
    size_t path_length;
    size_t components = 0;
    bool found = false;

    if (registry == NULL || request == NULL || context == NULL ||
        context->registry != NULL || context->active ||
        !registry->initialized) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    root_length = bounded_root_id_length(request->root_id);
    path_length = bounded_path_length(request->relative_path);
    if (!root_id_is_valid(request->root_id, root_length) ||
        path_length > RAYA_MAX_REQUEST_PATH_CHARS) {
        SetLastError(ERROR_INVALID_NAME);
        return FALSE;
    }

    if (path_length != 0 && !request_path_utf16_valid(request->relative_path, path_length)) {
        SetLastError(ERROR_INVALID_NAME);
        return FALSE;
    }

    if (!request_arguments_valid(request)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (request->operation == RAYA_OPERATION_LIST_DIRECTORY) {
        if (path_length != 0 &&
            (!raya_validate_relative_path(request->relative_path, &components) ||
             components == 0)) {
            SetLastError(ERROR_INVALID_NAME);
            return FALSE;
        }
    } else if (request_file_operation(request->operation)) {
        if (path_length == 0 ||
            !raya_validate_relative_path(request->relative_path, &components) ||
            components == 0) {
            SetLastError(ERROR_INVALID_NAME);
            return FALSE;
        }
    } else {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }

    AcquireSRWLockExclusive(&registry->lock);
    if (!registry->initialized || registry->closing) {
        ReleaseSRWLockExclusive(&registry->lock);
        SetLastError(ERROR_SHUTDOWN_IN_PROGRESS);
        return FALSE;
    }

    for (size_t i = 0; i < registry->count; ++i) {
        if (strcmp(registry->entries[i].root_id, request->root_id) == 0) {
            root = registry->entries[i].root_handle;
            found = true;
            break;
        }
    }

    if (!found || root == NULL || root == INVALID_HANDLE_VALUE) {
        ReleaseSRWLockExclusive(&registry->lock);
        SetLastError(ERROR_NOT_FOUND);
        return FALSE;
    }

    ++registry->active_contexts;
    ReleaseSRWLockExclusive(&registry->lock);

    memset(context, 0, sizeof(*context));
    context->registry = registry;
    context->root_handle = root;
    context->operation = request->operation;
    context->offset = request->offset;
    context->length = request->length;
    InitializeSRWLock(&context->io_lock);

    if (request->operation == RAYA_OPERATION_LIST_DIRECTORY) {
        if (path_length == 0) {
            HANDLE root_view = NULL;
            if (!raya_reopen_pinned_directory(root, &root_view))
                goto fail;
            context->directory_chain.handles =
                (HANDLE *)calloc(1, sizeof(HANDLE));
            if (context->directory_chain.handles == NULL) {
                DWORD error = ERROR_OUTOFMEMORY;
                CloseHandle(root_view);
                SetLastError(error);
                goto fail;
            }
            context->directory_chain.handles[0] = root_view;
            context->directory_chain.count = 1;
        } else if (!raya_open_directory_chain(root, request->relative_path,
                &context->directory_chain)) {
            goto fail;
        }

        context->directory_handle =
            context->directory_chain.handles[context->directory_chain.count - 1];
    } else {
        wchar_t parent_path[RAYA_MAX_REQUEST_PATH_CHARS + 1u] = { 0 };
        wchar_t leaf_name[256] = { 0 };
        HANDLE parent;

        if (!split_read_path(request->relative_path, parent_path, leaf_name))
            goto fail;

        if (parent_path[0] != L'\0' &&
            !raya_open_directory_chain(root, parent_path,
                &context->directory_chain))
            goto fail;

        parent = context->directory_chain.count == 0
            ? root
            : context->directory_chain.handles[context->directory_chain.count - 1];

        if (!raya_open_single_link_regular_file(parent, leaf_name,
                maximum_file_size, &context->file_handle, &context->file_size))
            goto fail;

        if (request->operation == RAYA_OPERATION_READ_FILE &&
            context->file_size > RAYA_MAX_RESPONSE_BYTES) {
            SetLastError(ERROR_FILE_TOO_LARGE);
            goto fail;
        }
        if (request->operation == RAYA_OPERATION_READ_FILE_CHUNK &&
            (context->offset > context->file_size ||
             (ULONGLONG)context->length > context->file_size - context->offset)) {
            SetLastError(ERROR_HANDLE_EOF);
            goto fail;
        }
        context->directory_handle = parent;
    }

    context->active = true;
    return TRUE;

fail:
    if (context->file_handle != NULL &&
        context->file_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(context->file_handle);
        context->file_handle = NULL;
    }
    raya_close_handle_chain(&context->directory_chain);
    release_registry_reference(registry);
    memset(context, 0, sizeof(*context));
    return FALSE;
}

BOOL raya_request_context_read(
    RAYA_REQUEST_CONTEXT *context,
    void *buffer,
    size_t buffer_capacity,
    DWORD *bytes_read)
{
    ULONGLONG offset = 0;
    DWORD requested = 0;
    LARGE_INTEGER position;
    DWORD actual = 0;

    if (bytes_read != NULL)
        *bytes_read = 0;
    if (context == NULL || !context->active ||
        context->file_handle == NULL || context->file_handle == INVALID_HANDLE_VALUE ||
        bytes_read == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    switch (context->operation) {
    case RAYA_OPERATION_READ_FILE:
        offset = 0;
        if (context->file_size > RAYA_MAX_RESPONSE_BYTES) {
            SetLastError(ERROR_FILE_TOO_LARGE);
            return FALSE;
        }
        requested = (DWORD)context->file_size;
        break;
    case RAYA_OPERATION_READ_FILE_CHUNK:
        offset = context->offset;
        requested = context->length;
        if (offset > context->file_size ||
            (ULONGLONG)requested > context->file_size - offset) {
            SetLastError(ERROR_HANDLE_EOF);
            return FALSE;
        }
        break;
    case RAYA_OPERATION_READ_FILE_TAIL:
        requested = context->length;
        if ((ULONGLONG)requested > context->file_size)
            requested = (DWORD)context->file_size;
        offset = context->file_size - requested;
        break;
    default:
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }

    if (requested > RAYA_MAX_RESPONSE_BYTES ||
        (size_t)requested > buffer_capacity ||
        (requested != 0 && buffer == NULL)) {
        SetLastError(requested > buffer_capacity
            ? ERROR_INSUFFICIENT_BUFFER
            : ERROR_INVALID_PARAMETER);
        return FALSE;
    }


    AcquireSRWLockExclusive(&context->io_lock);
    if (requested != 0) {
        RayaNtReadFileFn nt_read = get_nt_read_file();
        IO_STATUS_BLOCK io;
        NTSTATUS status;
        position.QuadPart = (LONGLONG)offset;
        if (nt_read == NULL) {
            ReleaseSRWLockExclusive(&context->io_lock);
            SetLastError(ERROR_PROC_NOT_FOUND);
            return FALSE;
        }

        status = nt_read(context->file_handle, NULL, NULL, NULL, &io,
            buffer, requested, &position, NULL);
        if (status < 0) {
            ReleaseSRWLockExclusive(&context->io_lock);
            SetLastError(ERROR_READ_FAULT);
            return FALSE;
        }
        actual = (DWORD)io.Information;
        if (actual != requested) {
            ReleaseSRWLockExclusive(&context->io_lock);
            SetLastError(ERROR_HANDLE_EOF);
            return FALSE;
        }
    }

    ReleaseSRWLockExclusive(&context->io_lock);
    *bytes_read = actual;
    return TRUE;
}

BOOL raya_request_context_get_file_info(
    RAYA_REQUEST_CONTEXT *context,
    RAYA_FILE_INFO *info)
{
    FILE_STANDARD_INFO standard_info;
    FILE_BASIC_INFO basic_info;
    FILE_ATTRIBUTE_TAG_INFO tag_info;
    DWORD error = ERROR_SUCCESS;

    if (context == NULL || !context->active ||
        context->operation != RAYA_OPERATION_GET_FILE_INFO ||
        context->file_handle == NULL ||
        context->file_handle == INVALID_HANDLE_VALUE || info == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    AcquireSRWLockExclusive(&context->io_lock);
    if (!GetFileInformationByHandleEx(context->file_handle,
            FileStandardInfo, &standard_info, sizeof(standard_info)) ||
        !GetFileInformationByHandleEx(context->file_handle,
            FileBasicInfo, &basic_info, sizeof(basic_info)) ||
        !GetFileInformationByHandleEx(context->file_handle,
            FileAttributeTagInfo, &tag_info, sizeof(tag_info))) {
        error = GetLastError();
        ReleaseSRWLockExclusive(&context->io_lock);
        SetLastError(error);
        return FALSE;
    }

    memset(info, 0, sizeof(*info));
    info->size = (ULONGLONG)standard_info.EndOfFile.QuadPart;
    info->link_count = standard_info.NumberOfLinks;
    info->attributes = tag_info.FileAttributes;
    info->creation_time.dwLowDateTime = (DWORD)basic_info.CreationTime.QuadPart;
    info->creation_time.dwHighDateTime = (DWORD)((ULONGLONG)basic_info.CreationTime.QuadPart >> 32);
    info->last_access_time.dwLowDateTime = (DWORD)basic_info.LastAccessTime.QuadPart;
    info->last_access_time.dwHighDateTime = (DWORD)((ULONGLONG)basic_info.LastAccessTime.QuadPart >> 32);
    info->last_write_time.dwLowDateTime = (DWORD)basic_info.LastWriteTime.QuadPart;
    info->last_write_time.dwHighDateTime = (DWORD)((ULONGLONG)basic_info.LastWriteTime.QuadPart >> 32);
    info->directory = standard_info.Directory != FALSE;
    info->delete_pending = standard_info.DeletePending != FALSE;
    info->reparse_point =
        (tag_info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

    ReleaseSRWLockExclusive(&context->io_lock);
    return TRUE;
}
BOOL raya_request_context_close(RAYA_REQUEST_CONTEXT *context)
{
    RAYA_ROOT_REGISTRY *registry;

    if (context == NULL || !context->active || context->registry == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    registry = context->registry;
    AcquireSRWLockExclusive(&context->io_lock);
    if (context->file_handle != NULL &&
        context->file_handle != INVALID_HANDLE_VALUE) {
        if (!CloseHandle(context->file_handle)) {
            DWORD error = GetLastError();
            ReleaseSRWLockExclusive(&context->io_lock);
            SetLastError(error);
            return FALSE; /* Keep context and registry reference for a safe retry. */
        }
    }
    context->file_handle = NULL;
    ReleaseSRWLockExclusive(&context->io_lock);

    raya_close_handle_chain(&context->directory_chain);
    context->file_handle = NULL;
    context->directory_handle = NULL;
    context->root_handle = NULL;
    context->active = false;

    release_registry_reference(registry);
    memset(context, 0, sizeof(*context));
    return TRUE;
}
