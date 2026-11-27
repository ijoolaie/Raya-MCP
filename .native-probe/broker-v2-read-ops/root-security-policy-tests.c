#define _WIN32_WINNT 0x0602
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <aclapi.h>
#include <stdio.h>
#include <string.h>

#include "root-registry.h"

#pragma comment(lib, "Advapi32.lib")

static unsigned passed;
static unsigned failed;

#define CHECK(label, expression) do { \
    if (expression) { printf("PASS %s\n", label); ++passed; } \
    else { printf("FAIL %s (error=%lu)\n", label, (unsigned long)GetLastError()); ++failed; } \
} while (0)

typedef struct SID_FIXTURES {
    HANDLE token;
    BYTE *token_user;
    PSID service;
    PSID system;
    PSID admins;
    PSID client;
    PSID everyone;
} SID_FIXTURES;

static void free_sid_fixtures(SID_FIXTURES *sids)
{
    if (sids->token_user != NULL) HeapFree(GetProcessHeap(), 0, sids->token_user);
    if (sids->token != NULL) CloseHandle(sids->token);
    if (sids->client != NULL) HeapFree(GetProcessHeap(), 0, sids->client);
    if (sids->system != NULL) HeapFree(GetProcessHeap(), 0, sids->system);
    if (sids->admins != NULL) HeapFree(GetProcessHeap(), 0, sids->admins);
    if (sids->everyone != NULL) HeapFree(GetProcessHeap(), 0, sids->everyone);
    ZeroMemory(sids, sizeof(*sids));
}

static BOOL init_sid_fixtures(SID_FIXTURES *sids)
{
    DWORD needed = 0;
    DWORD sid_size;
    ZeroMemory(sids, sizeof(*sids));
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &sids->token))
        return FALSE;
    (void)GetTokenInformation(sids->token, TokenUser, NULL, 0, &needed);
    if (needed == 0)
        return FALSE;
    sids->token_user = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, needed);
    if (sids->token_user == NULL ||
        !GetTokenInformation(sids->token, TokenUser, sids->token_user,
            needed, &needed))
        return FALSE;
    sids->service = ((TOKEN_USER *)sids->token_user)->User.Sid;

    sid_size = SECURITY_MAX_SID_SIZE;
    sids->client = (PSID)HeapAlloc(GetProcessHeap(), 0, sid_size);
    if (sids->client == NULL || !CreateWellKnownSid(WinLocalServiceSid, NULL,
            sids->client, &sid_size))
        return FALSE;
    sid_size = SECURITY_MAX_SID_SIZE;
    sids->system = (PSID)HeapAlloc(GetProcessHeap(), 0, sid_size);
    if (sids->system == NULL || !CreateWellKnownSid(WinLocalSystemSid, NULL,
            sids->system, &sid_size))
        return FALSE;
    sid_size = SECURITY_MAX_SID_SIZE;
    sids->admins = (PSID)HeapAlloc(GetProcessHeap(), 0, sid_size);
    if (sids->admins == NULL || !CreateWellKnownSid(WinBuiltinAdministratorsSid,
            NULL, sids->admins, &sid_size))
        return FALSE;
    sid_size = SECURITY_MAX_SID_SIZE;
    sids->everyone = (PSID)HeapAlloc(GetProcessHeap(), 0, sid_size);
    if (sids->everyone == NULL || !CreateWellKnownSid(WinWorldSid, NULL,
            sids->everyone, &sid_size))
        return FALSE;
    return TRUE;
}
static BOOL create_policy_directory(
    const wchar_t *path,
    SID_FIXTURES *sids,
    unsigned variant)
{
    PACL acl = NULL;
    PSECURITY_DESCRIPTOR sd = NULL;
    DWORD acl_size = sizeof(ACL);
    DWORD inherit = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE;
    DWORD client_flags = inherit;
    ACCESS_MASK client_mask = FILE_READ_DATA | FILE_READ_ATTRIBUTES |
        READ_CONTROL | SYNCHRONIZE | FILE_LIST_DIRECTORY | FILE_TRAVERSE;
    SECURITY_ATTRIBUTES sa;
    BOOL ok = FALSE;
    if (variant == 5)
        return CreateDirectoryW(path, NULL);

    acl_size += sizeof(ACCESS_ALLOWED_ACE) + GetLengthSid(sids->service) - sizeof(DWORD);
    acl_size += sizeof(ACCESS_ALLOWED_ACE) + GetLengthSid(sids->system) - sizeof(DWORD);
    acl_size += sizeof(ACCESS_ALLOWED_ACE) + GetLengthSid(sids->admins) - sizeof(DWORD);
    acl_size += sizeof(ACCESS_ALLOWED_ACE) +
        ((variant == 3 ? GetLengthSid(sids->service) : GetLengthSid(sids->client)) - sizeof(DWORD));
    if (variant == 1)
        acl_size += sizeof(ACCESS_ALLOWED_ACE) + GetLengthSid(sids->everyone) - sizeof(DWORD);

    acl = (PACL)LocalAlloc(LMEM_FIXED, acl_size);
    sd = (PSECURITY_DESCRIPTOR)LocalAlloc(LMEM_FIXED, SECURITY_DESCRIPTOR_MIN_LENGTH);
    if (acl == NULL || sd == NULL || !InitializeAcl(acl, acl_size, ACL_REVISION))
        goto done;

    if (!AddAccessAllowedAceEx(acl, ACL_REVISION, inherit, FILE_ALL_ACCESS, sids->service) ||
        !AddAccessAllowedAceEx(acl, ACL_REVISION, inherit, FILE_ALL_ACCESS, sids->system) ||
        !AddAccessAllowedAceEx(acl, ACL_REVISION, inherit, FILE_ALL_ACCESS, sids->admins))
        goto done;

    if (variant == 2)
        client_mask = FILE_ALL_ACCESS;
    if (variant == 3) {
        if (!AddAccessAllowedAceEx(acl, ACL_REVISION, inherit,
                FILE_ALL_ACCESS, sids->service))
            goto done;
    } else {
        if (variant == 4)
            client_flags = OBJECT_INHERIT_ACE;
        if (!AddAccessAllowedAceEx(acl, ACL_REVISION, client_flags,
                client_mask, sids->client))
            goto done;
    }

    if (variant == 1 &&
        !AddAccessAllowedAceEx(acl, ACL_REVISION, inherit,
            FILE_READ_DATA | FILE_READ_ATTRIBUTES, sids->everyone))
        goto done;

    if (!InitializeSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION) ||
        !SetSecurityDescriptorOwner(sd, sids->service, FALSE) ||
        !SetSecurityDescriptorDacl(sd, TRUE, acl, FALSE))
        goto done;
    if (variant != 5 &&
        !SetSecurityDescriptorControl(sd, SE_DACL_PROTECTED, SE_DACL_PROTECTED))
        goto done;

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = sd;
    sa.bInheritHandle = FALSE;
    ok = CreateDirectoryW(path, &sa);
    if (ok && variant == 5) {
        DWORD error = SetNamedSecurityInfoW((LPWSTR)path, SE_FILE_OBJECT,
            UNPROTECTED_DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL);
        if (error != ERROR_SUCCESS) {
            (void)RemoveDirectoryW(path);
            SetLastError(error);
            ok = FALSE;
        }
    }

done:
    if (sd != NULL) LocalFree(sd);
    if (acl != NULL) LocalFree(acl);
    return ok;
}

static HANDLE open_policy_directory(const wchar_t *path)
{
    return CreateFileW(path,
        FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | READ_CONTROL,
        FILE_SHARE_READ, NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
}

static BOOL create_client_writable_destination(
    const wchar_t *path,
    SID_FIXTURES *sids)
{
    DWORD acl_size = sizeof(ACL);
    DWORD inherit = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE;
    PACL acl = NULL;
    PSECURITY_DESCRIPTOR sd = NULL;
    SECURITY_ATTRIBUTES sa;
    BOOL ok = FALSE;

    acl_size += sizeof(ACCESS_ALLOWED_ACE) +
        GetLengthSid(sids->service) - sizeof(DWORD);
    acl_size += sizeof(ACCESS_ALLOWED_ACE) +
        GetLengthSid(sids->client) - sizeof(DWORD);
    acl = (PACL)LocalAlloc(LMEM_FIXED, acl_size);
    sd = (PSECURITY_DESCRIPTOR)LocalAlloc(LMEM_FIXED,
        SECURITY_DESCRIPTOR_MIN_LENGTH);
    if (acl == NULL || sd == NULL ||
        !InitializeAcl(acl, acl_size, ACL_REVISION) ||
        !AddAccessAllowedAceEx(acl, ACL_REVISION, inherit,
            FILE_ALL_ACCESS, sids->service) ||
        !AddAccessAllowedAceEx(acl, ACL_REVISION, inherit,
            FILE_ALL_ACCESS, sids->client) ||
        !InitializeSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION) ||
        !SetSecurityDescriptorOwner(sd, sids->service, FALSE) ||
        !SetSecurityDescriptorDacl(sd, TRUE, acl, FALSE) ||
        !SetSecurityDescriptorControl(sd, SE_DACL_PROTECTED, SE_DACL_PROTECTED))
        goto done;

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = sd;
    sa.bInheritHandle = FALSE;
    ok = CreateDirectoryW(path, &sa);

done:
    if (sd != NULL) LocalFree(sd);
    if (acl != NULL) LocalFree(acl);
    return ok;
}
static void test_client_read_only_and_hardlink(
    const wchar_t *root_path,
    const wchar_t *temp,
    SID_FIXTURES *sids)
{
    wchar_t file_path[MAX_PATH];
    wchar_t link_path[MAX_PATH];
    wchar_t destination_path[MAX_PATH];
    wchar_t dummy_path[MAX_PATH];
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE token = NULL;
    HANDLE restricted = NULL;
    SID_AND_ATTRIBUTES restricted_sid;
    const char payload[] = "root-policy-inheritance";
    DWORD written = 0;
    DWORD read_count = 0;
    char readback[sizeof(payload)] = { 0 };
    FILE_STANDARD_INFO standard;
    BOOL linked;
    DWORD link_error;

    (void)swprintf_s(file_path, MAX_PATH, L"%s\\payload.txt", root_path);
    (void)swprintf_s(destination_path, MAX_PATH, L"%s\\RayaHardlinkDestination-%lu", temp, (unsigned long)GetCurrentProcessId());
    (void)swprintf_s(link_path, MAX_PATH, L"%s\\client-link.txt", destination_path);
    (void)swprintf_s(dummy_path, MAX_PATH, L"%s\\client-write-test.txt", destination_path);
    CHECK("create client-writable external destination directory", create_client_writable_destination(destination_path, sids));
    file = CreateFileW(file_path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK("create child file under protected configured root",
        file != INVALID_HANDLE_VALUE);
    if (file == INVALID_HANDLE_VALUE)
        return;
    CHECK("write child fixture before client impersonation",
        WriteFile(file, payload, (DWORD)(sizeof(payload) - 1u), &written, NULL) &&
        written == sizeof(payload) - 1u);
    CloseHandle(file);

    if (!OpenProcessToken(GetCurrentProcess(),
            TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_IMPERSONATE, &token)) {
        CHECK("open process token for restricted client", FALSE);
        return;
    }
    restricted_sid.Sid = sids->client;
    restricted_sid.Attributes = 0;
    CHECK("create restricted client token requiring client SID access",
        CreateRestrictedToken(token, DISABLE_MAX_PRIVILEGE,
            0, NULL, 0, NULL, 1, &restricted_sid, &restricted));
    if (restricted == NULL) {
        CloseHandle(token);
        return;
    }
    CHECK("impersonate read-only configured-root client",
        ImpersonateLoggedOnUser(restricted));
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &file)) {
        CHECK("verify impersonation token is active", FALSE);
        RevertToSelf();
        CloseHandle(restricted);
        CloseHandle(token);
        return;
    }
    CloseHandle(file);
    file = CreateFileW(file_path, FILE_READ_DATA | FILE_READ_ATTRIBUTES | READ_CONTROL | SYNCHRONIZE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK("authorized client can read inherited child file",
        file != INVALID_HANDLE_VALUE);
    CHECK("authorized client reads exact inherited file content",
        file != INVALID_HANDLE_VALUE &&
        ReadFile(file, readback, (DWORD)(sizeof(payload) - 1u), &read_count, NULL) &&
        read_count == sizeof(payload) - 1u &&
        memcmp(readback, payload, read_count) == 0);
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);

    file = CreateFileW(file_path, GENERIC_WRITE, FILE_SHARE_READ,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK("authorized client cannot open child file for write",
        file == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED);
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);

    file = CreateFileW(dummy_path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK("authorized client can create an unrelated file in external destination",
        file != INVALID_HANDLE_VALUE);
    if (file != INVALID_HANDLE_VALUE) {
        (void)WriteFile(file, payload, (DWORD)(sizeof(payload) - 1u), &written, NULL);
        CloseHandle(file);
        (void)DeleteFileW(dummy_path);
    }

    linked = CreateHardLinkW(link_path, file_path, NULL);
    link_error = linked ? ERROR_SUCCESS : GetLastError();
    CHECK("authorized client cannot create a hard link into configured root",
        !linked && link_error == ERROR_ACCESS_DENIED);
    if (linked)
        (void)DeleteFileW(link_path);
    file = CreateFileW(file_path, FILE_READ_ATTRIBUTES | READ_CONTROL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK("denied hard-link attempt leaves exactly one file link",
        file != INVALID_HANDLE_VALUE &&
        GetFileInformationByHandleEx(file, FileStandardInfo, &standard, sizeof(standard)) &&
        standard.NumberOfLinks == 1);
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);

    CHECK("revert from restricted client impersonation", RevertToSelf());
    CloseHandle(restricted);
    CloseHandle(token);
    (void)DeleteFileW(file_path);
    (void)RemoveDirectoryW(destination_path);
    (void)sids;
}
int main(void)
{
    SID_FIXTURES sids;
    wchar_t temp[MAX_PATH];
    wchar_t paths[6][MAX_PATH];
    DWORD temp_length;
    const unsigned variants[] = { 0, 1, 2, 3, 4, 5 };
    const char *labels[] = {
        "accept exact protected owner/DACL policy",
        "reject additional Everyone ACE",
        "reject overbroad client rights",
        "reject duplicate service ACE in place of client",
        "reject incorrect ACE inheritance flags",
        "reject default inherited DACL"
    };
    BOOL fixtures_ok = TRUE;

    printf("ROOT_SECURITY_POLICY_TEST_BEGIN\n");
    CHECK("initialize service/client/system/admin SID fixtures",
        init_sid_fixtures(&sids));
    if (failed != 0) {
        free_sid_fixtures(&sids);
        return 1;
    }

    temp_length = GetTempPathW(MAX_PATH, temp);
    CHECK("obtain temp directory for root DACL fixtures",
        temp_length != 0 && temp_length < MAX_PATH);
    if (temp_length == 0 || temp_length >= MAX_PATH) {
        free_sid_fixtures(&sids);
        return 1;
    }

    for (size_t i = 0; i < (sizeof(variants) / sizeof(variants[0])); ++i) {
        (void)swprintf_s(paths[i], MAX_PATH,
            L"%sRayaRootSecurity-%lu-%llu-%u", temp,
            (unsigned long)GetCurrentProcessId(),
            (unsigned long long)GetTickCount64(), (unsigned)i);
        if (!create_policy_directory(paths[i], &sids, variants[i])) {
            printf("FATAL create security fixture %u failed: %lu\n",
                (unsigned)i, (unsigned long)GetLastError());
            fixtures_ok = FALSE;
            break;
        }
    }
    CHECK("create all protected/untrusted DACL fixtures", fixtures_ok);

    if (fixtures_ok) {
        test_client_read_only_and_hardlink(paths[0], temp, &sids);
        for (size_t i = 0; i < (sizeof(variants) / sizeof(variants[0])); ++i) {
            HANDLE root = open_policy_directory(paths[i]);
            CHECK("open root security fixture with READ_CONTROL",
                root != INVALID_HANDLE_VALUE);
            if (root == INVALID_HANDLE_VALUE)
                continue;
            CHECK(labels[i], raya_validate_root_security(
                root, sids.service, sids.client) == (variants[i] == 0));
            CloseHandle(root);
        }

        {
            RAYA_ROOT_REGISTRY registry = { 0 };
            RAYA_ROOT_CONFIG config = { "secure", paths[0] };
            CHECK("secure registry init accepts exact root policy",
                raya_root_registry_init_secure(&registry, &config, 1,
                    sids.service, sids.client));
            CHECK("secure registry retains validated root",
                registry.initialized && registry.count == 1);
            CHECK("close secure registry after policy validation",
                raya_root_registry_close(&registry));
        }

        {
            RAYA_ROOT_REGISTRY registry = { 0 };
            RAYA_ROOT_CONFIG config = { "untrusted", paths[1] };
            CHECK("secure registry init rejects overbroad root DACL",
                !raya_root_registry_init_secure(&registry, &config, 1,
                    sids.service, sids.client) &&
                !registry.initialized && registry.count == 0);
        }
    }

    for (size_t i = 0; i < (sizeof(variants) / sizeof(variants[0])); ++i)
        (void)RemoveDirectoryW(paths[i]);
    free_sid_fixtures(&sids);

    printf("ROOT_SECURITY_POLICY_TESTS: %u passed, %u failed\n",
        passed, failed);
    return failed == 0 ? 0 : 1;
}