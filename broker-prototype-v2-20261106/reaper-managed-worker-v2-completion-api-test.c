#include "reaper-managed-worker-v2.h"
#include <stdio.h>
#include <wchar.h>

static volatile LONG callbacks = 0;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; \
} } while (0)

static BOOL WINAPI injected_uncertain_result(HANDLE file,
    LPOVERLAPPED overlapped, LPDWORD transferred, BOOL wait, void *context) {
    (void)file; (void)overlapped; (void)transferred; (void)wait; (void)context;
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}
static void released(raya_managed_operation *op, void *context) {
    (void)op; (void)context;
    InterlockedIncrement(&callbacks);
}

int main(void) {
    wchar_t pipe_name[160];
    HANDLE server = INVALID_HANDLE_VALUE, client = INVALID_HANDLE_VALUE;
    HANDLE connect_event = NULL, operation_event = NULL;
    HANDLE duplicate_pipe = INVALID_HANDLE_VALUE;
    OVERLAPPED connect_ov;
    raya_managed_worker worker;
    raya_managed_operation *op;
    BYTE *buffer;
    DWORD transferred = 0, error = 0;
    BOOL io_ok;
    unsigned i;

    swprintf_s(pipe_name, 160, L"\\\\.\\pipe\\raya-worker-uncertain-%lu-%llu",
        (unsigned long)GetCurrentProcessId(), (unsigned long long)GetTickCount64());
    server = CreateNamedPipeW(pipe_name,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, NULL);
    CHECK(server != INVALID_HANDLE_VALUE);
    client = CreateFileW(pipe_name, GENERIC_READ | GENERIC_WRITE, 0,
        NULL, OPEN_EXISTING, 0, NULL);
    CHECK(client != INVALID_HANDLE_VALUE);
    ZeroMemory(&connect_ov, sizeof(connect_ov));
    connect_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(connect_event != NULL);
    connect_ov.hEvent = connect_event;
    io_ok = ConnectNamedPipe(server, &connect_ov);
    error = io_ok ? ERROR_SUCCESS : GetLastError();
    CHECK(io_ok || error == ERROR_PIPE_CONNECTED || error == ERROR_IO_PENDING);
    if (!io_ok && error == ERROR_IO_PENDING)
        CHECK(WaitForSingleObject(connect_event, 5000) == WAIT_OBJECT_0);
    CHECK(CloseHandle(connect_event));

    operation_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(operation_event != NULL);
    CHECK(DuplicateHandle(GetCurrentProcess(), server, GetCurrentProcess(),
        &duplicate_pipe, 0, FALSE, DUPLICATE_SAME_ACCESS));
    op = (raya_managed_operation *)HeapAlloc(GetProcessHeap(),
        HEAP_ZERO_MEMORY, sizeof(*op));
    buffer = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 1);
    CHECK(op != NULL && buffer != NULL);
    CHECK(raya_managed_operation_init(op, duplicate_pipe, operation_event,
        buffer, 1, released, NULL));
    CHECK(raya_managed_worker_start_with_completion_api(&worker,
        injected_uncertain_result, NULL));
    CHECK(raya_managed_worker_admit(&worker));
    CHECK(raya_managed_worker_reserve(&worker, op));
    io_ok = ReadFile(op->issuing_pipe, op->buffer, 1, &transferred, &op->overlapped);
    error = io_ok ? ERROR_SUCCESS : GetLastError();
    CHECK(!io_ok && error == ERROR_IO_PENDING);
    CHECK(raya_managed_worker_handoff(&worker, op) == RAYA_MANAGED_HANDOFF_TRANSFERRED);
    CHECK(WriteFile(client, "U", 1, &transferred, NULL));
    CHECK(transferred == 1);

    CHECK(WaitForSingleObject(worker.thread, 5000) == WAIT_OBJECT_0);
    CHECK(GetExitCodeThread(worker.thread, &error));
    CHECK(error == ERROR_ACCESS_DENIED);
    CHECK(worker.integrated.core.lifecycle == RAYA_REAPER_DEGRADED);
    CHECK((worker.integrated.core.degradation_causes &
        RAYA_RECOVERY_CAUSE_COMPLETION_UNCERTAIN) != 0);
    CHECK(worker.integrated.core.outstanding == 1);
    CHECK(worker.integrated.reserved_slots == 1);
    CHECK(worker.integrated.registry.count == 1);
    CHECK(op->integrated.collect_state == RAYA_INTEGRATED_UNCERTAIN);
    CHECK(op->integrated.reservation_state == RAYA_INTEGRATED_RESERVATION_HANDED_OFF);
    CHECK(op->owner.state == RAYA_PIPE_OWNER_REAPER_UNCERTAIN);
    CHECK(InterlockedCompareExchange(&callbacks, 0, 0) == 0);
    CHECK(!raya_managed_worker_join(&worker, 5000));
    CHECK(worker.joined == TRUE);
    CHECK(!raya_managed_worker_destroy(&worker));

    /* Uncertain ownership is intentionally retained until process exit. */
    CHECK(CloseHandle(client));
    CHECK(CloseHandle(server));
    for (i = 0; i < 100; ++i) Sleep(1);
    printf("completion uncertainty: %u assertions passed; operation intentionally retained\n", checks);
    return 0;
}
