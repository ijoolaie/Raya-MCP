#define _WIN32_WINNT 0x0602
#define WIN32_LEAN_AND_MEAN
#include "root-registry.h"

#include "relative-path.h"

#include <string.h>
#include <wchar.h>

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

    if (request->operation == RAYA_OPERATION_READ_FILE) {
        if (path_length == 0 ||
            !raya_validate_relative_path(request->relative_path, &components) ||
            components == 0) {
            SetLastError(ERROR_INVALID_NAME);
            return FALSE;
        }
    } else if (request->operation == RAYA_OPERATION_LIST_DIRECTORY) {
        if (path_length != 0 &&
            (!raya_validate_relative_path(request->relative_path, &components) ||
             components == 0)) {
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

    if (request->operation == RAYA_OPERATION_LIST_DIRECTORY) {
        if (path_length != 0 &&
            !raya_open_directory_chain(root, request->relative_path,
                &context->directory_chain))
            goto fail;

        context->directory_handle = context->directory_chain.count == 0
            ? root
            : context->directory_chain.handles[context->directory_chain.count - 1];
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

BOOL raya_request_context_close(RAYA_REQUEST_CONTEXT *context)
{
    RAYA_ROOT_REGISTRY *registry;

    if (context == NULL || !context->active || context->registry == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    registry = context->registry;
    if (context->file_handle != NULL &&
        context->file_handle != INVALID_HANDLE_VALUE) {
        if (!CloseHandle(context->file_handle))
            return FALSE; /* Keep context and registry reference for a safe retry. */
    }

    raya_close_handle_chain(&context->directory_chain);
    context->file_handle = NULL;
    context->directory_handle = NULL;
    context->root_handle = NULL;
    context->active = false;

    release_registry_reference(registry);
    memset(context, 0, sizeof(*context));
    return TRUE;
}
