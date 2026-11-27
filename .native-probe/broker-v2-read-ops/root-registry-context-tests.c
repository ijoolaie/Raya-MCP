#define _WIN32_WINNT 0x0602
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "root-registry.h"

static unsigned passed;
static unsigned failed;

#define CHECK(label, expression) do { \
    if (expression) { printf("PASS %s\n", label); ++passed; } \
    else { printf("FAIL %s (error=%lu)\n", label, (unsigned long)GetLastError()); ++failed; } \
} while (0)

static BOOL write_text_file(const wchar_t *path, const char *contents)
{
    HANDLE file;
    DWORD written = 0;
    DWORD length = (DWORD)strlen(contents);

    file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;

    BOOL success = WriteFile(file, contents, length, &written, NULL) &&
        written == length;
    CloseHandle(file);
    return success;
}

static BOOL read_context_file(RAYA_REQUEST_CONTEXT *context,
    const char *expected)
{
    char buffer[64] = { 0 };
    DWORD read_bytes = 0;
    DWORD expected_length = (DWORD)strlen(expected);

    return raya_request_context_read(context, buffer, sizeof(buffer) - 1,
            &read_bytes) &&
        read_bytes == expected_length &&
        memcmp(buffer, expected, expected_length) == 0;
}

static BOOL create_fixtures(
    wchar_t base[MAX_PATH],
    wchar_t root_one[MAX_PATH],
    wchar_t root_two[MAX_PATH])
{
    wchar_t temp[MAX_PATH];
    wchar_t path[MAX_PATH];
    DWORD length = GetTempPathW(MAX_PATH, temp);
    if (length == 0 || length >= MAX_PATH)
        return FALSE;

    (void)swprintf_s(base, MAX_PATH, L"%sRayaRootRegistry-%lu-%llu",
        temp, (unsigned long)GetCurrentProcessId(),
        (unsigned long long)GetTickCount64());
    (void)swprintf_s(root_one, MAX_PATH, L"%s\\root-one", base);
    (void)swprintf_s(root_two, MAX_PATH, L"%s\\root-two", base);

    if (!CreateDirectoryW(base, NULL) ||
        !CreateDirectoryW(root_one, NULL) ||
        !CreateDirectoryW(root_two, NULL))
        return FALSE;

    (void)swprintf_s(path, MAX_PATH, L"%s\\docs", root_one);
    if (!CreateDirectoryW(path, NULL))
        return FALSE;
    (void)swprintf_s(path, MAX_PATH, L"%s\\docs", root_two);
    if (!CreateDirectoryW(path, NULL))
        return FALSE;

    (void)swprintf_s(path, MAX_PATH, L"%s\\docs\\file.txt", root_one);
    if (!write_text_file(path, "root-one"))
        return FALSE;
    (void)swprintf_s(path, MAX_PATH, L"%s\\docs\\file.txt", root_two);
    if (!write_text_file(path, "root-two"))
        return FALSE;
    (void)swprintf_s(path, MAX_PATH, L"%s\\root.txt", root_one);
    if (!write_text_file(path, "root-level"))
        return FALSE;

    return TRUE;
}

static void cleanup_fixtures(
    const wchar_t *base,
    const wchar_t *root_one,
    const wchar_t *root_two)
{
    wchar_t path[MAX_PATH];
    wchar_t moved[MAX_PATH];

    (void)swprintf_s(moved, MAX_PATH, L"%s-pinned", root_one);
    if (GetFileAttributesW(root_one) == INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW(moved) != INVALID_FILE_ATTRIBUTES)
        (void)MoveFileExW(moved, root_one, MOVEFILE_WRITE_THROUGH);

    (void)swprintf_s(path, MAX_PATH, L"%s\\docs\\file.txt", root_one);
    (void)DeleteFileW(path);
    (void)swprintf_s(path, MAX_PATH, L"%s\\root.txt", root_one);
    (void)DeleteFileW(path);
    (void)swprintf_s(path, MAX_PATH, L"%s\\docs", root_one);
    (void)RemoveDirectoryW(path);
    (void)RemoveDirectoryW(root_one);

    (void)swprintf_s(path, MAX_PATH, L"%s\\docs\\file.txt", root_two);
    (void)DeleteFileW(path);
    (void)swprintf_s(path, MAX_PATH, L"%s\\docs", root_two);
    (void)RemoveDirectoryW(path);
    (void)RemoveDirectoryW(root_two);
    (void)RemoveDirectoryW(base);
}

static void test_read_and_root_mapping(
    RAYA_ROOT_REGISTRY *registry)
{
    raya_request request;
    RAYA_REQUEST_CONTEXT context = { 0 };
    HANDLE old_file;
    HANDLE old_parent;

    memset(&request, 0, sizeof(request));
    request.operation = RAYA_OPERATION_READ_FILE;
    strcpy_s(request.root_id, sizeof(request.root_id), "fixture");
    wcscpy_s(request.relative_path,
        RAYA_MAX_REQUEST_PATH_CHARS + 1u, L"docs\\file.txt");

    CHECK("begin READ_FILE request for configured root",
        raya_request_context_begin(registry, &request, 1024, &context));
    CHECK("context pins configured root and final file",
        context.active && context.root_handle != NULL &&
        context.file_handle != NULL && context.file_size == 8 &&
        context.directory_chain.count == 1);
    CHECK("READ_FILE returns content from selected root",
        read_context_file(&context, "root-one"));

    old_file = context.file_handle;
    old_parent = context.directory_chain.handles[0];
    CHECK("registry refuses shutdown with active request context",
        !raya_root_registry_close(registry) && GetLastError() == ERROR_BUSY &&
        registry->active_contexts == 1);
    CHECK("request context closes all owned handles",
        raya_request_context_close(&context) && registry->active_contexts == 0);
    CHECK("closed request file HANDLE is invalid",
        GetFileType(old_file) == FILE_TYPE_UNKNOWN);
    CHECK("closed parent-chain HANDLE is invalid",
        GetFileType(old_parent) == FILE_TYPE_UNKNOWN);
    CHECK("request context cannot be closed twice",
        !raya_request_context_close(&context));

    memset(&request, 0, sizeof(request));
    request.operation = RAYA_OPERATION_READ_FILE;
    strcpy_s(request.root_id, sizeof(request.root_id), "secondary");
    wcscpy_s(request.relative_path,
        RAYA_MAX_REQUEST_PATH_CHARS + 1u, L"docs\\file.txt");
    memset(&context, 0, sizeof(context));
    CHECK("begin READ_FILE request for second configured root",
        raya_request_context_begin(registry, &request, 1024, &context));
    CHECK("rootId maps to the second configured root",
        read_context_file(&context, "root-two"));
    CHECK("close second-root request", raya_request_context_close(&context));

    memset(&request, 0, sizeof(request));
    request.operation = RAYA_OPERATION_READ_FILE;
    strcpy_s(request.root_id, sizeof(request.root_id), "fixture");
    wcscpy_s(request.relative_path,
        RAYA_MAX_REQUEST_PATH_CHARS + 1u, L"root.txt");
    CHECK("READ_FILE handles a root-level leaf with no parent path",
        raya_request_context_begin(registry, &request, 1024, &context) &&
        read_context_file(&context, "root-level"));
    CHECK("close root-level file request", raya_request_context_close(&context));
}

static void test_list_directory_context(
    RAYA_ROOT_REGISTRY *registry)
{
    raya_request request;
    RAYA_REQUEST_CONTEXT context = { 0 };
    RAYA_DIRECTORY_PAGE page = { 0 };
    BOOL found = FALSE;

    memset(&request, 0, sizeof(request));
    request.operation = RAYA_OPERATION_LIST_DIRECTORY;
    strcpy_s(request.root_id, sizeof(request.root_id), "fixture");

    CHECK("begin root LIST_DIRECTORY request",
        raya_request_context_begin(registry, &request, 0, &context));
    CHECK("empty LIST_DIRECTORY path binds to configured root HANDLE",
        context.directory_handle == context.root_handle &&
        context.directory_chain.count == 0);
    CHECK("enumerate configured root through request context",
        raya_enumerate_directory_page(context.directory_handle, 32, TRUE, &page));
    for (size_t i = 0; i < page.count; ++i) {
        if (wcscmp(page.entries[i].name, L"docs") == 0 &&
            (page.entries[i].attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            found = TRUE;
    }
    CHECK("root enumeration returns configured docs directory", found);
    raya_free_directory_page(&page);
    CHECK("close root list request", raya_request_context_close(&context));

    memset(&request, 0, sizeof(request));
    request.operation = RAYA_OPERATION_LIST_DIRECTORY;
    strcpy_s(request.root_id, sizeof(request.root_id), "fixture");
    wcscpy_s(request.relative_path,
        RAYA_MAX_REQUEST_PATH_CHARS + 1u, L"docs");
    memset(&context, 0, sizeof(context));
    CHECK("begin nested LIST_DIRECTORY request",
        raya_request_context_begin(registry, &request, 0, &context));
    CHECK("nested directory is pinned by one relative HANDLE",
        context.directory_handle != context.root_handle &&
        context.directory_chain.count == 1);
    memset(&page, 0, sizeof(page));
    CHECK("enumerate nested directory through context",
        raya_enumerate_directory_page(context.directory_handle, 32, TRUE, &page));
    found = FALSE;
    for (size_t i = 0; i < page.count; ++i) {
        if (wcscmp(page.entries[i].name, L"file.txt") == 0)
            found = TRUE;
    }
    CHECK("nested listing returns file leaf", found);
    raya_free_directory_page(&page);
    CHECK("close nested list request", raya_request_context_close(&context));
}


static void test_chunk_tail_and_file_info(RAYA_ROOT_REGISTRY *registry)
{
    raya_request request;
    RAYA_REQUEST_CONTEXT context = { 0 };
    char buffer[64] = { 0 };
    DWORD bytes = 0;
    RAYA_FILE_INFO info;

    memset(&request, 0, sizeof(request));
    request.operation = RAYA_OPERATION_READ_FILE_CHUNK;
    strcpy_s(request.root_id, sizeof(request.root_id), "fixture");
    wcscpy_s(request.relative_path,
        RAYA_MAX_REQUEST_PATH_CHARS + 1u, L"docs\\file.txt");
    request.offset = 2;
    request.length = 3;
    CHECK("begin READ_FILE_CHUNK request",
        raya_request_context_begin(registry, &request, 1024, &context));
    CHECK("READ_FILE_CHUNK returns exact requested range",
        raya_request_context_read(&context, buffer, sizeof(buffer), &bytes) &&
        bytes == 3 && memcmp(buffer, "ot-", 3) == 0);
    CHECK("close READ_FILE_CHUNK context",
        raya_request_context_close(&context));

    memset(&request, 0, sizeof(request));
    request.operation = RAYA_OPERATION_READ_FILE_TAIL;
    strcpy_s(request.root_id, sizeof(request.root_id), "fixture");
    wcscpy_s(request.relative_path,
        RAYA_MAX_REQUEST_PATH_CHARS + 1u, L"docs\\file.txt");
    request.length = 4;
    memset(&context, 0, sizeof(context));
    memset(buffer, 0, sizeof(buffer));
    CHECK("begin READ_FILE_TAIL request",
        raya_request_context_begin(registry, &request, 1024, &context));
    CHECK("READ_FILE_TAIL returns last four bytes",
        raya_request_context_read(&context, buffer, sizeof(buffer), &bytes) &&
        bytes == 4 && memcmp(buffer, "-one", 4) == 0);
    CHECK("close READ_FILE_TAIL context",
        raya_request_context_close(&context));

    request.length = 20;
    memset(&context, 0, sizeof(context));
    memset(buffer, 0, sizeof(buffer));
    CHECK("begin READ_FILE_TAIL larger than file",
        raya_request_context_begin(registry, &request, 1024, &context));
    CHECK("READ_FILE_TAIL clamps to complete small file",
        raya_request_context_read(&context, buffer, sizeof(buffer), &bytes) &&
        bytes == 8 && memcmp(buffer, "root-one", 8) == 0);
    CHECK("close large-tail context",
        raya_request_context_close(&context));

    memset(&request, 0, sizeof(request));
    request.operation = RAYA_OPERATION_GET_FILE_INFO;
    strcpy_s(request.root_id, sizeof(request.root_id), "fixture");
    wcscpy_s(request.relative_path,
        RAYA_MAX_REQUEST_PATH_CHARS + 1u, L"docs\\file.txt");
    memset(&context, 0, sizeof(context));
    CHECK("begin GET_FILE_INFO request",
        raya_request_context_begin(registry, &request, 1024, &context));
    CHECK("GET_FILE_INFO reads metadata from same validated HANDLE",
        raya_request_context_get_file_info(&context, &info) &&
        info.size == 8 && info.link_count == 1 &&
        !info.directory && !info.reparse_point && !info.delete_pending);
    CHECK("GET_FILE_INFO does not expose file content read",
        !raya_request_context_read(&context, buffer, sizeof(buffer), &bytes));
    CHECK("close GET_FILE_INFO context",
        raya_request_context_close(&context));

    memset(&request, 0, sizeof(request));
    request.operation = RAYA_OPERATION_READ_FILE_CHUNK;
    strcpy_s(request.root_id, sizeof(request.root_id), "fixture");
    wcscpy_s(request.relative_path,
        RAYA_MAX_REQUEST_PATH_CHARS + 1u, L"docs\\file.txt");
    request.offset = 7;
    request.length = 2;
    memset(&context, 0, sizeof(context));
    CHECK("out-of-range READ_FILE_CHUNK is rejected before context is returned",
        !raya_request_context_begin(registry, &request, 1024, &context) &&
        GetLastError() == ERROR_HANDLE_EOF && registry->active_contexts == 0);

    memset(&request, 0, sizeof(request));
    request.operation = RAYA_OPERATION_READ_FILE;
    strcpy_s(request.root_id, sizeof(request.root_id), "fixture");
    wcscpy_s(request.relative_path,
        RAYA_MAX_REQUEST_PATH_CHARS + 1u, L"docs\\file.txt");
    memset(&context, 0, sizeof(context));
    CHECK("begin READ_FILE for small-buffer test",
        raya_request_context_begin(registry, &request, 1024, &context));
    CHECK("READ_FILE rejects insufficient output buffer",
        !raya_request_context_read(&context, buffer, 4, &bytes) &&
        GetLastError() == ERROR_INSUFFICIENT_BUFFER);
    CHECK("close small-buffer context",
        raya_request_context_close(&context));
}


static void test_invalid_requests_and_registry_close(
    RAYA_ROOT_REGISTRY *registry,
    const wchar_t *root_one)
{
    raya_request request;
    RAYA_REQUEST_CONTEXT context = { 0 };
    wchar_t renamed[MAX_PATH];
    BOOL rename_while_open;
    HANDLE saved_root = registry->entries[0].root_handle;

    memset(&request, 0, sizeof(request));
    request.operation = RAYA_OPERATION_LIST_DIRECTORY;
    strcpy_s(request.root_id, sizeof(request.root_id), "unknown");
    CHECK("unknown rootId is rejected",
        !raya_request_context_begin(registry, &request, 1024, &context) &&
        GetLastError() == ERROR_NOT_FOUND);

    strcpy_s(request.root_id, sizeof(request.root_id), "fixture");
    wcscpy_s(request.relative_path,
        RAYA_MAX_REQUEST_PATH_CHARS + 1u, L"..\\outside");
    CHECK("request rejects dot-dot path before root access",
        !raya_request_context_begin(registry, &request, 1024, &context));

    wcscpy_s(request.relative_path,
        RAYA_MAX_REQUEST_PATH_CHARS + 1u, L"C:\\absolute.txt");
    CHECK("request rejects caller absolute path",
        !raya_request_context_begin(registry, &request, 1024, &context));

    request.operation = RAYA_OPERATION_READ_FILE;
    request.relative_path[0] = L'\0';
    CHECK("READ_FILE rejects empty relative path",
        !raya_request_context_begin(registry, &request, 1024, &context));

    request.operation = RAYA_OPERATION_READ_FILE_CHUNK;
    wcscpy_s(request.relative_path,
        RAYA_MAX_REQUEST_PATH_CHARS + 1u, L"docs\\file.txt");
    request.offset = 0;
    request.length = 0;
    CHECK("request context rejects zero-length READ_FILE_CHUNK",
        !raya_request_context_begin(registry, &request, 1024, &context));

    request.operation = RAYA_OPERATION_READ_FILE_TAIL;
    request.offset = 1;
    request.length = 1;
    CHECK("request context rejects nonzero READ_FILE_TAIL offset",
        !raya_request_context_begin(registry, &request, 1024, &context));

    request.operation = 99;
    CHECK("request context rejects unsupported operation",
        !raya_request_context_begin(registry, &request, 1024, &context));
    CHECK("invalid requests do not leak active registry references",
        registry->active_contexts == 0);

    request.operation = RAYA_OPERATION_READ_FILE;
    wcscpy_s(request.relative_path,
        RAYA_MAX_REQUEST_PATH_CHARS + 1u, L"docs\\missing.txt");
    CHECK("missing file releases request-scoped root reference",
        !raya_request_context_begin(registry, &request, 1024, &context) &&
        registry->active_contexts == 0);

    wcscpy_s(request.relative_path,
        RAYA_MAX_REQUEST_PATH_CHARS + 1u, L"docs\\file.txt");
    CHECK("file size cap is enforced before returning context",
        !raya_request_context_begin(registry, &request, 4, &context) &&
        registry->active_contexts == 0);

    wcscpy_s(request.relative_path,
        RAYA_MAX_REQUEST_PATH_CHARS + 1u, L"docs");
    CHECK("READ_FILE rejects directory as final component",
        !raya_request_context_begin(registry, &request, 1024, &context) &&
        registry->active_contexts == 0);

    request.operation = RAYA_OPERATION_LIST_DIRECTORY;
    request.relative_path[0] = (wchar_t)0xd800u;
    request.relative_path[1] = L'\0';
    CHECK("request context rejects unpaired UTF-16 surrogate",
        !raya_request_context_begin(registry, &request, 1024, &context) &&
        registry->active_contexts == 0);

    (void)swprintf_s(renamed, MAX_PATH, L"%s-pinned", root_one);
    rename_while_open = MoveFileExW(root_one, renamed, MOVEFILE_WRITE_THROUGH);
    CHECK("configured root namespace rename blocked while registry pins HANDLE",
        !rename_while_open &&
        (GetLastError() == ERROR_SHARING_VIOLATION ||
         GetLastError() == ERROR_ACCESS_DENIED));

    CHECK("close root registry after requests drained",
        raya_root_registry_close(registry));
    CHECK("registry shutdown closes pinned root HANDLE",
        GetFileType(saved_root) == FILE_TYPE_UNKNOWN);
    CHECK("root directory rename succeeds after registry close",
        MoveFileExW(root_one, renamed, MOVEFILE_WRITE_THROUGH));
    CHECK("restore root directory for cleanup",
        MoveFileExW(renamed, root_one, MOVEFILE_WRITE_THROUGH));

    memset(&context, 0, sizeof(context));
    memset(&request, 0, sizeof(request));
    request.operation = RAYA_OPERATION_LIST_DIRECTORY;
    strcpy_s(request.root_id, sizeof(request.root_id), "fixture");
    CHECK("closed registry refuses new request contexts",
        !raya_request_context_begin(registry, &request, 1024, &context));
}

static void test_invalid_registry_configs(const wchar_t *root_one)
{
    RAYA_ROOT_REGISTRY registry = { 0 };
    RAYA_ROOT_CONFIG duplicate[2] = {
        { "same", root_one }, { "same", root_one }
    };
    RAYA_ROOT_CONFIG invalid_id[1] = { { "bad/id", root_one } };
    RAYA_ROOT_CONFIG relative_path[1] = { { "relative", L"relative-root" } };

    CHECK("registry rejects duplicate rootIds atomically",
        !raya_root_registry_init(&registry, duplicate, 2) &&
        registry.count == 0 && !registry.initialized);
    CHECK("registry rejects invalid configured rootId",
        !raya_root_registry_init(&registry, invalid_id, 1));
    CHECK("registry rejects non-absolute configured root path",
        !raya_root_registry_init(&registry, relative_path, 1));
}

static void test_reparse_root_rejection(const wchar_t *root_one)
{
    wchar_t temp[MAX_PATH];
    wchar_t link_path[MAX_PATH];
    DWORD length = GetTempPathW(MAX_PATH, temp);
    RAYA_ROOT_REGISTRY registry = { 0 };
    RAYA_ROOT_CONFIG config;

    if (length == 0 || length >= MAX_PATH)
        return;

    (void)swprintf_s(link_path, MAX_PATH, L"%sRayaRootRegistryLink-%lu",
        temp, (unsigned long)GetCurrentProcessId());
    if (!CreateSymbolicLinkW(link_path, root_one,
            SYMBOLIC_LINK_FLAG_DIRECTORY |
            SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
        printf("SKIP configured-root symbolic-link fixture unavailable (%lu)\n",
            (unsigned long)GetLastError());
        return;
    }

    config.root_id = "link";
    config.absolute_path = link_path;
    CHECK("registry rejects configured root that is a reparse point",
        !raya_root_registry_init(&registry, &config, 1) &&
        !registry.initialized && registry.count == 0);
    RemoveDirectoryW(link_path);
}

int main(void)
{
    wchar_t base[MAX_PATH] = { 0 };
    wchar_t root_one[MAX_PATH] = { 0 };
    wchar_t root_two[MAX_PATH] = { 0 };
    RAYA_ROOT_REGISTRY registry = { 0 };
    RAYA_ROOT_CONFIG configs[2];

    if (!create_fixtures(base, root_one, root_two)) {
        fprintf(stderr, "FATAL fixture creation failed: %lu\n",
            (unsigned long)GetLastError());
        cleanup_fixtures(base, root_one, root_two);
        return 2;
    }

    configs[0].root_id = "fixture";
    configs[0].absolute_path = root_one;
    configs[1].root_id = "secondary";
    configs[1].absolute_path = root_two;

    printf("ROOT_REGISTRY_CONTEXT_TEST_BEGIN\n");
    CHECK("initialize configured root registry",
        raya_root_registry_init(&registry, configs, 2));
    CHECK("registry contains two pinned roots",
        registry.initialized && registry.count == 2 &&
        registry.active_contexts == 0);

    test_read_and_root_mapping(&registry);
    test_list_directory_context(&registry);
    test_chunk_tail_and_file_info(&registry);
    test_invalid_requests_and_registry_close(&registry, root_one);
    test_invalid_registry_configs(root_one);
    test_reparse_root_rejection(root_one);

    if (registry.initialized)
        raya_root_registry_close(&registry);
    cleanup_fixtures(base, root_one, root_two);

    printf("ROOT_REGISTRY_CONTEXT_TESTS: %u passed, %u failed\n",
        passed, failed);
    return failed == 0 ? 0 : 1;
}
