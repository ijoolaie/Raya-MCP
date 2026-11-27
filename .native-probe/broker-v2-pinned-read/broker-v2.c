#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <winternl.h>
#include <winsvc.h>
#include <aclapi.h>
#include <sddl.h>
#include <wtsapi32.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <stdlib.h>

#define SERVICE_NAME L"RayaFsBrokerV2Probe20261127"
#define SERVICE_ACCOUNT L"NT SERVICE\\" SERVICE_NAME
#define ROOT_PATH L"C:\\ProgramData\\RayaFsBrokerV2Probe-20261127"
#define ROOT_OLD_PATH L"C:\\ProgramData\\RayaFsBrokerV2Probe-20261127-original"
#define OLD_PAYLOAD_PATH ROOT_OLD_PATH L"\\payload.txt"
#define OLD_POLICY_PATH ROOT_OLD_PATH L"\\client.policy"
#define PAYLOAD_PATH ROOT_PATH L"\\payload.txt"
#define POLICY_PATH ROOT_PATH L"\\client.policy"
#define BIN_PATH L"C:\\ProgramData\\RayaFsBrokerV2ProbeBin-20261127"
#define BIN_EXE_PATH BIN_PATH L"\\broker-v2.exe"
#define PIPE_NAME L"\\\\.\\pipe\\RayaFsBrokerV2PipeProbe20261127"
#define MAX_REQUEST 256
#define MAX_PAYLOAD 4096

static SERVICE_STATUS_HANDLE g_status_handle;
static SERVICE_STATUS g_status;
static HANDLE g_stop_event;
static HANDLE g_service_thread;
static HANDLE g_root_handle = INVALID_HANDLE_VALUE;

static void err(const wchar_t *where, DWORD e) {
    fwprintf(stderr, L"%ls: %lu\n", where, e);
}
static BOOL lookup_sid(const wchar_t *name, PSID *out) {
    DWORD ns=0, nd=0; SID_NAME_USE use;
    LookupAccountNameW(NULL,name,NULL,&ns,NULL,&nd,&use);
    if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER) return FALSE;
    PSID s=(PSID)LocalAlloc(LMEM_FIXED,ns);
    wchar_t *d=(wchar_t*)LocalAlloc(LMEM_FIXED,(nd?nd:1)*sizeof(wchar_t));
    if(!s||!d){if(s)LocalFree(s);if(d)LocalFree(d);SetLastError(ERROR_OUTOFMEMORY);return FALSE;}
    if(!LookupAccountNameW(NULL,name,s,&ns,d,&nd,&use)){DWORD e=GetLastError();LocalFree(s);LocalFree(d);SetLastError(e);return FALSE;}
    LocalFree(d);*out=s;return TRUE;
}
static BOOL privilege_enabled(HANDLE token, const wchar_t *name)
{
    LUID target;
    DWORD bytes = 0;
    TOKEN_PRIVILEGES *privileges = NULL;
    BOOL enabled = FALSE;
    if (!LookupPrivilegeValueW(NULL, name, &target))
        return TRUE; /* fail closed if the privilege cannot be identified */
    (void)GetTokenInformation(token, TokenPrivileges, NULL, 0, &bytes);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0)
        return TRUE;
    privileges = (TOKEN_PRIVILEGES *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
    if (privileges == NULL)
        return TRUE;
    if (!GetTokenInformation(token, TokenPrivileges, privileges, bytes, &bytes)) {
        HeapFree(GetProcessHeap(), 0, privileges);
        return TRUE;
    }
    for (DWORD i = 0; i < privileges->PrivilegeCount; ++i) {
        if (privileges->Privileges[i].Luid.LowPart == target.LowPart &&
            privileges->Privileges[i].Luid.HighPart == target.HighPart &&
            (privileges->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED)) {
            enabled = TRUE;
            break;
        }
    }
    HeapFree(GetProcessHeap(), 0, privileges);
    return enabled;
}

static BOOL verify_service_identity(PSID expected_service_sid)
{
    HANDLE token = NULL;
    TOKEN_USER *user = NULL;
    TOKEN_GROUPS *groups = NULL;
    DWORD bytes = 0;
    BOOL member = FALSE;
    BOOL ok = FALSE;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return FALSE;
    (void)GetTokenInformation(token, TokenUser, NULL, 0, &bytes);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0)
        goto done;
    user = (TOKEN_USER *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
    if (user == NULL ||
        !GetTokenInformation(token, TokenUser, user, bytes, &bytes))
        goto done;
    member = EqualSid(user->User.Sid, expected_service_sid);
    if (!member) {
        bytes = 0;
        (void)GetTokenInformation(token, TokenGroups, NULL, 0, &bytes);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0)
            goto done;
        groups = (TOKEN_GROUPS *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
        if (groups == NULL ||
            !GetTokenInformation(token, TokenGroups, groups, bytes, &bytes))
            goto done;
        for (DWORD i = 0; i < groups->GroupCount; ++i) {
            if (EqualSid(groups->Groups[i].Sid, expected_service_sid) &&
                (groups->Groups[i].Attributes & SE_GROUP_ENABLED)) {
                member = TRUE;
                break;
            }
        }
    }
    if (!member)
        goto done;
    if (privilege_enabled(token, SE_BACKUP_NAME) ||
        privilege_enabled(token, SE_RESTORE_NAME) ||
        privilege_enabled(token, SE_TAKE_OWNERSHIP_NAME) ||
        privilege_enabled(token, SE_DEBUG_NAME))
        goto done;
    ok = TRUE;
done:
    if (groups != NULL)
        HeapFree(GetProcessHeap(), 0, groups);
    if (user != NULL)
        HeapFree(GetProcessHeap(), 0, user);
    if (token != NULL)
        CloseHandle(token);
    if (!ok)
        SetLastError(ERROR_ACCESS_DENIED);
    return ok;
}

static BOOL current_user(PSID *out, DWORD *session) {
    HANDLE t=NULL;DWORD n=0;BOOL ok=FALSE;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&t))return FALSE;
    GetTokenInformation(t,TokenUser,NULL,0,&n);
    if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER)goto done;
    TOKEN_USER *u=(TOKEN_USER*)LocalAlloc(LMEM_FIXED,n);
    if(!u)goto done;
    if(GetTokenInformation(t,TokenUser,u,n,&n)){
        DWORD z=GetLengthSid(u->User.Sid);*out=LocalAlloc(LMEM_FIXED,z);
        if(*out&&CopySid(z,*out,u->User.Sid)&&ProcessIdToSessionId(GetCurrentProcessId(),session))ok=TRUE;
    }
    LocalFree(u);
done: CloseHandle(t);return ok;
}
static BOOL set_privilege(HANDLE t,const wchar_t *name){
    TOKEN_PRIVILEGES p;LUID l;
    if(!LookupPrivilegeValueW(NULL,name,&l))return FALSE;
    p.PrivilegeCount=1;p.Privileges[0].Luid=l;p.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
    if(!AdjustTokenPrivileges(t,FALSE,&p,sizeof(p),NULL,NULL))return FALSE;
    return GetLastError()!=ERROR_NOT_ALL_ASSIGNED;
}
static BOOL write_file_secure(const wchar_t *path,const void *data,DWORD size,SECURITY_ATTRIBUTES *sa){
    HANDLE h=CreateFileW(path,GENERIC_WRITE,0,sa,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(h==INVALID_HANDLE_VALUE)return FALSE;
    DWORD n=0;BOOL ok=WriteFile(h,data,size,&n,NULL)&&n==size&&FlushFileBuffers(h);
    CloseHandle(h);return ok;
}
static BOOL prepare_fixture(void){
    PSID svc=NULL,user=NULL,sys=NULL,admins=NULL;PACL acl=NULL;PSECURITY_DESCRIPTOR sd=NULL;
    HANDLE tok=NULL;BOOL ok=FALSE;DWORD session=0;LPWSTR sidtext=NULL;DWORD asz;
    SID_IDENTIFIER_AUTHORITY nt=SECURITY_NT_AUTHORITY;
    if(!lookup_sid(SERVICE_ACCOUNT,&svc)||!current_user(&user,&session))goto done;
    if(!AllocateAndInitializeSid(&nt,1,SECURITY_LOCAL_SYSTEM_RID,0,0,0,0,0,0,0,&sys))goto done;
    if(!AllocateAndInitializeSid(&nt,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&admins))goto done;
    asz=sizeof(ACL);
    asz+=(DWORD)sizeof(ACCESS_ALLOWED_ACE)+GetLengthSid(svc)-sizeof(DWORD);
    asz+=(DWORD)sizeof(ACCESS_ALLOWED_ACE)+GetLengthSid(sys)-sizeof(DWORD);
    asz+=(DWORD)sizeof(ACCESS_ALLOWED_ACE)+GetLengthSid(admins)-sizeof(DWORD);
    asz+=(DWORD)sizeof(ACCESS_ALLOWED_ACE)+GetLengthSid(user)-sizeof(DWORD);
    acl=(PACL)LocalAlloc(LMEM_FIXED,asz);sd=(PSECURITY_DESCRIPTOR)LocalAlloc(LMEM_FIXED,SECURITY_DESCRIPTOR_MIN_LENGTH);
    if(!acl||!sd||!InitializeAcl(acl,asz,ACL_REVISION))goto done;
    DWORD inh=OBJECT_INHERIT_ACE|CONTAINER_INHERIT_ACE;
    DWORD ur=FILE_READ_DATA|FILE_READ_ATTRIBUTES|READ_CONTROL|SYNCHRONIZE|FILE_LIST_DIRECTORY|FILE_TRAVERSE;
    if(!AddAccessAllowedAceEx(acl,ACL_REVISION,inh,FILE_ALL_ACCESS,svc)||
       !AddAccessAllowedAceEx(acl,ACL_REVISION,inh,FILE_ALL_ACCESS,sys)||
       !AddAccessAllowedAceEx(acl,ACL_REVISION,inh,FILE_ALL_ACCESS,admins)||
       !AddAccessAllowedAceEx(acl,ACL_REVISION,inh,ur,user))goto done;
    if(!InitializeSecurityDescriptor(sd,SECURITY_DESCRIPTOR_REVISION)||
       !SetSecurityDescriptorOwner(sd,svc,FALSE)||
       !SetSecurityDescriptorDacl(sd,TRUE,acl,FALSE)||
       !SetSecurityDescriptorControl(sd,SE_DACL_PROTECTED,SE_DACL_PROTECTED))goto done;
    SECURITY_ATTRIBUTES sa={sizeof(sa),sd,FALSE};
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&tok)||
       !set_privilege(tok,SE_RESTORE_NAME))goto done;
    if(!CreateDirectoryW(ROOT_PATH,&sa)){err(L"CreateDirectory fixture",GetLastError());goto done;}
    if(!ConvertSidToStringSidW(user,&sidtext))goto done;
    char policy[256];int pn=_snprintf_s(policy,sizeof(policy),_TRUNCATE,"%ls\n%lu\n",sidtext,(unsigned long)session);
    if(pn<=0)goto done;
    const char payload[]="Raya isolated Broker v2 pinned-root read-only fixture\n";
    if(!write_file_secure(POLICY_PATH,policy,(DWORD)pn,&sa)||
       !write_file_secure(PAYLOAD_PATH,payload,(DWORD)(sizeof(payload)-1),&sa))goto done;
    wprintf(L"FIXTURE_READY session=%lu\n",session);ok=TRUE;
done:
    if(tok)CloseHandle(tok);if(sidtext)LocalFree(sidtext);if(sd)LocalFree(sd);if(acl)LocalFree(acl);
    if(svc)LocalFree(svc);if(user)LocalFree(user);if(sys)FreeSid(sys);if(admins)FreeSid(admins);return ok;
}
static BOOL install_image(void){
    PSID svc=NULL,user=NULL,sys=NULL,admins=NULL;PACL acl=NULL;PSECURITY_DESCRIPTOR sd=NULL;HANDLE tok=NULL;BOOL ok=FALSE;
    SID_IDENTIFIER_AUTHORITY nt=SECURITY_NT_AUTHORITY;wchar_t src[MAX_PATH];DWORD z,as;
    if(!lookup_sid(SERVICE_ACCOUNT,&svc)||!current_user(&user,&z))goto done;
    if(!AllocateAndInitializeSid(&nt,1,SECURITY_LOCAL_SYSTEM_RID,0,0,0,0,0,0,0,&sys)||
       !AllocateAndInitializeSid(&nt,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&admins))goto done;
    as=sizeof(ACL);
    as+=(DWORD)sizeof(ACCESS_ALLOWED_ACE)+GetLengthSid(svc)-sizeof(DWORD);
    as+=(DWORD)sizeof(ACCESS_ALLOWED_ACE)+GetLengthSid(sys)-sizeof(DWORD);
    as+=(DWORD)sizeof(ACCESS_ALLOWED_ACE)+GetLengthSid(admins)-sizeof(DWORD);
    as+=(DWORD)sizeof(ACCESS_ALLOWED_ACE)+GetLengthSid(user)-sizeof(DWORD);
    acl=(PACL)LocalAlloc(LMEM_FIXED,as);sd=(PSECURITY_DESCRIPTOR)LocalAlloc(LMEM_FIXED,SECURITY_DESCRIPTOR_MIN_LENGTH);
    if(!acl||!sd||!InitializeAcl(acl,as,ACL_REVISION))goto done;
    DWORD inh=OBJECT_INHERIT_ACE|CONTAINER_INHERIT_ACE;
    if(!AddAccessAllowedAceEx(acl,ACL_REVISION,inh,FILE_ALL_ACCESS,svc)||
       !AddAccessAllowedAceEx(acl,ACL_REVISION,inh,FILE_ALL_ACCESS,sys)||
       !AddAccessAllowedAceEx(acl,ACL_REVISION,inh,FILE_ALL_ACCESS,admins)||
       !AddAccessAllowedAceEx(acl,ACL_REVISION,inh,FILE_GENERIC_READ|FILE_GENERIC_EXECUTE|READ_CONTROL|SYNCHRONIZE,user)||
       !InitializeSecurityDescriptor(sd,SECURITY_DESCRIPTOR_REVISION)||
       !SetSecurityDescriptorOwner(sd,svc,FALSE)||!SetSecurityDescriptorDacl(sd,TRUE,acl,FALSE)||
       !SetSecurityDescriptorControl(sd,SE_DACL_PROTECTED,SE_DACL_PROTECTED))goto done;
    SECURITY_ATTRIBUTES sa={sizeof(sa),sd,FALSE};
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&tok)||!set_privilege(tok,SE_RESTORE_NAME))goto done;
    if(!CreateDirectoryW(BIN_PATH,&sa)){err(L"CreateDirectory image",GetLastError());goto done;}
    z=GetModuleFileNameW(NULL,src,MAX_PATH);if(!z||z>=MAX_PATH||!CopyFileW(src,BIN_EXE_PATH,TRUE))goto done;
    DWORD e=SetNamedSecurityInfoW(BIN_EXE_PATH,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION,svc,NULL,NULL,NULL);
    if(e!=ERROR_SUCCESS){err(L"Set image owner",e);goto done;}
    wprintf(L"IMAGE_READY\n");ok=TRUE;
done:
    if(tok)CloseHandle(tok);if(sd)LocalFree(sd);if(acl)LocalFree(acl);if(svc)LocalFree(svc);if(user)LocalFree(user);
    if(sys)FreeSid(sys);if(admins)FreeSid(admins);return ok;
}
#ifndef OBJ_DONT_REPARSE
#define OBJ_DONT_REPARSE 0x00001000L
#endif

typedef NTSTATUS (NTAPI *NtCreateFileFn)(
    PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK,
    PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);

static HANDLE open_relative_regular(HANDLE root, const wchar_t *leaf,
                                    ACCESS_MASK access)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    NtCreateFileFn nt = ntdll ? (NtCreateFileFn)GetProcAddress(ntdll, "NtCreateFile") : NULL;
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    HANDLE file = INVALID_HANDLE_VALUE;
    size_t chars = wcslen(leaf);
    if (!nt || root == INVALID_HANDLE_VALUE || chars == 0 || chars > 255 ||
        wcschr(leaf, L'\\') != NULL || wcschr(leaf, L'/') != NULL ||
        wcscmp(leaf, L".") == 0 || wcscmp(leaf, L"..") == 0) {
        SetLastError(ERROR_INVALID_NAME);
        return INVALID_HANDLE_VALUE;
    }
    name.Buffer = (PWSTR)leaf;
    name.Length = (USHORT)(chars * sizeof(wchar_t));
    name.MaximumLength = name.Length;
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE,
                               root, NULL);
    NTSTATUS status = nt(&file, access | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        &oa, &iosb, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ,
        FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT |
        FILE_NON_DIRECTORY_FILE, NULL, 0);
    if (status < 0) {
        SetLastError(ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }
    return file;
}

static BOOL validate_regular_file(HANDLE file, FILE_STANDARD_INFO *standard)
{
    FILE_ATTRIBUTE_TAG_INFO tag;
    if (GetFileType(file) != FILE_TYPE_DISK ||
        !GetFileInformationByHandleEx(file, FileAttributeTagInfo, &tag, sizeof(tag)) ||
        !GetFileInformationByHandleEx(file, FileStandardInfo, standard, sizeof(*standard)) ||
        (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        standard->Directory || standard->DeletePending ||
        standard->NumberOfLinks != 1 || standard->EndOfFile.QuadPart < 0 ||
        (ULONGLONG)standard->EndOfFile.QuadPart > MAX_PAYLOAD) {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    return TRUE;
}

static BOOL open_and_pin_root(PSID service_sid)
{
    HANDLE root = INVALID_HANDLE_VALUE;
    FILE_ATTRIBUTE_TAG_INFO tag;
    FILE_STANDARD_INFO standard;
    PSID owner = NULL;
    PACL dacl = NULL;
    PSECURITY_DESCRIPTOR sd = NULL;
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    DWORD e;

    root = CreateFileW(ROOT_PATH,
        FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | READ_CONTROL | SYNCHRONIZE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (root == INVALID_HANDLE_VALUE)
        return FALSE;

    if (GetFileType(root) != FILE_TYPE_DISK ||
        !GetFileInformationByHandleEx(root, FileAttributeTagInfo, &tag, sizeof(tag)) ||
        !GetFileInformationByHandleEx(root, FileStandardInfo, &standard, sizeof(standard)) ||
        !(tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        !standard.Directory || standard.DeletePending) {
        SetLastError(ERROR_REPARSE_TAG_INVALID);
        CloseHandle(root);
        return FALSE;
    }

    e = GetSecurityInfo(root, SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        &owner, NULL, &dacl, NULL, &sd);
    if (e != ERROR_SUCCESS || owner == NULL || !EqualSid(owner, service_sid) ||
        dacl == NULL ||
        !GetSecurityDescriptorControl(sd, &control, &revision) ||
        !(control & SE_DACL_PROTECTED)) {
        if (sd) LocalFree(sd);
        CloseHandle(root);
        SetLastError(e == ERROR_SUCCESS ? ERROR_ACCESS_DENIED : e);
        return FALSE;
    }
    LocalFree(sd);
    g_root_handle = root;
    return TRUE;
}

/*
 * A protected-DACL bit is not itself a security policy. Require exactly the
 * four ACEs provisioned by the fixture: service/SYSTEM/Administrators full
 * control and the authorized client read-only. Reject broad or unknown ACEs.
 */
static BOOL validate_root_dacl(HANDLE root, PSID service_sid, PSID client_sid)
{
    PSID owner = NULL;
    PACL dacl = NULL;
    PSECURITY_DESCRIPTOR sd = NULL;
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    ACL_SIZE_INFORMATION info;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    PSID system_sid = NULL;
    PSID admins_sid = NULL;
    BOOL seen_service = FALSE, seen_system = FALSE;
    BOOL seen_admins = FALSE, seen_client = FALSE;
    BOOL ok = FALSE;
    DWORD e = GetSecurityInfo(root, SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        &owner, NULL, &dacl, NULL, &sd);
    if (e != ERROR_SUCCESS || owner == NULL || !EqualSid(owner, service_sid) ||
        dacl == NULL || !GetSecurityDescriptorControl(sd, &control, &revision) ||
        !(control & SE_DACL_PROTECTED) ||
        !GetAclInformation(dacl, &info, sizeof(info), AclSizeInformation) ||
        info.AceCount != 4)
        goto done;
    if (!AllocateAndInitializeSid(&nt, 1, SECURITY_LOCAL_SYSTEM_RID,
            0, 0, 0, 0, 0, 0, 0, &system_sid) ||
        !AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID,
            DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admins_sid))
        goto done;
    for (DWORD i = 0; i < info.AceCount; ++i) {
        ACE_HEADER *header = NULL;
        ACCESS_ALLOWED_ACE *ace;
        PSID trustee;
        BOOL *seen = NULL;
        ACCESS_MASK expected_mask = 0;
        if (!GetAce(dacl, i, (LPVOID *)&header) ||
            header->AceType != ACCESS_ALLOWED_ACE_TYPE ||
            header->AceFlags != (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE))
            goto done;
        ace = (ACCESS_ALLOWED_ACE *)header;
        trustee = (PSID)&ace->SidStart;
        if (EqualSid(trustee, service_sid)) {
            seen = &seen_service;
            expected_mask = FILE_ALL_ACCESS;
        } else if (EqualSid(trustee, system_sid)) {
            seen = &seen_system;
            expected_mask = FILE_ALL_ACCESS;
        } else if (EqualSid(trustee, admins_sid)) {
            seen = &seen_admins;
            expected_mask = FILE_ALL_ACCESS;
        } else if (EqualSid(trustee, client_sid)) {
            seen = &seen_client;
            expected_mask = FILE_READ_DATA | FILE_READ_ATTRIBUTES |
                READ_CONTROL | SYNCHRONIZE | FILE_LIST_DIRECTORY | FILE_TRAVERSE;
        } else {
            goto done;
        }
        if (*seen || ace->Mask != expected_mask)
            goto done;
        *seen = TRUE;
    }
    ok = seen_service && seen_system && seen_admins && seen_client;
done:
    if (system_sid) FreeSid(system_sid);
    if (admins_sid) FreeSid(admins_sid);
    if (sd) LocalFree(sd);
    if (!ok) SetLastError(e == ERROR_SUCCESS ? ERROR_ACCESS_DENIED : e);
    return ok;
}

static BOOL read_policy_from_root(HANDLE root, PSID *sid, DWORD *session)
{
    HANDLE file = open_relative_regular(root, L"client.policy", GENERIC_READ);
    FILE_STANDARD_INFO standard;
    char buffer[256] = {0};
    DWORD n = 0;
    char *newline, *end;
    BOOL ok = FALSE;
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    if (!validate_regular_file(file, &standard) ||
        standard.EndOfFile.QuadPart >= (LONGLONG)sizeof(buffer) ||
        !ReadFile(file, buffer, (DWORD)standard.EndOfFile.QuadPart, &n, NULL) ||
        n != (DWORD)standard.EndOfFile.QuadPart)
        goto done;
    buffer[n] = '\0';
    newline = strchr(buffer, '\n');
    if (!newline)
        goto done;
    *newline++ = '\0';
    end = strchr(newline, '\n');
    if (end)
        *end = '\0';
    if (!ConvertStringSidToSidA(buffer, sid))
        goto done;
    {
        char *parse_end = NULL;
        unsigned long value = strtoul(newline, &parse_end, 10);
        if (!parse_end || *parse_end || value > 0xffffffffUL) {
            LocalFree(*sid);
            *sid = NULL;
            goto done;
        }
        *session = (DWORD)value;
    }
    ok = TRUE;
done:
    CloseHandle(file);
    if (!ok && GetLastError() == ERROR_SUCCESS)
        SetLastError(ERROR_INVALID_DATA);
    return ok;
}

static BOOL read_payload_from_root(HANDLE root, BYTE *data, DWORD *length)
{
    HANDLE file = open_relative_regular(root, L"payload.txt", FILE_READ_DATA);
    FILE_STANDARD_INFO standard;
    DWORD n = 0;
    BOOL ok = FALSE;
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    if (!validate_regular_file(file, &standard) ||
        !ReadFile(file, data, (DWORD)standard.EndOfFile.QuadPart, &n, NULL) ||
        n != (DWORD)standard.EndOfFile.QuadPart)
        goto done;
    *length = n;
    ok = TRUE;
done:
    CloseHandle(file);
    return ok;
}
typedef BOOL (WINAPI *RayaGetNamedPipeClientProcessIdFn)(HANDLE, PULONG);

static BOOL query_token_identity(
    HANDLE token,
    PSID *sid_out,
    DWORD *session_out)
{
    TOKEN_USER *user = NULL;
    DWORD bytes = 0;
    BOOL ok = FALSE;

    *sid_out = NULL;
    (void)GetTokenInformation(token, TokenUser, NULL, 0, &bytes);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0)
        return FALSE;
    user = (TOKEN_USER *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
    if (user == NULL)
        return FALSE;
    if (!GetTokenInformation(token, TokenUser, user, bytes, &bytes))
        goto done;
    *sid_out = HeapAlloc(GetProcessHeap(), 0, GetLengthSid(user->User.Sid));
    if (*sid_out == NULL ||
        !CopySid(GetLengthSid(user->User.Sid), *sid_out, user->User.Sid))
        goto done;
    bytes = sizeof(*session_out);
    if (!GetTokenInformation(token, TokenSessionId, session_out, bytes, &bytes))
        goto done;
    ok = TRUE;

done:
    if (!ok && *sid_out != NULL) {
        HeapFree(GetProcessHeap(), 0, *sid_out);
        *sid_out = NULL;
    }
    if (user != NULL)
        HeapFree(GetProcessHeap(), 0, user);
    return ok;
}

static BOOL authenticate_client(
    HANDLE pipe,
    PSID expected,
    DWORD expected_session)
{
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    RayaGetNamedPipeClientProcessIdFn get_client_pid =
        kernel32 ? (RayaGetNamedPipeClientProcessIdFn)
            GetProcAddress(kernel32, "GetNamedPipeClientProcessId") : NULL;
    HANDLE impersonated_token = NULL;
    PSID pipe_sid = NULL;
    DWORD pipe_session = 0;
    DWORD peer_session = 0;
    ULONG peer_pid = 0;
    BOOL impersonating = FALSE;
    BOOL ok = FALSE;

    /* Fail closed if the OS cannot identify the peer process on this pipe. */
    if (expected == NULL || !IsValidSid(expected) ||
        get_client_pid == NULL ||
        !get_client_pid(pipe, &peer_pid) || peer_pid == 0 ||
        !ProcessIdToSessionId((DWORD)peer_pid, &peer_session))
        goto done;

    /* The impersonation token is the authoritative security identity of the
       named-pipe peer; do not trust any PID/SID/session supplied in a frame. */
    if (!ImpersonateNamedPipeClient(pipe))
        goto done;
    impersonating = TRUE;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE,
            &impersonated_token))
        goto done;
    if (!query_token_identity(impersonated_token, &pipe_sid, &pipe_session))
        goto done;

    ok = EqualSid(pipe_sid, expected) &&
        pipe_session == expected_session &&
        peer_session == pipe_session;

done:
    if (impersonating && !RevertToSelf()) {
        /* Continuing while still impersonating would violate the service
           security boundary. Terminate rather than reuse this worker thread. */
        TerminateProcess(GetCurrentProcess(), ERROR_CANNOT_IMPERSONATE);
        ExitProcess(ERROR_CANNOT_IMPERSONATE);
    }
    if (pipe_sid != NULL)
        HeapFree(GetProcessHeap(), 0, pipe_sid);
    if (impersonated_token != NULL)
        CloseHandle(impersonated_token);
    if (!ok)
        SetLastError(ERROR_ACCESS_DENIED);
    return ok;
}
static BOOL wait_io(HANDLE pipe,OVERLAPPED *ov,DWORD timeout,DWORD *transferred){
    HANDLE waits[2];DWORD count=0,wr;
    if(g_stop_event)waits[count++]=g_stop_event;
    waits[count++]=ov->hEvent;
    wr=WaitForMultipleObjects(count,waits,FALSE,timeout);
    if((g_stop_event&&wr==WAIT_OBJECT_0)||wr==WAIT_TIMEOUT){
        CancelIoEx(pipe,ov);
        WaitForSingleObject(ov->hEvent,INFINITE);
        GetOverlappedResult(pipe,ov,transferred,FALSE);
        SetLastError(wr==WAIT_TIMEOUT?ERROR_TIMEOUT:ERROR_OPERATION_ABORTED);
        return FALSE;
    }
    if(wr!=WAIT_OBJECT_0+(g_stop_event?1:0)){
        CancelIoEx(pipe,ov);
        WaitForSingleObject(ov->hEvent,INFINITE);
        GetOverlappedResult(pipe,ov,transferred,FALSE);
        return FALSE;
    }
    return GetOverlappedResult(pipe,ov,transferred,FALSE);
}
static BOOL io_event_init(OVERLAPPED *ov){
    ZeroMemory(ov,sizeof(*ov));
    ov->hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);
    return ov->hEvent!=NULL;
}
static BOOL wait_for_response_ack(HANDLE p){
    static const char ack[]="RAYA/2 ACK\\n";
    char received[32]={0};DWORD n=0;BOOL ok=FALSE;
    OVERLAPPED ov;
    if(!io_event_init(&ov))return FALSE;
    if(ReadFile(p,received,sizeof(received),&n,&ov))ok=TRUE;
    else if(GetLastError()==ERROR_IO_PENDING)ok=wait_io(p,&ov,5000,&n);
    if(ok)ok=(n==sizeof(ack)-1)&&memcmp(received,ack,sizeof(ack)-1)==0;
    CloseHandle(ov.hEvent);return ok;
}
static BOOL send_text(HANDLE p,const char *s,DWORD n){
    OVERLAPPED ov;DWORD w=0;BOOL ok=FALSE;
    if(!io_event_init(&ov))return FALSE;
    if(WriteFile(p,s,n,&w,&ov))ok=(w==n);
    else if(GetLastError()==ERROR_IO_PENDING)ok=wait_io(p,&ov,2000,&w)&&w==n;
    CloseHandle(ov.hEvent);
    if(!ok)return FALSE;
    return wait_for_response_ack(p);
}
#define SEND_LITERAL(p, s) send_text((p),(s),(DWORD)(sizeof(s)-1))
static BOOL connect_with_poll(HANDLE p){
    OVERLAPPED ov;DWORD ignored=0;BOOL ok=FALSE;
    if(!io_event_init(&ov))return FALSE;
    if(ConnectNamedPipe(p,&ov))ok=TRUE;
    else{
        DWORD e=GetLastError();
        if(e==ERROR_PIPE_CONNECTED)ok=TRUE;
        else if(e==ERROR_IO_PENDING)ok=wait_io(p,&ov,10000,&ignored);
    }
    CloseHandle(ov.hEvent);return ok;
}
static BOOL read_request_with_deadline(HANDLE p,char *req,DWORD *got){
    OVERLAPPED ov;DWORD n=0;BOOL ok=FALSE;
    if(!io_event_init(&ov))return FALSE;
    if(ReadFile(p,req,MAX_REQUEST,&n,&ov))ok=(n>0);
    else{
        DWORD e=GetLastError();
        if(e==ERROR_IO_PENDING){
            ok=wait_io(p,&ov,2000,&n);
            if(!ok&&GetLastError()==ERROR_MORE_DATA){*got=n;ok=FALSE;}
            else ok=ok&&(n>0);
        }else if(e==ERROR_MORE_DATA){*got=n;ok=FALSE;}
    }
    *got=n;CloseHandle(ov.hEvent);return ok;
}
static void serve_one(HANDLE p,PSID expected,DWORD session){
    char req[MAX_REQUEST+1]={0};DWORD got=0;
    if(!read_request_with_deadline(p,req,&got)||!got||got>=MAX_REQUEST){SEND_LITERAL(p,"RAYA/2 ERR BAD_REQUEST\n");return;}
    if(!authenticate_client(p,expected,session)){SEND_LITERAL(p,"RAYA/2 ERR UNAUTHORIZED\n");return;}
    req[got]=0;
    if(strcmp(req,"RAYA/2 READ fixture\n")!=0){
        if(strcmp(req,"RAYA/2 READ unknown\n")==0)SEND_LITERAL(p,"RAYA/2 ERR ROOT_NOT_FOUND\n");
        else SEND_LITERAL(p,"RAYA/2 ERR BAD_REQUEST\n");
        return;
    }
    BYTE data[MAX_PAYLOAD];DWORD n=0;
    if(g_root_handle==INVALID_HANDLE_VALUE||!read_payload_from_root(g_root_handle,data,&n)){
        SEND_LITERAL(p,"RAYA/2 ERR POLICY_OR_IO\n");return;
    }
    char header[64];int hn=_snprintf_s(header,sizeof(header),_TRUNCATE,"RAYA/2 OK %lu\n",(unsigned long)n);
    BYTE wire[64+MAX_PAYLOAD];
    if(hn>0&&(DWORD)hn+n<=sizeof(wire)){
        memcpy(wire,header,(size_t)hn);memcpy(wire+hn,data,n);
        send_text(p,(const char*)wire,(DWORD)hn+n);
    }
}
static void WINAPI control(DWORD c){
    if(c==SERVICE_CONTROL_STOP||c==SERVICE_CONTROL_SHUTDOWN){
        g_status.dwCurrentState=SERVICE_STOP_PENDING;g_status.dwWaitHint=5000;SetServiceStatus(g_status_handle,&g_status);
        if(g_stop_event)SetEvent(g_stop_event);
        if(g_service_thread)CancelSynchronousIo(g_service_thread);
    }
}
static void WINAPI service_main(DWORD argc,LPWSTR *argv){
    (void)argc;(void)argv;g_status_handle=RegisterServiceCtrlHandlerW(SERVICE_NAME,control);if(!g_status_handle)return;
    if(!DuplicateHandle(GetCurrentProcess(),GetCurrentThread(),GetCurrentProcess(),&g_service_thread,THREAD_TERMINATE,FALSE,0))return;
    ZeroMemory(&g_status,sizeof(g_status));g_status.dwServiceType=SERVICE_WIN32_OWN_PROCESS;
    g_status.dwControlsAccepted=SERVICE_ACCEPT_STOP|SERVICE_ACCEPT_SHUTDOWN;g_status.dwCurrentState=SERVICE_START_PENDING;
    g_status.dwWaitHint=10000;SetServiceStatus(g_status_handle,&g_status);
    PSID expected=NULL,svc=NULL;DWORD session=0;PSECURITY_DESCRIPTOR psd=NULL;HANDLE pipe=INVALID_HANDLE_VALUE;
    if(!lookup_sid(SERVICE_ACCOUNT,&svc)||!verify_service_identity(svc)||
       !open_and_pin_root(svc)||
       !read_policy_from_root(g_root_handle,&expected,&session)||
       !validate_root_dacl(g_root_handle,svc,expected))goto failed;
    LPWSTR svctext=NULL;LPWSTR usertext=NULL;
    if(!ConvertSidToStringSidW(svc,&svctext))goto failed;
    if(!ConvertSidToStringSidW(expected,&usertext)){LocalFree(svctext);goto failed;}
    wchar_t sddl[512];
    _snwprintf_s(sddl,512,_TRUNCATE,L"D:P(A;;GA;;;SY)(A;;GA;;;%ls)(A;;GRGW;;;%ls)",svctext,usertext);
    LocalFree(usertext);LocalFree(svctext);
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl,SDDL_REVISION_1,&psd,NULL))goto failed;
    g_stop_event=CreateEventW(NULL,TRUE,FALSE,NULL);if(!g_stop_event)goto failed;
    SECURITY_ATTRIBUTES sa={sizeof(sa),psd,FALSE};
    pipe=CreateNamedPipeW(PIPE_NAME,PIPE_ACCESS_DUPLEX|FILE_FLAG_FIRST_PIPE_INSTANCE|FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,MAX_PAYLOAD+128,MAX_REQUEST,1000,&sa);
    if(pipe==INVALID_HANDLE_VALUE)goto failed;
    g_status.dwCurrentState=SERVICE_RUNNING;g_status.dwWin32ExitCode=NO_ERROR;g_status.dwWaitHint=0;SetServiceStatus(g_status_handle,&g_status);
    while(WaitForSingleObject(g_stop_event,0)==WAIT_TIMEOUT){
        BOOL connected=connect_with_poll(pipe);
        if(connected&&WaitForSingleObject(g_stop_event,0)==WAIT_TIMEOUT)serve_one(pipe,expected,session);
        DisconnectNamedPipe(pipe);
    }
    if(pipe!=INVALID_HANDLE_VALUE)CloseHandle(pipe);pipe=INVALID_HANDLE_VALUE;
    if(g_stop_event)CloseHandle(g_stop_event);g_stop_event=NULL;
    if(g_service_thread)CloseHandle(g_service_thread);g_service_thread=NULL;
    if(g_root_handle!=INVALID_HANDLE_VALUE)CloseHandle(g_root_handle);g_root_handle=INVALID_HANDLE_VALUE;
    if(psd)LocalFree(psd);if(expected)LocalFree(expected);if(svc)LocalFree(svc);
    g_status.dwCurrentState=SERVICE_STOPPED;SetServiceStatus(g_status_handle,&g_status);return;
failed:
    if(pipe!=INVALID_HANDLE_VALUE)CloseHandle(pipe);if(psd)LocalFree(psd);if(expected)LocalFree(expected);if(svc)LocalFree(svc);
    if(g_root_handle!=INVALID_HANDLE_VALUE)CloseHandle(g_root_handle);g_root_handle=INVALID_HANDLE_VALUE;
    if(g_service_thread)CloseHandle(g_service_thread);g_service_thread=NULL;
    g_status.dwWin32ExitCode=GetLastError();g_status.dwCurrentState=SERVICE_STOPPED;SetServiceStatus(g_status_handle,&g_status);
}
static BOOL cleanup_one(const wchar_t *path,BOOL dir,PSID user){
    DWORD e=SetNamedSecurityInfoW((LPWSTR)path,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION,user,NULL,NULL,NULL);
    if(e!=ERROR_SUCCESS){err(L"cleanup owner",e);return FALSE;}
    DWORD z=sizeof(ACL)+(DWORD)sizeof(ACCESS_ALLOWED_ACE)+GetLengthSid(user)-sizeof(DWORD);
    PACL a=(PACL)LocalAlloc(LMEM_FIXED,z);if(!a||!InitializeAcl(a,z,ACL_REVISION)||!AddAccessAllowedAce(a,ACL_REVISION,FILE_ALL_ACCESS,user)){if(a)LocalFree(a);return FALSE;}
    e=SetNamedSecurityInfoW((LPWSTR)path,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,NULL,NULL,a,NULL);LocalFree(a);
    if(e!=ERROR_SUCCESS)return FALSE;
    return dir?RemoveDirectoryW(path):DeleteFileW(path);
}
static BOOL cleanup_fixture(void){
    HANDLE t=NULL;PSID user=NULL;DWORD s=0;BOOL ok=FALSE;
    if(!current_user(&user,&s)||!OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&t)||!set_privilege(t,SE_RESTORE_NAME))goto done;
    if(GetFileAttributesW(PAYLOAD_PATH)!=INVALID_FILE_ATTRIBUTES&&!cleanup_one(PAYLOAD_PATH,FALSE,user))goto done;
    if(GetFileAttributesW(POLICY_PATH)!=INVALID_FILE_ATTRIBUTES&&!cleanup_one(POLICY_PATH,FALSE,user))goto done;
    if(GetFileAttributesW(ROOT_PATH)!=INVALID_FILE_ATTRIBUTES&&!cleanup_one(ROOT_PATH,TRUE,user))goto done;
    if(GetFileAttributesW(OLD_PAYLOAD_PATH)!=INVALID_FILE_ATTRIBUTES&&!cleanup_one(OLD_PAYLOAD_PATH,FALSE,user))goto done;
    if(GetFileAttributesW(OLD_POLICY_PATH)!=INVALID_FILE_ATTRIBUTES&&!cleanup_one(OLD_POLICY_PATH,FALSE,user))goto done;
    if(GetFileAttributesW(ROOT_OLD_PATH)!=INVALID_FILE_ATTRIBUTES&&!cleanup_one(ROOT_OLD_PATH,TRUE,user))goto done;
    if(GetFileAttributesW(BIN_EXE_PATH)!=INVALID_FILE_ATTRIBUTES&&!cleanup_one(BIN_EXE_PATH,FALSE,user))goto done;
    if(GetFileAttributesW(BIN_PATH)!=INVALID_FILE_ATTRIBUTES&&!cleanup_one(BIN_PATH,TRUE,user))goto done;
    wprintf(L"CLEANUP_COMPLETE\n");ok=TRUE;
done:if(t)CloseHandle(t);if(user)LocalFree(user);return ok;
}

static BOOL client_case(const char *request,const char *expected_error,BOOL valid_read){
    if(!WaitNamedPipeW(PIPE_NAME,5000)){err(L"WaitNamedPipe",GetLastError());return FALSE;}
    HANDLE h=CreateFileW(PIPE_NAME,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE){err(L"Open named pipe",GetLastError());return FALSE;}
    DWORD mode=PIPE_READMODE_MESSAGE;
    if(!SetNamedPipeHandleState(h,&mode,NULL,NULL)){err(L"Set pipe message mode",GetLastError());CloseHandle(h);return FALSE;}
    DWORD n=0,w=0;BOOL ok=WriteFile(h,request,(DWORD)strlen(request),&w,NULL)&&w==strlen(request);
    char response[512]={0};
    if(ok)ok=ReadFile(h,response,sizeof(response)-1,&n,NULL);
    if(!ok){err(L"Read response",GetLastError());CloseHandle(h);return FALSE;}
    {
        static const char ack[]="RAYA/2 ACK\\n";
        DWORD ack_n=0;
        if(!WriteFile(h,ack,(DWORD)(sizeof(ack)-1),&ack_n,NULL)||ack_n!=sizeof(ack)-1){
            err(L"Write response ACK",GetLastError());CloseHandle(h);return FALSE;
        }
    }
    response[n]=0;
    if(valid_read){
        char expected_payload[]="Raya isolated Broker v2 pinned-root read-only fixture\n";
        unsigned long declared=0;char extra=0;
        int parsed=sscanf_s(response,"RAYA/2 OK %lu%c",&declared,&extra,1);
        char *payload=strchr(response,'\n');
        if(parsed!=2||extra!='\n'||!payload||declared!=sizeof(expected_payload)-1)ok=FALSE;
        if(ok){payload++;ok=(DWORD)(response+n-payload)==declared&&memcmp(payload,expected_payload,declared)==0;}
    }else if(strcmp(response,expected_error)!=0)ok=FALSE;
    CloseHandle(h);return ok;
}
static int client_test(void){
    BOOL a=client_case("RAYA/2 READ fixture\n","",TRUE);
    wprintf(L"%ls read-only configured root\n",a?L"PASS":L"FAIL");
    BOOL b=client_case("RAYA/2 READ unknown\n","RAYA/2 ERR ROOT_NOT_FOUND\n",FALSE);
    wprintf(L"%ls unknown root rejected\n",b?L"PASS":L"FAIL");
    BOOL c=client_case("RAYA/2 DELETE fixture\n","RAYA/2 ERR BAD_REQUEST\n",FALSE);
    wprintf(L"%ls unsupported operation rejected\n",c?L"PASS":L"FAIL");
    BOOL d=client_case("RAYA/2 READ fixture C:\\Windows\\win.ini\n","RAYA/2 ERR BAD_REQUEST\n",FALSE);
    wprintf(L"%ls caller-supplied absolute path rejected\n",d?L"PASS":L"FAIL");
    return a&&b&&c&&d?0:2;
}

static int peer_auth_client(const wchar_t *pipe_name)
{
    HANDLE pipe = INVALID_HANDLE_VALUE;
    BYTE marker = 0x5A;
    BYTE ack = 0;
    DWORD written = 0;
    DWORD received = 0;

    pipe = CreateFileW(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
        OPEN_EXISTING, SECURITY_SQOS_PRESENT | SECURITY_IMPERSONATION, NULL);
    if (pipe == INVALID_HANDLE_VALUE)
        return 10;
    for (unsigned i = 0; i < 3; ++i) {
        marker = (BYTE)(0x5Au + i);
        if (!WriteFile(pipe, &marker, sizeof(marker), &written, NULL) ||
            written != sizeof(marker)) {
            CloseHandle(pipe);
            return 11;
        }
    }
    if (!ReadFile(pipe, &ack, sizeof(ack), &received, NULL) ||
        received != sizeof(ack) || ack != 0xA5) {
        CloseHandle(pipe);
        return 12;
    }
    CloseHandle(pipe);
    return 0;
}

static BOOL peer_auth_check(const wchar_t *label, BOOL condition)
{
    wprintf(L"%ls %ls\n", condition ? L"PASS" : L"FAIL", label);
    return condition;
}

static int peer_auth_test(void)
{
    wchar_t exe[MAX_PATH];
    wchar_t pipe_name[256];
    wchar_t command[1024];
    DWORD exe_length;
    DWORD pid = GetCurrentProcessId();
    DWORD tick = GetTickCount();
    HANDLE pipe = INVALID_HANDLE_VALUE;
    HANDLE child = NULL;
    HANDLE child_thread = NULL;
    HANDLE token = NULL;
    PSID expected_sid = NULL;
    PSID wrong_sid = NULL;
    DWORD expected_session = 0;
    DWORD wrong_sid_size = SECURITY_MAX_SID_SIZE;
    DWORD peer_pid = 0;
    DWORD child_exit = STILL_ACTIVE;
    BYTE markers[3] = { 0 };
    BYTE ack = 0xA5;
    DWORD transferred = 0;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    BOOL connected = FALSE;
    BOOL all_ok = TRUE;
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    RayaGetNamedPipeClientProcessIdFn get_client_pid =
        kernel32 ? (RayaGetNamedPipeClientProcessIdFn)
            GetProcAddress(kernel32, "GetNamedPipeClientProcessId") : NULL;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) ||
        !query_token_identity(token, &expected_sid, &expected_session)) {
        all_ok = peer_auth_check(L"query expected local token identity", FALSE);
        goto done;
    }
    (void)CloseHandle(token);
    token = NULL;

    wrong_sid = HeapAlloc(GetProcessHeap(), 0, wrong_sid_size);
    if (wrong_sid == NULL ||
        !CreateWellKnownSid(WinLocalServiceSid, NULL, wrong_sid, &wrong_sid_size)) {
        all_ok = peer_auth_check(L"create non-matching expected SID", FALSE);
        goto done;
    }

    exe_length = GetModuleFileNameW(NULL, exe, _countof(exe));
    if (exe_length == 0 || exe_length >= _countof(exe) ||
        swprintf_s(pipe_name, _countof(pipe_name),
            L"\\\\.\\pipe\\RayaPeerAuth-%lu-%lu",
            (unsigned long)pid, (unsigned long)tick) < 0) {
        all_ok = peer_auth_check(L"construct disposable peer-auth fixture", FALSE);
        goto done;
    }

    pipe = CreateNamedPipeW(pipe_name,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
            PIPE_REJECT_REMOTE_CLIENTS,
        1, 64, 64, 5000, NULL);
    if (pipe == INVALID_HANDLE_VALUE) {
        all_ok = peer_auth_check(L"create disposable local named pipe", FALSE);
        goto done;
    }

    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    if (swprintf_s(command, _countof(command),
            L"\"%ls\" --peer-auth-client \"%ls\"", exe, pipe_name) < 0 ||
        !CreateProcessW(exe, command, NULL, NULL, FALSE,
            CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        all_ok = peer_auth_check(L"launch disposable local pipe client", FALSE);
        goto done;
    }
    child = pi.hProcess;
    child_thread = pi.hThread;

    if (ConnectNamedPipe(pipe, NULL))
        connected = TRUE;
    else
        connected = GetLastError() == ERROR_PIPE_CONNECTED;
    all_ok &= peer_auth_check(L"local peer connected", connected);
    if (!connected)
        goto done;

    if (get_client_pid != NULL)
        connected = get_client_pid(pipe, &peer_pid) && peer_pid == pi.dwProcessId;
    else
        connected = FALSE;
    all_ok &= peer_auth_check(L"OS-reported pipe client PID matches created child",
        connected);

    for (unsigned i = 0; i < 3; ++i) {
        BOOL read_ok = ReadFile(pipe, &markers[i], sizeof(markers[i]),
            &transferred, NULL) && transferred == sizeof(markers[i]) &&
            markers[i] == (BYTE)(0x5Au + i);
        all_ok &= peer_auth_check(L"receive peer identity challenge marker", read_ok);
        if (!read_ok)
            goto done;

        if (i == 0) {
            all_ok &= peer_auth_check(L"authorized peer SID/session accepted",
                authenticate_client(pipe, expected_sid, expected_session));
        } else if (i == 1) {
            all_ok &= peer_auth_check(L"mismatched peer SID rejected",
                !authenticate_client(pipe, wrong_sid, expected_session));
        } else {
            all_ok &= peer_auth_check(L"mismatched peer session rejected",
                !authenticate_client(pipe, expected_sid, expected_session + 1u));
        }
    }

    if (!WriteFile(pipe, &ack, sizeof(ack), &transferred, NULL) ||
        transferred != sizeof(ack)) {
        all_ok &= peer_auth_check(L"release peer-auth child", FALSE);
        goto done;
    }
    if (WaitForSingleObject(child, 5000) != WAIT_OBJECT_0 ||
        !GetExitCodeProcess(child, &child_exit) || child_exit != 0) {
        all_ok &= peer_auth_check(L"peer-auth child exits successfully", FALSE);
        goto done;
    }
    all_ok &= peer_auth_check(L"peer-auth child exits successfully", TRUE);

done:
    if (child != NULL && WaitForSingleObject(child, 0) == WAIT_TIMEOUT) {
        TerminateProcess(child, 99);
        (void)WaitForSingleObject(child, 5000);
    }
    if (child_thread != NULL)
        CloseHandle(child_thread);
    if (child != NULL)
        CloseHandle(child);
    if (pipe != INVALID_HANDLE_VALUE)
        CloseHandle(pipe);
    if (token != NULL)
        CloseHandle(token);
    if (expected_sid != NULL)
        HeapFree(GetProcessHeap(), 0, expected_sid);
    if (wrong_sid != NULL)
        HeapFree(GetProcessHeap(), 0, wrong_sid);
    wprintf(L"PEER_AUTH_INTEGRATION_TESTS: %ls\n",
        all_ok ? L"PASS" : L"FAIL");
    return all_ok ? 0 : 1;
}
int wmain(int argc,wchar_t **argv){
    if(argc==2&&!wcscmp(argv[1],L"--peer-auth-test"))return peer_auth_test();
    if(argc==3&&!wcscmp(argv[1],L"--peer-auth-client"))return peer_auth_client(argv[2]);
    if(argc==2&&!wcscmp(argv[1],L"--prepare"))return prepare_fixture()?0:1;
    if(argc==2&&!wcscmp(argv[1],L"--install-image"))return install_image()?0:1;
    if(argc==2&&!wcscmp(argv[1],L"--cleanup"))return cleanup_fixture()?0:1;
    if(argc==2&&!wcscmp(argv[1],L"--client-test"))return client_test();
    if(argc==2&&!wcscmp(argv[1],L"--service")){
        SERVICE_TABLE_ENTRYW table[]={{(LPWSTR)SERVICE_NAME,service_main},{NULL,NULL}};
        if(!StartServiceCtrlDispatcherW(table)){err(L"StartServiceCtrlDispatcher",GetLastError());return 1;}return 0;
    }
    return 64;
}