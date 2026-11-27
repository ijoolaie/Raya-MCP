#define _WIN32_WINNT 0x0602
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <stdbool.h>

#include "handle-chain.h"

#define FILE_COUNT 1200u

static unsigned passed;
static unsigned failed;

#define CHECK(label, expression) do { \
    if (expression) { printf("PASS %s\n", label); ++passed; } \
    else { printf("FAIL %s (error=%lu)\n", label, (unsigned long)GetLastError()); ++failed; } \
} while (0)

static BOOL create_fixture(wchar_t root[MAX_PATH], wchar_t outside[MAX_PATH])
{
    wchar_t temp[MAX_PATH];
    DWORD length = GetTempPathW(MAX_PATH, temp);
    if (length == 0 || length >= MAX_PATH)
        return FALSE;

    (void)swprintf_s(root, MAX_PATH, L"%sRayaDirEnum-%lu-%llu",
        temp, (unsigned long)GetCurrentProcessId(),
        (unsigned long long)GetTickCount64());
    (void)swprintf_s(outside, MAX_PATH, L"%s-outside", root);

    if (!CreateDirectoryW(root, NULL)) {
        root[0] = L'\0';
        return FALSE;
    }
    if (!CreateDirectoryW(outside, NULL)) {
        RemoveDirectoryW(root);
        root[0] = L'\0';
        outside[0] = L'\0';
        return FALSE;
    }

    for (unsigned i = 0; i < FILE_COUNT; ++i) {
        wchar_t path[MAX_PATH];
        HANDLE file;
        (void)swprintf_s(path, MAX_PATH, L"%s\\item-%04u.txt", root, i);
        file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE)
            return FALSE;
        CloseHandle(file);
    }

    return TRUE;
}

static void cleanup_fixture(const wchar_t *root, const wchar_t *outside)
{
    if (root == NULL || root[0] == L'\0')
        return;
    wchar_t path[MAX_PATH];
    wchar_t renamed[MAX_PATH];
    (void)swprintf_s(renamed, MAX_PATH, L"%s-pinned", root);
    if (GetFileAttributesW(root) == INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW(renamed) != INVALID_FILE_ATTRIBUTES)
        (void)MoveFileExW(renamed, root, MOVEFILE_WRITE_THROUGH);

    (void)swprintf_s(path, MAX_PATH, L"%s\\link-outside", root);
    (void)RemoveDirectoryW(path);

    for (unsigned i = 0; i < FILE_COUNT; ++i) {
        (void)swprintf_s(path, MAX_PATH, L"%s\\item-%04u.txt", root, i);
        (void)DeleteFileW(path);
    }

    if (outside != NULL && outside[0] != L'\0') {
        (void)swprintf_s(path, MAX_PATH, L"%s\\outside-canary.txt", outside);
        (void)DeleteFileW(path);
        (void)RemoveDirectoryW(outside);
    }
    (void)RemoveDirectoryW(root);
}

static HANDLE open_directory_for_enumeration(const wchar_t *path)
{
    return CreateFileW(path,
        FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL);
}

static void test_pagination_and_restart(const wchar_t *root)
{
    HANDLE directory = open_directory_for_enumeration(root);
    bool seen[FILE_COUNT] = { false };
    size_t total = 0;
    unsigned pages = 0;
    wchar_t first_name[256] = { 0 };
    BOOL done = FALSE;
    BOOL names_valid = TRUE;
    BOOL metadata_valid = TRUE;

    CHECK("open directory with list access", directory != INVALID_HANDLE_VALUE);
    if (directory == INVALID_HANDLE_VALUE)
        return;

    while (!done) {
        RAYA_DIRECTORY_PAGE page = { 0 };
        CHECK("enumerate bounded directory page",
            raya_enumerate_directory_page(directory, 1024, FALSE, &page));
        if (failed != 0) {
            raya_free_directory_page(&page);
            break;
        }

        ++pages;
        for (size_t i = 0; i < page.count; ++i) {
            unsigned index = FILE_COUNT;
            if (swscanf_s(page.entries[i].name, L"item-%u.txt", &index) != 1 ||
                index >= FILE_COUNT || seen[index]) {
                names_valid = FALSE;
                continue;
            }
            seen[index] = true;
            ++total;
            if (total == 1)
                (void)wcscpy_s(first_name, 256, page.entries[i].name);
            if ((page.entries[i].attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
                (page.entries[i].attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
                page.entries[i].size != 0)
                metadata_valid = FALSE;
        }

        done = page.done;
        raya_free_directory_page(&page);
    }

    CHECK("all enumerated names are valid and unique", names_valid);
    CHECK("all entries have regular-file metadata", metadata_valid);
    CHECK("pagination returns every entry exactly once", total == FILE_COUNT);
    CHECK("pagination spans multiple OS pages", pages > 1);

    {
        RAYA_DIRECTORY_PAGE restart_page = { 0 };
        CHECK("restart resets cursor on same HANDLE",
            raya_enumerate_directory_page(directory, 1024, TRUE, &restart_page));
        CHECK("restart reproduces first page entry",
            restart_page.count != 0 &&
            wcscmp(restart_page.entries[0].name, first_name) == 0);
        raya_free_directory_page(&restart_page);
    }

    {
        RAYA_DIRECTORY_PAGE overflow_page = { 0 };
        BOOL result = raya_enumerate_directory_page(directory, 1, TRUE, &overflow_page);
        CHECK("over-cap page fails explicitly",
            !result && GetLastError() == ERROR_MORE_DATA &&
            overflow_page.entries == NULL && overflow_page.count == 0);
        raya_free_directory_page(&overflow_page);
    }

    {
        RAYA_DIRECTORY_PAGE restarted = { 0 };
        CHECK("restart after over-cap restores first page",
            raya_enumerate_directory_page(directory, 1024, TRUE, &restarted) &&
            restarted.count != 0);
        raya_free_directory_page(&restarted);
    }

    {
        RAYA_DIRECTORY_PAGE invalid = { 0 };
        CHECK("reject zero entry cap",
            !raya_enumerate_directory_page(directory, 0, FALSE, &invalid));
        CHECK("reject entry cap above 1024",
            !raya_enumerate_directory_page(directory, 1025, FALSE, &invalid));
    }

    CloseHandle(directory);
}

static void test_reparse_entry_and_no_traversal(
    const wchar_t *root, const wchar_t *outside)
{
    wchar_t canary[MAX_PATH];
    wchar_t link_path[MAX_PATH];
    HANDLE directory;
    RAYA_DIRECTORY_PAGE page = { 0 };
    BOOL linked;
    BOOL found = FALSE;

    (void)swprintf_s(canary, MAX_PATH, L"%s\\outside-canary.txt", outside);
    (void)swprintf_s(link_path, MAX_PATH, L"%s\\link-outside", root);

    {
        HANDLE file = CreateFileW(canary, GENERIC_WRITE, FILE_SHARE_READ,
            NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE) {
            CHECK("create outside canary for junction test", FALSE);
            return;
        }
        CloseHandle(file);
    }

    linked = CreateSymbolicLinkW(link_path, outside,
        SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE);
    if (!linked) {
        printf("SKIP directory symbolic-link creation unavailable (%lu)\n",
            (unsigned long)GetLastError());
        return;
    }

    directory = open_directory_for_enumeration(root);
    CHECK("open directory containing reparse entry", directory != INVALID_HANDLE_VALUE);
    if (directory == INVALID_HANDLE_VALUE)
        return;

    CHECK("enumerate directory containing reparse entry",
        raya_enumerate_directory_page(directory, 1024, TRUE, &page));
    for (size_t i = 0; i < page.count; ++i) {
        if (wcscmp(page.entries[i].name, L"link-outside") == 0) {
            found = (page.entries[i].attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        }
    }
    CHECK("reparse entry is reported but not followed", found);
    raya_free_directory_page(&page);
    CloseHandle(directory);

    CHECK("outside canary remains present after enumeration",
        GetFileAttributesW(canary) != INVALID_FILE_ATTRIBUTES);
}

static void test_directory_handle_pinning(const wchar_t *root)
{
    wchar_t renamed[MAX_PATH];
    HANDLE directory;
    BOOL renamed_while_open;
    wchar_t blocked_child[MAX_PATH];
    HANDLE blocked_create;
    (void)swprintf_s(renamed, MAX_PATH, L"%s-pinned", root);
    (void)swprintf_s(blocked_child, MAX_PATH, L"%s\\blocked-during-enumeration.txt", root);
    directory = open_directory_for_enumeration(root);
    CHECK("open directory for namespace pinning", directory != INVALID_HANDLE_VALUE);
    if (directory == INVALID_HANDLE_VALUE)
        return;

    blocked_create = CreateFileW(blocked_child, GENERIC_WRITE, FILE_SHARE_READ,
        NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (blocked_create != INVALID_HANDLE_VALUE) {
        printf("OBSERVED child creation succeeds while directory HANDLE is open; enumeration is not a snapshot\n");
        ++passed;
        CloseHandle(blocked_create);
        DeleteFileW(blocked_child);
    } else {
        DWORD create_error = GetLastError();
        CHECK("child creation while directory HANDLE is open is denied",
            create_error == ERROR_SHARING_VIOLATION || create_error == ERROR_ACCESS_DENIED);
    }

    renamed_while_open = MoveFileExW(root, renamed, MOVEFILE_WRITE_THROUGH);
    CHECK("directory rename blocked while enumeration HANDLE is open",
        !renamed_while_open &&
        (GetLastError() == ERROR_SHARING_VIOLATION ||
         GetLastError() == ERROR_ACCESS_DENIED));
    CloseHandle(directory);

    if (renamed_while_open) {
        CHECK("restore directory after unexpected open-time rename",
            MoveFileExW(renamed, root, MOVEFILE_WRITE_THROUGH));
    } else {
        CHECK("directory rename succeeds after HANDLE close",
            MoveFileExW(root, renamed, MOVEFILE_WRITE_THROUGH));
        CHECK("directory rename restored for cleanup",
            MoveFileExW(renamed, root, MOVEFILE_WRITE_THROUGH));
    }
}

static void test_empty_directory(const wchar_t *root)
{
    wchar_t empty_path[MAX_PATH];
    HANDLE directory;
    RAYA_DIRECTORY_PAGE page = { 0 };
    BOOL first_done;

    (void)swprintf_s(empty_path, MAX_PATH, L"%s\\empty", root);
    CHECK("create empty directory fixture", CreateDirectoryW(empty_path, NULL));
    directory = open_directory_for_enumeration(empty_path);
    CHECK("open empty directory HANDLE", directory != INVALID_HANDLE_VALUE);
    if (directory == INVALID_HANDLE_VALUE) {
        RemoveDirectoryW(empty_path);
        return;
    }

    CHECK("empty directory first page succeeds",
        raya_enumerate_directory_page(directory, 16, TRUE, &page));
    CHECK("empty directory returns no entries", page.count == 0);
    first_done = page.done;
    raya_free_directory_page(&page);

    if (!first_done) {
        CHECK("empty directory reaches explicit end-of-directory",
            raya_enumerate_directory_page(directory, 16, FALSE, &page) &&
            page.done && page.count == 0);
        raya_free_directory_page(&page);
    } else {
        CHECK("empty directory reports explicit end-of-directory", first_done);
    }

    CloseHandle(directory);
    CHECK("remove empty directory fixture", RemoveDirectoryW(empty_path));
}
static void test_invalid_handles(const wchar_t *root)
{
    HANDLE file;
    HANDLE directory;
    RAYA_DIRECTORY_PAGE page = { 0 };
    wchar_t file_path[MAX_PATH];

    (void)swprintf_s(file_path, MAX_PATH, L"%s\\item-0000.txt", root);
    file = CreateFileW(file_path, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK("open regular file for invalid-handle test", file != INVALID_HANDLE_VALUE);
    CHECK("reject regular file HANDLE as directory",
        !raya_enumerate_directory_page(file, 16, FALSE, &page));
    CloseHandle(file);

    directory = open_directory_for_enumeration(root);
    CHECK("open directory for closed-handle test", directory != INVALID_HANDLE_VALUE);
    CloseHandle(directory);
    CHECK("reject closed directory HANDLE",
        !raya_enumerate_directory_page(directory, 16, FALSE, &page));
}

int main(void)
{
    wchar_t root[MAX_PATH] = { 0 };
    wchar_t outside[MAX_PATH] = { 0 };

    if (!create_fixture(root, outside)) {
        fprintf(stderr, "FATAL fixture creation failed: %lu\n",
            (unsigned long)GetLastError());
        cleanup_fixture(root, outside);
        return 2;
    }

    printf("DIRECTORY_ENUMERATION_TEST_BEGIN\n");
    test_pagination_and_restart(root);
    test_reparse_entry_and_no_traversal(root, outside);
    test_directory_handle_pinning(root);
    test_empty_directory(root);
    test_invalid_handles(root);
    cleanup_fixture(root, outside);

    printf("DIRECTORY_ENUMERATION_TESTS: %u passed, %u failed\n",
        passed, failed);
    return failed == 0 ? 0 : 1;
}