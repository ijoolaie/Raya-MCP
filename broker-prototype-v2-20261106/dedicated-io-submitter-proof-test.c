#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

#define CHECK(x) do { ++checks; if (!(x)) { (void)fprintf(stderr, "FAIL line %d: %s (err=%lu)\n", __LINE__, #x, (unsigned long)GetLastError()); return 1; } } while (0)

typedef struct {
    HANDLE request_ready;
    HANDLE issued;
    HANDLE terminal_ack;
    HANDLE pipe;
    OVERLAPPED ov;
    BYTE buffer;
    DWORD issue_error;
    DWORD completion_error;
    DWORD transferred;
    DWORD issuer_tid;
    volatile LONG producer_exited;
} test_state;

static unsigned checks;

static DWORD WINAPI producer_thread(void *context) {
    test_state *s = (test_state *)context;
    (void)SetEvent(s->request_ready);
    (void)InterlockedExchange(&s->producer_exited, 1);
    return 0;
}

/* The only thread allowed to issue the overlapped operation. */
static DWORD WINAPI submitter_thread(void *context) {
    test_state *s = (test_state *)context;
    DWORD bytes = 0;
    BOOL ok;
    s->issuer_tid = GetCurrentThreadId();
    if (WaitForSingleObject(s->request_ready, 5000) != WAIT_OBJECT_0) {
        s->issue_error = GetLastError();
        (void)SetEvent(s->issued);
        return 10;
    }
    ok = ReadFile(s->pipe, &s->buffer, 1, &bytes, &s->ov);
    if (ok) {
        s->issue_error = ERROR_SUCCESS;
    } else {
        s->issue_error = GetLastError();
        if (s->issue_error != ERROR_IO_PENDING) {
            (void)SetEvent(s->issued);
            return 11;
        }
    }
    (void)SetEvent(s->issued);
    /* Keep the issuing thread alive until the completion thread proves terminal. */
    if (WaitForSingleObject(s->terminal_ack, 10000) != WAIT_OBJECT_0)
        return 12;
    return 0;
}

/* Models the independent Completion Worker collecting the exact I/O. */
static DWORD WINAPI completion_thread(void *context) {
    test_state *s = (test_state *)context;
    DWORD bytes = 0;
    BOOL ok;
    if (WaitForSingleObject(s->issued, 5000) != WAIT_OBJECT_0)
        return 20;
    if (s->issue_error != ERROR_IO_PENDING && s->issue_error != ERROR_SUCCESS)
        return 21;
    if (WaitForSingleObject(s->ov.hEvent, 10000) != WAIT_OBJECT_0)
        return 22;
    ok = GetOverlappedResult(s->pipe, &s->ov, &bytes, FALSE);
    s->completion_error = ok ? ERROR_SUCCESS : GetLastError();
    s->transferred = bytes;
    (void)SetEvent(s->terminal_ack);
    return ok ? 0 : 23;
}

static int make_pipe(HANDLE *server, HANDLE *client, wchar_t *name, size_t cap) {
    OVERLAPPED connect_ov;
    BOOL ok;
    DWORD error;
    (void)swprintf_s(name, cap, L"\\\\.\\pipe\\raya-submitter-proof-%lu-%llu",
        (unsigned long)GetCurrentProcessId(), (unsigned long long)GetTickCount64());
    *server = CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, NULL);
    if (*server == INVALID_HANDLE_VALUE) return 0;
    *client = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
        OPEN_EXISTING, 0, NULL);
    if (*client == INVALID_HANDLE_VALUE) { CloseHandle(*server); return 0; }
    ZeroMemory(&connect_ov, sizeof(connect_ov));
    connect_ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!connect_ov.hEvent) { CloseHandle(*client); CloseHandle(*server); return 0; }
    ok = ConnectNamedPipe(*server, &connect_ov);
    error = ok ? ERROR_SUCCESS : GetLastError();
    (void)CloseHandle(connect_ov.hEvent);
    if (!ok && error != ERROR_PIPE_CONNECTED) {
        CloseHandle(*client); CloseHandle(*server); return 0;
    }
    return 1;
}

int main(void) {
    HANDLE server = INVALID_HANDLE_VALUE, client = INVALID_HANDLE_VALUE;
    HANDLE issuer_pipe = INVALID_HANDLE_VALUE;
    HANDLE producer = NULL, submitter = NULL, completion = NULL;
    wchar_t name[160];
    test_state s;
    DWORD written = 0;
    BYTE payload = 'S';
    ZeroMemory(&s, sizeof(s));
    s.completion_error = ERROR_INVALID_FUNCTION;

    CHECK(make_pipe(&server, &client, name, 160));
    CHECK(DuplicateHandle(GetCurrentProcess(), server, GetCurrentProcess(),
        &issuer_pipe, 0, FALSE, DUPLICATE_SAME_ACCESS));
    s.pipe = issuer_pipe;
    s.request_ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    s.issued = CreateEventW(NULL, TRUE, FALSE, NULL);
    s.terminal_ack = CreateEventW(NULL, TRUE, FALSE, NULL);
    s.ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(s.request_ready != NULL && s.issued != NULL &&
        s.terminal_ack != NULL && s.ov.hEvent != NULL);

    submitter = CreateThread(NULL, 0, submitter_thread, &s, 0, NULL);
    CHECK(submitter != NULL);
    producer = CreateThread(NULL, 0, producer_thread, &s, 0, NULL);
    CHECK(producer != NULL);
    CHECK(WaitForSingleObject(producer, 5000) == WAIT_OBJECT_0);
    CHECK(InterlockedCompareExchange(&s.producer_exited, 0, 0) == 1);
    CHECK(CloseHandle(producer)); producer = NULL;

    CHECK(WaitForSingleObject(s.issued, 5000) == WAIT_OBJECT_0);
    CHECK(s.issue_error == ERROR_IO_PENDING);
    CHECK(WaitForSingleObject(submitter, 0) == WAIT_TIMEOUT);
    CHECK(s.issuer_tid == GetThreadId(submitter));
    CHECK(WriteFile(client, &payload, 1, &written, NULL));
    CHECK(written == 1);

    completion = CreateThread(NULL, 0, completion_thread, &s, 0, NULL);
    CHECK(completion != NULL);
    CHECK(WaitForSingleObject(completion, 10000) == WAIT_OBJECT_0);
    CHECK(s.completion_error == ERROR_SUCCESS);
    CHECK(s.transferred == 1 && s.buffer == payload);
    CHECK(WaitForSingleObject(submitter, 5000) == WAIT_OBJECT_0);
    CHECK(CloseHandle(completion)); completion = NULL;
    CHECK(CloseHandle(submitter)); submitter = NULL;

    CHECK(CloseHandle(s.ov.hEvent));
    CHECK(CloseHandle(s.terminal_ack));
    CHECK(CloseHandle(s.issued));
    CHECK(CloseHandle(s.request_ready));
    CHECK(CloseHandle(issuer_pipe));
    CHECK(CloseHandle(client));
    CHECK(CloseHandle(server));
    (void)printf("Dedicated Submitter issuer-lifetime proof: %u assertions, PASS\n", checks);
    return 0;
}
