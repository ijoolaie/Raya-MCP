#include "reaper-managed-worker-v2.h"
#include <stdio.h>
#include <wchar.h>
#define CHECK(x) do { ++checks; if (!(x)) { \
    (void)fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; \
} } while (0)
static volatile LONG released_count = 0;
static volatile LONG released_after_commit = 0;
static void released(raya_managed_operation *op, void *context) {
    HANDLE event_handle = (HANDLE)context;
    if (op->owner.state == RAYA_PIPE_OWNER_RELEASED &&
        op->finalizer.state == RAYA_FINALIZER_RELEASED &&
        op->integrated.base.state == RAYA_REAPER_OP_RELEASED)
        (void)InterlockedExchange(&released_after_commit, 1);
    (void)InterlockedIncrement(&released_count);
    (void)SetEvent(event_handle);
}
int main(void) {
    unsigned checks = 0;
    wchar_t name[160];
    HANDLE server, client, connect_event, operation_event, callback_event;
    HANDLE duplicate_pipe;
    OVERLAPPED connect_ov;
    raya_managed_worker worker;
    raya_managed_operation operation;
    BYTE buffer = 0;
    DWORD transferred = 0, error;
    BOOL ok;
    raya_managed_handoff_result handoff;
    (void)swprintf_s(name, 160, L"\\\\.\\pipe\\raya-managed-notfound-%lu-%llu",
        (unsigned long)GetCurrentProcessId(), (unsigned long long)GetTickCount64());
    server = CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, NULL);
    CHECK(server != INVALID_HANDLE_VALUE);
    client = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
        OPEN_EXISTING, 0, NULL);
    CHECK(client != INVALID_HANDLE_VALUE);
    ZeroMemory(&connect_ov, sizeof(connect_ov));
    connect_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(connect_event != NULL);
    connect_ov.hEvent = connect_event;
    ok = ConnectNamedPipe(server, &connect_ov);
    error = ok ? ERROR_SUCCESS : GetLastError();
    CHECK(ok || error == ERROR_PIPE_CONNECTED);
    CHECK(CloseHandle(connect_event));
    operation_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    callback_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(operation_event != NULL && callback_event != NULL);
    CHECK(DuplicateHandle(GetCurrentProcess(), server, GetCurrentProcess(),
        &duplicate_pipe, 0, FALSE, DUPLICATE_SAME_ACCESS));
    CHECK(raya_managed_operation_init(&operation, duplicate_pipe,
        operation_event, &buffer, 1, released, callback_event));
    CHECK(raya_managed_worker_start(&worker));
    CHECK(raya_managed_worker_admit(&worker));
    ok = ReadFile(operation.issuing_pipe, &buffer, 1, &transferred,
        &operation.overlapped);
    error = ok ? ERROR_SUCCESS : GetLastError();
    CHECK(!ok && error == ERROR_IO_PENDING);
    CHECK(raya_managed_worker_reserve(&worker, &operation));
    CHECK(WriteFile(client, "N", 1, &transferred, NULL));
    CHECK(transferred == 1);
    CHECK(WaitForSingleObject(operation_event, 5000) == WAIT_OBJECT_0);
    ok = CancelIoEx(operation.issuing_pipe, &operation.overlapped);
    error = ok ? ERROR_SUCCESS : GetLastError();
    CHECK(!ok && error == ERROR_NOT_FOUND);
    handoff = raya_managed_worker_handoff(&worker, &operation);
    CHECK(handoff == RAYA_MANAGED_HANDOFF_TRANSFERRED);
    CHECK(WaitForSingleObject(callback_event, 5000) == WAIT_OBJECT_0);
    CHECK(InterlockedCompareExchange(&released_count, 0, 0) == 1);
    CHECK(InterlockedCompareExchange(&released_after_commit, 0, 0) == 1);
    CHECK(buffer == (BYTE)'N');
    CHECK(operation.owner.terminal_collections == 1);
    CHECK(operation.finalizer.successful_closes == 2);
    CHECK(operation.finalizer.release_commits == 1);
    CHECK(raya_managed_worker_shutdown(&worker));
    CHECK(raya_managed_worker_join(&worker, 5000));
    CHECK(raya_managed_worker_destroy(&worker));
    CHECK(CloseHandle(callback_event));
    CHECK(CloseHandle(client));
    CHECK(CloseHandle(server));
    (void)printf("PASS: %u assertions; ERROR_NOT_FOUND completion-wins race collected and released\n", checks);
    return 0;
}
