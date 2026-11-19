#include "reaper-managed-worker-v2.h"
#include <stdio.h>
#include <wchar.h>
#define CHECK(x) do { ++checks; if (!(x)) { \
    (void)fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; \
} } while (0)
typedef struct { HANDLE fail_handle; LONG calls; } close_context;
static volatile LONG release_calls = 0;
static BOOL injected_close(HANDLE h, void *context) {
    close_context *c = (close_context *)context;
    (void)InterlockedIncrement(&c->calls);
    if (h == c->fail_handle) return FALSE;
    return CloseHandle(h);
}
static void released(raya_managed_operation *op, void *context) {
    (void)op; (void)context;
    (void)InterlockedIncrement(&release_calls);
}
int main(void) {
    unsigned checks = 0;
    wchar_t name[160];
    HANDLE server, client, connect_event, operation_event, callback_event;
    HANDLE duplicate_pipe;
    OVERLAPPED connect_ov;
    raya_managed_worker worker;
    raya_managed_operation operation;
    close_context close_state;
    BYTE buffer = 0;
    DWORD transferred = 0, error;
    BOOL ok;
    raya_managed_handoff_result handoff;
    (void)swprintf_s(name, 160, L"\\\\.\\pipe\\raya-managed-closefail-%lu-%llu",
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
        operation_event, &buffer, 1, released, NULL));
    CHECK(raya_managed_worker_start(&worker));
    close_state.fail_handle = operation_event;
    close_state.calls = 0;
    CHECK(raya_managed_worker_set_close_callback(&worker, injected_close, &close_state));
    CHECK(raya_managed_worker_admit(&worker));
    ok = ReadFile(operation.issuing_pipe, &buffer, 1, &transferred,
        &operation.overlapped);
    error = ok ? ERROR_SUCCESS : GetLastError();
    CHECK(!ok && error == ERROR_IO_PENDING);
    CHECK(raya_managed_worker_reserve(&worker, &operation));
    handoff = raya_managed_worker_handoff(&worker, &operation);
    CHECK(handoff == RAYA_MANAGED_HANDOFF_TRANSFERRED);
    CHECK(WriteFile(client, "F", 1, &transferred, NULL));
    CHECK(transferred == 1);
    CHECK(WaitForSingleObject(worker.thread, 5000) == WAIT_OBJECT_0);
    CHECK(GetExitCodeThread(worker.thread, &error));
    CHECK(error != ERROR_SUCCESS);
    CHECK((worker.integrated.core.degradation_causes &
        RAYA_RECOVERY_CAUSE_HANDLE_CLOSE) != 0);
    CHECK(worker.integrated.core.lifecycle == RAYA_REAPER_DEGRADED);
    CHECK(worker.integrated.core.outstanding == 1);
    CHECK(worker.integrated.reserved_slots == 1);
    CHECK(worker.integrated.registry.count == 0);
    CHECK(operation.finalizer.state == RAYA_FINALIZER_CLOSE_UNCERTAIN);
    CHECK(operation.finalizer.successful_closes == 1);
    CHECK(operation.finalizer.release_commits == 0);
    CHECK(InterlockedCompareExchange(&release_calls, 0, 0) == 0);
    CHECK(InterlockedCompareExchange(&close_state.calls, 0, 0) == 2);
    CHECK(!raya_managed_worker_join(&worker, 100));
    CHECK(!raya_managed_worker_destroy(&worker));
    CHECK(CloseHandle(callback_event));
    CHECK(CloseHandle(client));
    CHECK(CloseHandle(server));
    (void)printf("PASS: %u assertions; close failure retained outstanding ownership and blocked release\n",
        checks);
    return 0;
}
