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
    if (sids->system != NULL) HeapFree(GetProcessHeap(), 0, sids->system);
    if (sids->admins != NULL) HeapFree(GetProcessHeap(), 0, sids->admins);
    if (sids->client != NULL) HeapFree(GetProcessHeap(), 0, sids->client);
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
    sids->client = (PSID)HeapAlloc(GetProcessHeap(), 0, sid_size);
    if (sids->client == NULL || !CreateWellKnownSid(WinLocalServiceSid, NULL,
            sids->client, &sid_size))
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