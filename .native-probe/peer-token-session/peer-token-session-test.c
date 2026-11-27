#define _WIN32_WINNT 0x0602
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
#define _countof(a) (sizeof(a) / sizeof((a)[0]))

#pragma comment(lib, "Advapi32.lib")

typedef struct peer_message {
    DWORD claimed_server_pid; /* deliberately forged by the child */
    DWORD observed_server_pid;
    DWORD child_flags;
} peer_message;

static unsigned g_pass, g_fail;

static void check(const wchar_t *name, BOOL ok, DWORD error)
{
    if (ok) {
        ++g_pass;
        wprintf(L"PASS %ls\n", name);
    } else {
        ++g_fail;
        wprintf(L"FAIL %ls (error=%lu)\n", name, (unsigned long)error);
    }
}

static BOOL token_facts(HANDLE process, PSID *sid_out, DWORD *session_out)
{
    HANDLE token = NULL;
    TOKEN_USER *user = NULL;
    DWORD bytes = 0;
    BOOL ok = FALSE;

    *sid_out = NULL;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token))
        return FALSE;
    (void)GetTokenInformation(token, TokenUser, NULL, 0, &bytes);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0)
        goto done;
    user = (TOKEN_USER *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
    if (user == NULL)
        goto done;
    if (!GetTokenInformation(token, TokenUser, user, bytes, &bytes))
        goto done;
    *sid_out = HeapAlloc(GetProcessHeap(), 0, GetLengthSid(user->User.Sid));
    if (*sid_out == NULL)
        goto done;
    if (!CopySid(GetLengthSid(user->User.Sid), *sid_out, user->User.Sid))
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
    if (token != NULL)
        CloseHandle(token);
    return ok;
}

static void free_sid(PSID sid)
{
    if (sid != NULL)
        HeapFree(GetProcessHeap(), 0, sid);
}

static int run_child(const wchar_t *pipe_name, DWORD expected_server_pid)
{
    HANDLE pipe = INVALID_HANDLE_VALUE;
    HANDLE server = NULL;
    PSID child_sid = NULL, server_sid = NULL;
    DWORD child_session = 0, server_session = 0, server_pid = 0;
    peer_message message;
    DWORD written = 0, read = 0;
    BYTE ack = 0;
    BOOL facts_ok, same_sid = FALSE, same_session = FALSE;

    pipe = CreateFileW(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                       OPEN_EXISTING, SECURITY_SQOS_PRESENT |
                       SECURITY_IMPERSONATION, NULL);
    if (pipe == INVALID_HANDLE_VALUE) {
        wprintf(L"CHILD CreateFile failed: %lu\n", (unsigned long)GetLastError());
        return 10;
    }

    if (!GetNamedPipeServerProcessId(pipe, &server_pid)) {
        wprintf(L"CHILD GetNamedPipeServerProcessId failed: %lu\n",
                (unsigned long)GetLastError());
        CloseHandle(pipe);
        return 11;
    }

    server = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, server_pid);
    if (server == NULL) {
        wprintf(L"CHILD OpenProcess(server) failed: %lu\n",
                (unsigned long)GetLastError());
        CloseHandle(pipe);
        return 12;
    }

    facts_ok = token_facts(GetCurrentProcess(), &child_sid, &child_session) &&
               token_facts(server, &server_sid, &server_session);
    if (facts_ok) {
        same_sid = EqualSid(child_sid, server_sid);
        same_session = (child_session == server_session);
    }

    message.claimed_server_pid = GetCurrentProcessId(); /* not the server */
    message.observed_server_pid = server_pid;
    message.child_flags = (facts_ok ? 1u : 0u) |
                          (same_sid ? 2u : 0u) |
                          (same_session ? 4u : 0u) |
                          (server_pid == expected_server_pid ? 8u : 0u);

    if (!WriteFile(pipe, &message, sizeof(message), &written, NULL) ||
        written != sizeof(message)) {
        wprintf(L"CHILD WriteFile failed: %lu\n", (unsigned long)GetLastError());
        free_sid(child_sid);
        free_sid(server_sid);
        CloseHandle(server);
        CloseHandle(pipe);
        return 13;
    }

    if (!ReadFile(pipe, &ack, sizeof(ack), &read, NULL) ||
        read != sizeof(ack) || ack != 0xA5) {
        wprintf(L"CHILD ACK failed: %lu\n", (unsigned long)GetLastError());
        free_sid(child_sid);
        free_sid(server_sid);
        CloseHandle(server);
        CloseHandle(pipe);
        return 14;
    }

    free_sid(child_sid);
    free_sid(server_sid);
    CloseHandle(server);
    CloseHandle(pipe);
    return (facts_ok && same_sid && same_session &&
            server_pid == expected_server_pid) ? 0 : 15;
}

static BOOL current_user_sddl(wchar_t *out, size_t cch)
{
    PSID sid = NULL;
    HANDLE token = NULL;
    TOKEN_USER *user = NULL;
    DWORD bytes = 0;
    LPWSTR sid_text = NULL;
    BOOL ok = FALSE;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        goto done;
    (void)GetTokenInformation(token, TokenUser, NULL, 0, &bytes);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0)
        goto done;
    user = (TOKEN_USER *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
    if (user == NULL || !GetTokenInformation(token, TokenUser, user, bytes, &bytes))
        goto done;
    sid = user->User.Sid;
    if (!ConvertSidToStringSidW(sid, &sid_text))
        goto done;
    if (swprintf_s(out, cch, L"D:P(A;;GA;;;%ls)(A;;GA;;;SY)", sid_text) < 0)
        goto done;
    ok = TRUE;

done:
    if (sid_text != NULL)
        LocalFree(sid_text);
    if (user != NULL)
        HeapFree(GetProcessHeap(), 0, user);
    if (token != NULL)
        CloseHandle(token);
    return ok;
}

int wmain(int argc, wchar_t **argv)
{
    wchar_t exe[MAX_PATH], pipe_name[256], sddl[512], command[1024];
    DWORD exe_len, pid = GetCurrentProcessId(), tick = GetTickCount();
    HANDLE pipe = INVALID_HANDLE_VALUE, child = NULL, thread = NULL;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    SECURITY_ATTRIBUTES sa;
    PSECURITY_DESCRIPTOR sd = NULL;
    HANDLE child_process = NULL;
    PSID parent_sid = NULL, child_sid = NULL;
    DWORD parent_session = 0, child_session = 0;
    DWORD client_pid = 0, read = 0, written = 0, exit_code = STILL_ACTIVE;
    peer_message message;
    BYTE ack = 0xA5;
    BOOL connected = FALSE, facts_ok = FALSE;
    int rc = 0;

    if (argc == 4 && wcscmp(argv[1], L"--client") == 0) {
        wchar_t *end = NULL;
        unsigned long expected = wcstoul(argv[3], &end, 10);
        if (end == argv[3] || *end != L'\0' || expected == 0)
            return 9;
        return run_child(argv[2], (DWORD)expected);
    }

    if (argc != 1) {
        wprintf(L"Usage: peer-token-session-test.exe\n");
        return 2;
    }

    if (!current_user_sddl(sddl, _countof(sddl))) {
        wprintf(L"FATAL cannot construct test DACL: %lu\n",
                (unsigned long)GetLastError());
        return 2;
    }
    if (swprintf_s(pipe_name, _countof(pipe_name),
                   L"\\\\.\\pipe\\RayaPeerIdentity-%lu-%lu",
                   (unsigned long)pid, (unsigned long)tick) < 0)
        return 2;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl, SDDL_REVISION_1, &sd, NULL)) {
        wprintf(L"FATAL SDDL conversion failed: %lu\n",
                (unsigned long)GetLastError());
        return 2;
    }
    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = sd;

    pipe = CreateNamedPipeW(pipe_name,
                            PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                            PIPE_REJECT_REMOTE_CLIENTS,
                            1, 256, 256, 5000, &sa);
    LocalFree(sd);
    if (pipe == INVALID_HANDLE_VALUE) {
        wprintf(L"FATAL CreateNamedPipe failed: %lu\n",
                (unsigned long)GetLastError());
        return 2;
    }

    exe_len = GetModuleFileNameW(NULL, exe, _countof(exe));
    if (exe_len == 0 || exe_len >= (DWORD)_countof(exe)) {
        rc = 2;
        goto cleanup;
    }
    if (swprintf_s(command, _countof(command),
                   L"\"%ls\" --client \"%ls\" %lu",
                   exe, pipe_name, (unsigned long)pid) < 0) {
        rc = 2;
        goto cleanup;
    }

    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    if (!CreateProcessW(exe, command, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        wprintf(L"FATAL CreateProcess failed: %lu\n",
                (unsigned long)GetLastError());
        rc = 2;
        goto cleanup;
    }
    child = pi.hProcess;
    thread = pi.hThread;

    if (ConnectNamedPipe(pipe, NULL)) {
        connected = TRUE;
    } else if (GetLastError() == ERROR_PIPE_CONNECTED) {
        connected = TRUE;
    } else {
        wprintf(L"FAIL ConnectNamedPipe: %lu\n",
                (unsigned long)GetLastError());
        rc = 1;
        goto cleanup;
    }
    check(L"child connected to local protected pipe", connected, GetLastError());

    if (!GetNamedPipeClientProcessId(pipe, &client_pid)) {
        check(L"GetNamedPipeClientProcessId", FALSE, GetLastError());
        rc = 1;
        goto cleanup;
    }
    check(L"OS-reported client PID equals owned child PID",
          client_pid == pi.dwProcessId, ERROR_SUCCESS);

    child_process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, client_pid);
    check(L"open OS-identified child process", child_process != NULL,
          child_process ? ERROR_SUCCESS : GetLastError());
    if (child_process == NULL) {
        rc = 1;
        goto cleanup;
    }

    facts_ok = token_facts(GetCurrentProcess(), &parent_sid, &parent_session) &&
               token_facts(child_process, &child_sid, &child_session);
    check(L"query both process token identities and sessions", facts_ok,
          facts_ok ? ERROR_SUCCESS : GetLastError());
    if (!facts_ok) {
        rc = 1;
        goto cleanup;
    }
    check(L"client TokenUser matches expected local user",
          EqualSid(parent_sid, child_sid), ERROR_SUCCESS);
    check(L"client TokenSessionId matches expected session",
          parent_session == child_session, ERROR_SUCCESS);

    ZeroMemory(&message, sizeof(message));
    if (!ReadFile(pipe, &message, sizeof(message), &read, NULL) ||
        read != sizeof(message)) {
        check(L"read child handshake", FALSE, GetLastError());
        rc = 1;
        goto cleanup;
    }
    check(L"child observed server PID from OS", message.observed_server_pid == pid,
          ERROR_SUCCESS);
    check(L"forged PID claim differs from actual server PID",
          message.claimed_server_pid != pid, ERROR_SUCCESS);
    check(L"child independently matched server SID and session",
          (message.child_flags & 7u) == 7u, ERROR_SUCCESS);
    check(L"child compared OS server PID with expected parent PID",
          (message.child_flags & 8u) != 0, ERROR_SUCCESS);

    if (!WriteFile(pipe, &ack, sizeof(ack), &written, NULL) || written != sizeof(ack)) {
        check(L"release child handshake", FALSE, GetLastError());
        rc = 1;
        goto cleanup;
    }
    check(L"release child handshake", TRUE, ERROR_SUCCESS);

    if (WaitForSingleObject(child, 5000) != WAIT_OBJECT_0) {
        check(L"child exits within deadline", FALSE, GetLastError());
        rc = 1;
        goto cleanup;
    }
    if (!GetExitCodeProcess(child, &exit_code)) {
        check(L"collect child exit code", FALSE, GetLastError());
        rc = 1;
        goto cleanup;
    }
    check(L"child reports successful reciprocal identity check", exit_code == 0,
          exit_code);
    check(L"pipe rejects remote clients flag configured", TRUE, ERROR_SUCCESS);

cleanup:
    if (child != NULL && WaitForSingleObject(child, 0) == WAIT_TIMEOUT) {
        TerminateProcess(child, 99);
        WaitForSingleObject(child, 5000);
    }
    if (child_process != NULL)
        CloseHandle(child_process);
    free_sid(parent_sid);
    free_sid(child_sid);
    if (thread != NULL)
        CloseHandle(thread);
    if (child != NULL)
        CloseHandle(child);
    if (pipe != INVALID_HANDLE_VALUE)
        CloseHandle(pipe);

    wprintf(L"RESULT pass=%u fail=%u\n", g_pass, g_fail);
    return (rc == 0 && g_fail == 0) ? 0 : 1;
}
