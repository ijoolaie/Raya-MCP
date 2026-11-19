#include "reaper-managed-worker-v2.h"
#include <stdio.h>
#include <wchar.h>

#define NOPS 70
#define CHECK(x) do { ++checks; if (!(x)) { \
    (void)fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; \
} } while (0)

static volatile LONG released_count = 0;
static void released(raya_managed_operation *op, void *context) {
    HANDLE semaphore = (HANDLE)context;
    (void)op;
    (void)InterlockedIncrement(&released_count);
    (void)ReleaseSemaphore(semaphore, 1, NULL);
}

static int create_connected_pipe(unsigned index, HANDLE *server_out,
    HANDLE *client_out, DWORD process_id, ULONGLONG nonce) {
    wchar_t name[192];
    HANDLE server, client, connect_event;
    OVERLAPPED connect_ov;
    BOOL ok;
    DWORD error;
    (void)swprintf_s(name, 192,
        L"\\\\.\\pipe\\raya-managed-multi-%lu-%llu-%u",
        (unsigned long)process_id, (unsigned long long)nonce, index);
    server = CreateNamedPipeW(name,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, 4096, 4096, 0, NULL);
    if (server == INVALID_HANDLE_VALUE) return 0;
    client = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0,
        NULL, OPEN_EXISTING, 0, NULL);
    if (client == INVALID_HANDLE_VALUE) {
        CloseHandle(server);
        return 0;
    }
    ZeroMemory(&connect_ov, sizeof(connect_ov));
    connect_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (connect_event == NULL) {
        CloseHandle(client); CloseHandle(server); return 0;
    }
    connect_ov.hEvent = connect_event;
    ok = ConnectNamedPipe(server, &connect_ov);
    error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(connect_event);
    if (!ok && error != ERROR_PIPE_CONNECTED) {
        CloseHandle(client); CloseHandle(server); return 0;
    }
    *server_out = server;
    *client_out = client;
    return 1;
}

int main(void) {
    unsigned checks = 0, i;
    HANDLE servers[NOPS], clients[NOPS], events[NOPS];
    HANDLE semaphore;
    raya_managed_operation operations[NOPS];
    raya_managed_worker worker;
    BYTE buffers[NOPS];
    DWORD transferred;
    BOOL io_ok;
    DWORD error;
    raya_managed_handoff_result handoff;
    ULONGLONG nonce = GetTickCount64();

    ZeroMemory(servers, sizeof(servers));
    ZeroMemory(clients, sizeof(clients));
    ZeroMemory(events, sizeof(events));
    ZeroMemory(operations, sizeof(operations));
    ZeroMemory(buffers, sizeof(buffers));
    semaphore = CreateSemaphoreW(NULL, 0, NOPS, NULL);
    CHECK(semaphore != NULL);
    CHECK(raya_managed_worker_start(&worker));

    for (i = 0; i < NOPS; ++i) {
        HANDLE duplicate_pipe = INVALID_HANDLE_VALUE;
        CHECK(create_connected_pipe(i, &servers[i], &clients[i],
            GetCurrentProcessId(), nonce));
        events[i] = CreateEventW(NULL, TRUE, FALSE, NULL);
        CHECK(events[i] != NULL);
        CHECK(DuplicateHandle(GetCurrentProcess(), servers[i],
            GetCurrentProcess(), &duplicate_pipe, 0, FALSE,
            DUPLICATE_SAME_ACCESS));
        CHECK(raya_managed_operation_init(&operations[i], duplicate_pipe,
            events[i], &buffers[i], 1, released, semaphore));
        CHECK(raya_managed_worker_admit(&worker));
        io_ok = ReadFile(operations[i].issuing_pipe, &buffers[i], 1,
            &transferred, &operations[i].overlapped);
        error = io_ok ? ERROR_SUCCESS : GetLastError();
        CHECK(!io_ok && error == ERROR_IO_PENDING);
        CHECK(raya_managed_worker_reserve(&worker, &operations[i]));
        handoff = raya_managed_worker_handoff(&worker, &operations[i]);
        CHECK(handoff == RAYA_MANAGED_HANDOFF_TRANSFERRED);
    }

    /* The caller drops each original pipe handle immediately after handoff. */
    for (i = 0; i < NOPS; ++i) {
        CHECK(CloseHandle(servers[i]));
        servers[i] = INVALID_HANDLE_VALUE;
    }
    /* Do not complete any I/O until all 70 operations have been handed off. */
    for (i = 0; i < NOPS; ++i) {
        CHECK(WriteFile(clients[i], "M", 1, &transferred, NULL));
        CHECK(transferred == 1);
    }
    for (i = 0; i < NOPS; ++i)
        CHECK(WaitForSingleObject(semaphore, 10000) == WAIT_OBJECT_0);
    CHECK(InterlockedCompareExchange(&released_count, 0, 0) == NOPS);
    for (i = 0; i < NOPS; ++i) CHECK(buffers[i] == (BYTE)'M');

    CHECK(raya_managed_worker_shutdown(&worker));
    CHECK(raya_managed_worker_join(&worker, 10000));
    CHECK(raya_managed_worker_destroy(&worker));
    for (i = 0; i < NOPS; ++i) {
        CHECK(CloseHandle(clients[i]));
    }
    CHECK(CloseHandle(semaphore));
    (void)printf("PASS: %u assertions; %u concurrent live operations collected across >63-event windows\n",
        checks, NOPS);
    return 0;
}
