#include "reaper-managed-worker-v2.h"
#include <stdio.h>
#include <wchar.h>

#define CHECK(x) do { ++checks; if (!(x)) { \
    (void)fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; \
} } while (0)

static volatile LONG released_count = 0;
static void released(raya_managed_operation *op, void *context) {
    HANDLE event_handle = (HANDLE)context;
    (void)op;
    (void)InterlockedIncrement(&released_count);
    (void)SetEvent(event_handle);
}

int main(void) {
    unsigned checks = 0;
    wchar_t pipe_name[160];
    HANDLE server = INVALID_HANDLE_VALUE;
    HANDLE client = INVALID_HANDLE_VALUE;
    HANDLE operation_event = NULL;
    HANDLE callback_event = NULL;
    HANDLE duplicate_pipe = INVALID_HANDLE_VALUE;
    raya_managed_worker worker;
    raya_managed_operation operation;
    OVERLAPPED connect_ov;
    BYTE buffer = 0;
    DWORD transferred = 0;
    BOOL io_ok;
    DWORD error;
    raya_managed_handoff_result handoff;

    (void)swprintf_s(pipe_name, 160, L"\\\\.\\pipe\\raya-managed-worker-%lu-%llu",
        (unsigned long)GetCurrentProcessId(),
        (unsigned long long)GetTickCount64());
    server = CreateNamedPipeW(pipe_name,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, 4096, 4096, 0, NULL);
    CHECK(server != INVALID_HANDLE_VALUE);
    client = CreateFileW(pipe_name, GENERIC_READ | GENERIC_WRITE, 0,
        NULL, OPEN_EXISTING, 0, NULL);
    CHECK(client != INVALID_HANDLE_VALUE);
    ZeroMemory(&connect_ov, sizeof(connect_ov));
    connect_ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(connect_ov.hEvent != NULL);
    io_ok = ConnectNamedPipe(server, &connect_ov);
    error = io_ok ? ERROR_SUCCESS : GetLastError();
    CHECK(io_ok || error == ERROR_PIPE_CONNECTED);
    CHECK(CloseHandle(connect_ov.hEvent));

    operation_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    callback_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(operation_event != NULL && callback_event != NULL);
    CHECK(DuplicateHandle(GetCurrentProcess(), server, GetCurrentProcess(),
        &duplicate_pipe, 0, FALSE, DUPLICATE_SAME_ACCESS));

    CHECK(raya_managed_operation_init(&operation, duplicate_pipe,
        operation_event, &buffer, sizeof(buffer), released, callback_event));
    CHECK(raya_managed_worker_start(&worker));
    CHECK(raya_managed_worker_admit(&worker));
    CHECK(raya_managed_worker_reserve(&worker, &operation));
    CHECK(raya_managed_worker_shutdown(&worker)); /* pre-admitted handoff must still drain */
    CHECK(raya_managed_worker_shutdown(&worker)); /* idempotent while STOPPING */

    io_ok = ReadFile(operation.issuing_pipe, &buffer, 1, &transferred, &operation.overlapped);
    error = io_ok ? ERROR_SUCCESS : GetLastError();
    CHECK(!io_ok && error == ERROR_IO_PENDING);
    CHECK(SetEvent(operation_event)); /* deliberate spurious signal while I/O remains pending */

    handoff = raya_managed_worker_handoff(&worker, &operation);
    CHECK(handoff == RAYA_MANAGED_HANDOFF_TRANSFERRED);
    {
        DWORD waited = 0;
        while (InterlockedCompareExchange(&operation.incomplete_observations, 0, 0) == 0 &&
               waited < 5000) { Sleep(1); ++waited; }
        CHECK(InterlockedCompareExchange(&operation.incomplete_observations, 0, 0) >= 1);
    }
    CHECK(WriteFile(client, "R", 1, &transferred, NULL));
    CHECK(transferred == 1);
    CHECK(WaitForSingleObject(callback_event, 5000) == WAIT_OBJECT_0);
    CHECK(InterlockedCompareExchange(&released_count, 0, 0) == 1);
    CHECK(buffer == (BYTE)'R');
    CHECK(InterlockedCompareExchange(&operation.incomplete_observations, 0, 0) >= 1);

    CHECK(raya_managed_worker_join(&worker, 5000));
    CHECK(raya_managed_worker_shutdown(&worker)); /* idempotent after STOPPED */
    CHECK(raya_managed_worker_destroy(&worker));

    CHECK(CloseHandle(callback_event));
    CHECK(CloseHandle(client));
    CHECK(CloseHandle(server));

    /* A bare admission lease must also drain after shutdown via finish(). */
    CHECK(raya_managed_worker_start(&worker));
    CHECK(raya_managed_worker_admit(&worker));
    CHECK(raya_managed_worker_shutdown(&worker));
    CHECK(raya_managed_worker_finish(&worker));
    CHECK(raya_managed_worker_join(&worker, 5000));
    CHECK(raya_managed_worker_destroy(&worker));

    (void)printf("PASS: %u assertions; live I/O and admission-lease shutdown drained\n",
        checks);
    return 0;
}
