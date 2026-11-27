#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#pragma comment(lib, "Advapi32.lib")

static unsigned g_pass = 0;
static unsigned g_fail = 0;
static unsigned g_skip = 0;

static void report(const wchar_t *name, BOOL ok, DWORD error)
{
    if (ok) {
        ++g_pass;
        wprintf(L"PASS %ls\n", name);
    } else {
        ++g_fail;
        wprintf(L"FAIL %ls (error=%lu)\n", name, (unsigned long)error);
    }
}

static void skip_test(const wchar_t *name, DWORD error)
{
    ++g_skip;
    wprintf(L"SKIP %ls (error=%lu)\n", name, (unsigned long)error);
}

static BOOL get_current_user_sid_string(wchar_t *buffer, size_t cch)
{
    HANDLE token = NULL;
    DWORD bytes = 0;
    TOKEN_USER *user = NULL;
    LPWSTR sid_text = NULL;
    BOOL ok = FALSE;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return FALSE;

    (void)GetTokenInformation(token, TokenUser, NULL, 0, &bytes);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0)
        goto cleanup;

    user = (TOKEN_USER *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
    if (user == NULL)
        goto cleanup;

    if (!GetTokenInformation(token, TokenUser, user, bytes, &bytes))
        goto cleanup;

    if (!ConvertSidToStringSidW(user->User.Sid, &sid_text))
        goto cleanup;

    if (wcslen(sid_text) + 1 > cch)
        goto cleanup;

    wcscpy_s(buffer, cch, sid_text);
    ok = TRUE;

cleanup:
    if (sid_text != NULL)
        LocalFree(sid_text);
    if (user != NULL)
        HeapFree(GetProcessHeap(), 0, user);
    if (token != NULL)
        CloseHandle(token);
    return ok;
}

static HANDLE create_pipe(const wchar_t *name, const wchar_t *sddl, DWORD mode)
{
    PSECURITY_DESCRIPTOR descriptor = NULL;
    SECURITY_ATTRIBUTES sa;
    HANDLE pipe = INVALID_HANDLE_VALUE;

    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = FALSE;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl, SDDL_REVISION_1, &descriptor, NULL)) {
        wprintf(L"SETUP ConvertStringSecurityDescriptor failed: %lu\n",
                (unsigned long)GetLastError());
        return INVALID_HANDLE_VALUE;
    }

    sa.lpSecurityDescriptor = descriptor;
    pipe = CreateNamedPipeW(
        name,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | mode,
        1,
        256,
        256,
        1000,
        &sa);

    LocalFree(descriptor);
    return pipe;
}

static BOOL begin_connect(HANDLE pipe, OVERLAPPED *ov, HANDLE *event_handle,
                          BOOL *completed)
{
    DWORD error;
    ZeroMemory(ov, sizeof(*ov));
    *event_handle = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (*event_handle == NULL)
        return FALSE;
    ov->hEvent = *event_handle;
    if (ConnectNamedPipe(pipe, ov)) {
        *completed = TRUE;
        return TRUE;
    }
    error = GetLastError();
    if (error == ERROR_IO_PENDING) {
        *completed = FALSE;
        return TRUE;
    }
    if (error == ERROR_PIPE_CONNECTED) {
        SetEvent(*event_handle);
        *completed = TRUE;
        return TRUE;
    }
    CloseHandle(*event_handle);
    *event_handle = NULL;
    SetLastError(error);
    return FALSE;
}

static BOOL finish_connect(HANDLE pipe, OVERLAPPED *ov, HANDLE event_handle,
                           BOOL completed)
{
    DWORD transferred = 0;
    DWORD wait_result;
    if (completed)
        return TRUE;
    wait_result = WaitForSingleObject(event_handle, 5000);
    if (wait_result != WAIT_OBJECT_0) {
        DWORD ignored = 0;
        (void)CancelIoEx(pipe, ov);
        (void)GetOverlappedResult(pipe, ov, &ignored, TRUE);
        return FALSE;
    }
    return GetOverlappedResult(pipe, ov, &transferred, FALSE);
}

static void cancel_connect(HANDLE pipe, OVERLAPPED *ov, HANDLE event_handle,
                           BOOL completed)
{
    if (event_handle == NULL)
        return;
    if (!completed) {
        DWORD ignored = 0;
        (void)CancelIoEx(pipe, ov);
        (void)GetOverlappedResult(pipe, ov, &ignored, TRUE);
    }
    CloseHandle(event_handle);
}

static BOOL expect_access_denied(HANDLE pipe, const wchar_t *pipe_name,
                                 const wchar_t *test_name)
{
    HANDLE restricted_sid_token = NULL;
    HANDLE impersonation_token = NULL;
    HANDLE process_token = NULL;
    PSID disabled_sid = NULL;
    SID_AND_ATTRIBUTES disabling;
    HANDLE client = INVALID_HANDLE_VALUE;
    OVERLAPPED connect_ov;
    HANDLE connect_event = NULL;
    BOOL connect_completed = FALSE;
    DWORD error = ERROR_SUCCESS;
    BOOL impersonating = FALSE;
    BOOL ok = FALSE;

    (void)pipe;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY,
                          &process_token)) {
        error = GetLastError();
        wprintf(L"DACL diagnostic: OpenProcessToken failed\n");
        report(test_name, FALSE, error);
        return FALSE;
    }

    if (!ConvertStringSidToSidW(L"S-1-5-11", &disabled_sid)) {
        error = GetLastError();
        wprintf(L"DACL diagnostic: ConvertStringSidToSid failed\n");
        report(test_name, FALSE, error);
        goto cleanup;
    }

    disabling.Sid = disabled_sid;
    disabling.Attributes = 0;

    if (!CreateRestrictedToken(process_token, DISABLE_MAX_PRIVILEGE,
                               1, &disabling, 0, NULL, 0, NULL,
                               &restricted_sid_token)) {
        error = GetLastError();
        wprintf(L"DACL diagnostic: CreateRestrictedToken failed\n");
        report(test_name, FALSE, error);
        goto cleanup;
    }

    if (!DuplicateTokenEx(restricted_sid_token, MAXIMUM_ALLOWED, NULL,
                          SecurityImpersonation, TokenImpersonation,
                          &impersonation_token)) {
        error = GetLastError();
        wprintf(L"DACL diagnostic: DuplicateTokenEx failed\n");
        report(test_name, FALSE, error);
        goto cleanup;
    }

    if (!begin_connect(pipe, &connect_ov, &connect_event, &connect_completed)) {
        error = GetLastError();
        wprintf(L"DACL diagnostic: begin_connect failed\n");
        report(test_name, FALSE, error);
        goto cleanup;
    }

    if (!ImpersonateLoggedOnUser(impersonation_token)) {
        error = GetLastError();
        wprintf(L"DACL diagnostic: ImpersonateLoggedOnUser failed\n");
        report(test_name, FALSE, error);
        goto cleanup;
    }
    impersonating = TRUE;

    client = CreateFileW(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                         OPEN_EXISTING, SECURITY_SQOS_PRESENT |
                         SECURITY_IMPERSONATION, NULL);
    if (client == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        wprintf(L"DACL diagnostic: client CreateFile returned error %lu\n", (unsigned long)error);
        ok = (error == ERROR_ACCESS_DENIED);
        cancel_connect(pipe, &connect_ov, connect_event, connect_completed);
        connect_event = NULL;
    } else {
        error = ERROR_SUCCESS;
        ok = FALSE;
        (void)finish_connect(pipe, &connect_ov, connect_event, connect_completed);
        cancel_connect(pipe, &connect_ov, connect_event, connect_completed);
        connect_event = NULL;
        CloseHandle(client);
        client = INVALID_HANDLE_VALUE;
    }

    RevertToSelf();
    impersonating = FALSE;
    report(test_name, ok, error);

cleanup:
    if (impersonating)
        RevertToSelf();
    if (connect_event != NULL)
        cancel_connect(pipe, &connect_ov, connect_event, connect_completed);
    if (client != INVALID_HANDLE_VALUE)
        CloseHandle(client);
    if (impersonation_token != NULL)
        CloseHandle(impersonation_token);
    if (restricted_sid_token != NULL)
        CloseHandle(restricted_sid_token);
    if (disabled_sid != NULL)
        LocalFree(disabled_sid);
    if (process_token != NULL)
        CloseHandle(process_token);
    return ok;
}

int wmain(void)
{
    wchar_t pipe_name[256];
    wchar_t remote_name[512];
    wchar_t computer_name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD computer_chars = _countof(computer_name);
    DWORD pid = GetCurrentProcessId();
    DWORD tick = GetTickCount();
    wchar_t user_sid[192];
    wchar_t user_sddl[512];
    HANDLE pipe = INVALID_HANDLE_VALUE;
    HANDLE client = INVALID_HANDLE_VALUE;
    OVERLAPPED connect_ov;
    HANDLE connect_event = NULL;
    BOOL connect_completed = FALSE;
    DWORD error = ERROR_SUCCESS;

    if (!get_current_user_sid_string(user_sid, _countof(user_sid))) {
        wprintf(L"FATAL cannot read current token SID: %lu\n",
                (unsigned long)GetLastError());
        return 2;
    }

    if (swprintf_s(pipe_name, _countof(pipe_name),
                   L"\\\\.\\pipe\\RayaSupervisorSecurity-%lu-%lu",
                   (unsigned long)pid, (unsigned long)tick) < 0) {
        wprintf(L"FATAL pipe name construction failed\n");
        return 2;
    }

    wcscpy_s(user_sddl, _countof(user_sddl),
             L"D:P(A;;GA;;;AU)(A;;GA;;;SY)");

    wprintf(L"Host PID: %lu\n", (unsigned long)pid);
    wprintf(L"Pipe: %ls\n", pipe_name);
    wprintf(L"Current user SID: %ls\n", user_sid);

    /* DACL test: the server grants Authenticated Users. The client token
       disables that group, so the same process identity must be denied. */
    pipe = create_pipe(pipe_name, user_sddl, 0);
    if (pipe == INVALID_HANDLE_VALUE) {
        report(L"create DACL test pipe", FALSE, GetLastError());
        return 2;
    }
    report(L"create DACL test pipe", TRUE, ERROR_SUCCESS);

    (void)expect_access_denied(pipe, pipe_name,
                               L"restricted-token client denied by pipe DACL");
    CloseHandle(pipe);
    pipe = INVALID_HANDLE_VALUE;

    /* Remote-client test: prove ordinary local access first, then attempt
       the same pipe through the machine UNC path. The DACL allows Everyone,
       so ERROR_ACCESS_DENIED on the UNC attempt is attributable to the
       PIPE_REJECT_REMOTE_CLIENTS gate, not the DACL. */
    if (swprintf_s(pipe_name, _countof(pipe_name),
                   L"\\\\.\\pipe\\RayaSupervisorRemote-%lu-%lu",
                   (unsigned long)pid, (unsigned long)tick) < 0) {
        wprintf(L"FATAL remote pipe name construction failed\n");
        return 2;
    }

    pipe = create_pipe(pipe_name, L"D:P(A;;GA;;;WD)(A;;GA;;;SY)",
                       PIPE_REJECT_REMOTE_CLIENTS);
    if (pipe == INVALID_HANDLE_VALUE) {
        report(L"create remote-reject test pipe", FALSE, GetLastError());
        return 2;
    }
    report(L"create remote-reject test pipe", TRUE, ERROR_SUCCESS);

    if (!begin_connect(pipe, &connect_ov, &connect_event, &connect_completed)) {
        report(L"arm local pipe connection", FALSE, GetLastError());
        CloseHandle(pipe);
        return 2;
    }
    client = CreateFileW(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                         OPEN_EXISTING, SECURITY_SQOS_PRESENT |
                         SECURITY_IMPERSONATION, NULL);
    if (client == INVALID_HANDLE_VALUE) {
        report(L"local client allowed by DACL", FALSE, GetLastError());
        cancel_connect(pipe, &connect_ov, connect_event, connect_completed);
        connect_event = NULL;
    } else {
        report(L"local client allowed by DACL", TRUE, ERROR_SUCCESS);
        if (!finish_connect(pipe, &connect_ov, connect_event, connect_completed))
            report(L"server accepted local connection", FALSE, GetLastError());
        else
            report(L"server accepted local connection", TRUE, ERROR_SUCCESS);
        cancel_connect(pipe, &connect_ov, connect_event, connect_completed);
        connect_event = NULL;
        CloseHandle(client);
        client = INVALID_HANDLE_VALUE;
        if (!DisconnectNamedPipe(pipe)) {
            report(L"disconnect local client", FALSE, GetLastError());
        } else {
            report(L"disconnect local client", TRUE, ERROR_SUCCESS);
        }

        if (!GetComputerNameW(computer_name, &computer_chars)) {
            skip_test(L"remote UNC denial (computer name unavailable)",
                      GetLastError());
        } else {
            if (!begin_connect(pipe, &connect_ov, &connect_event, &connect_completed)) {
                skip_test(L"remote UNC denial (could not arm server)", GetLastError());
                connect_event = NULL;
            } else {
                if (swprintf_s(remote_name, _countof(remote_name),
                               L"\\\\%ls\\pipe\\RayaSupervisorRemote-%lu-%lu",
                               computer_name, (unsigned long)pid,
                               (unsigned long)tick) < 0) {
                    skip_test(L"remote UNC denial (path construction failed)",
                              ERROR_INVALID_DATA);
                } else {
                    client = CreateFileW(remote_name, GENERIC_READ | GENERIC_WRITE,
                                          0, NULL, OPEN_EXISTING,
                                          SECURITY_SQOS_PRESENT |
                                          SECURITY_IMPERSONATION, NULL);
                    if (client == INVALID_HANDLE_VALUE) {
                        error = GetLastError();
                        if (error == ERROR_ACCESS_DENIED) {
                            report(L"remote UNC client rejected", TRUE, error);
                        } else {
                            skip_test(L"remote UNC denial inconclusive", error);
                        }
                    } else {
                        CloseHandle(client);
                        client = INVALID_HANDLE_VALUE;
                        report(L"remote UNC client rejected", FALSE,
                               ERROR_SUCCESS);
                    }
                }
                if (connect_event != NULL) {
                    cancel_connect(pipe, &connect_ov, connect_event, connect_completed);
                    connect_event = NULL;
                }
            }
        }
    }

    if (pipe != INVALID_HANDLE_VALUE)
        CloseHandle(pipe);

    wprintf(L"RESULT pass=%u fail=%u skip=%u\n", g_pass, g_fail, g_skip);
    if (g_fail != 0)
        return 1;
    if (g_skip != 0)
        return 3;
    return 0;
}
