#include "reaper-managed-worker-v2.h"
#include <stdio.h>
#include <wchar.h>
#define NOPS 70
#define NPRODUCERS 4
#define CHECK(x) do { ++checks; if (!(x)) { \
    (void)fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; \
} } while (0)

typedef struct {
    raya_managed_worker *worker;
    raya_managed_operation *ops;
    unsigned first, count;
    HANDLE ready, gate;
    volatile LONG *handoffs;
} producer_context;

static volatile LONG released_count = 0;
static void released(raya_managed_operation *op, void *context) {
    (void)op;
    (void)InterlockedIncrement(&released_count);
    (void)ReleaseSemaphore((HANDLE)context, 1, NULL);
}
static int create_pipe(unsigned index, HANDLE *server_out, HANDLE *client_out,
    DWORD pid, ULONGLONG nonce) {
    wchar_t name[192];
    HANDLE server, client, ev;
    OVERLAPPED ov;
    BOOL ok;
    DWORD err;
    (void)swprintf_s(name, 192, L"\\\\.\\pipe\\raya-managed-prod-%lu-%llu-%u",
        (unsigned long)pid, (unsigned long long)nonce, index);
    server = CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, NULL);
    if (server == INVALID_HANDLE_VALUE) return 0;
    client = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
        OPEN_EXISTING, 0, NULL);
    if (client == INVALID_HANDLE_VALUE) { CloseHandle(server); return 0; }
    ZeroMemory(&ov, sizeof(ov));
    ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (ev == NULL) { CloseHandle(client); CloseHandle(server); return 0; }
    ov.hEvent = ev;
    ok = ConnectNamedPipe(server, &ov);
    err = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(ev);
    if (!ok && err != ERROR_PIPE_CONNECTED) {
        CloseHandle(client); CloseHandle(server); return 0;
    }
    *server_out = server; *client_out = client;
    return 1;
}
static DWORD WINAPI producer_main(void *arg) {
    producer_context *c = (producer_context *)arg;
    unsigned i;
    for (i = 0; i < c->count; ++i) {
        raya_managed_operation *op = &c->ops[c->first + i];
        if (!raya_managed_worker_admit(c->worker) ||
            !raya_managed_worker_reserve(c->worker, op)) return 10;
    }
    if (!SetEvent(c->ready) || WaitForSingleObject(c->gate, 5000) != WAIT_OBJECT_0)
        return 11;
    for (i = 0; i < c->count; ++i) {
        raya_managed_handoff_result result = raya_managed_worker_handoff(
            c->worker, &c->ops[c->first + i]);
        if (result != RAYA_MANAGED_HANDOFF_TRANSFERRED) return 12;
        (void)InterlockedIncrement(c->handoffs);
    }
    return 0;
}

int main(void) {
    unsigned checks = 0, i, start = 0;
    HANDLE servers[NOPS], clients[NOPS], events[NOPS];
    HANDLE ready[NPRODUCERS], threads[NPRODUCERS], gate, sem;
    raya_managed_operation ops[NOPS];
    producer_context contexts[NPRODUCERS];
    raya_managed_worker worker;
    BYTE buffers[NOPS];
    DWORD transferred, err, exit_code;
    BOOL ok;
    volatile LONG handoffs = 0;
    ULONGLONG nonce = GetTickCount64();
    ZeroMemory(servers, sizeof(servers)); ZeroMemory(clients, sizeof(clients));
    ZeroMemory(events, sizeof(events)); ZeroMemory(ready, sizeof(ready));
    ZeroMemory(threads, sizeof(threads)); ZeroMemory(ops, sizeof(ops));
    ZeroMemory(buffers, sizeof(buffers));
    gate = CreateEventW(NULL, TRUE, FALSE, NULL);
    sem = CreateSemaphoreW(NULL, 0, NOPS, NULL);
    CHECK(gate != NULL && sem != NULL);
    CHECK(raya_managed_worker_start(&worker));
    for (i = 0; i < NOPS; ++i) {
        HANDLE dup;
        CHECK(create_pipe(i, &servers[i], &clients[i], GetCurrentProcessId(), nonce));
        events[i] = CreateEventW(NULL, TRUE, FALSE, NULL);
        CHECK(events[i] != NULL);
        CHECK(DuplicateHandle(GetCurrentProcess(), servers[i], GetCurrentProcess(),
            &dup, 0, FALSE, DUPLICATE_SAME_ACCESS));
        CHECK(raya_managed_operation_init(&ops[i], dup, events[i], &buffers[i], 1,
            released, sem));
        ok = ReadFile(ops[i].issuing_pipe, &buffers[i], 1, &transferred,
            &ops[i].overlapped);
        err = ok ? ERROR_SUCCESS : GetLastError();
        CHECK(!ok && err == ERROR_IO_PENDING);
    }
    for (i = 0; i < NPRODUCERS; ++i) {
        unsigned count = (i == NPRODUCERS - 1) ? NOPS - start :
            (NOPS / NPRODUCERS);
        ready[i] = CreateEventW(NULL, TRUE, FALSE, NULL);
        CHECK(ready[i] != NULL);
        contexts[i].worker = &worker; contexts[i].ops = ops;
        contexts[i].first = start; contexts[i].count = count;
        contexts[i].ready = ready[i]; contexts[i].gate = gate;
        contexts[i].handoffs = &handoffs;
        threads[i] = CreateThread(NULL, 0, producer_main, &contexts[i], 0, NULL);
        CHECK(threads[i] != NULL);
        start += count;
    }
    for (i = 0; i < NPRODUCERS; ++i)
        CHECK(WaitForSingleObject(ready[i], 10000) == WAIT_OBJECT_0);
    {
        size_t callers, reservations;
        EnterCriticalSection(&worker.lock);
        callers = worker.integrated.core.caller_active;
        reservations = worker.integrated.reserved_slots;
        LeaveCriticalSection(&worker.lock);
        CHECK(callers == NOPS);
        CHECK(reservations == NOPS);
    }
    CHECK(raya_managed_worker_shutdown(&worker));
    CHECK(!raya_managed_worker_admit(&worker)); /* no new leases after STOPPING */
    CHECK(SetEvent(gate));
    for (i = 0; i < NPRODUCERS; ++i) {
        CHECK(WaitForSingleObject(threads[i], 10000) == WAIT_OBJECT_0);
        CHECK(GetExitCodeThread(threads[i], &exit_code) && exit_code == 0);
        CHECK(CloseHandle(threads[i]));
    }
    CHECK(InterlockedCompareExchange(&handoffs, 0, 0) == NOPS);
    for (i = 0; i < NOPS; ++i) {
        CHECK(WriteFile(clients[i], "P", 1, &transferred, NULL));
        CHECK(transferred == 1);
    }
    {
        DWORD first_wait = WaitForSingleObject(sem, 15000);
        if (first_wait != WAIT_OBJECT_0) {
            DWORD thread_state = WaitForSingleObject(worker.thread, 0), thread_code = 0;
            size_t outstanding, queued, registry_count, callers;
            uint32_t causes;
            EnterCriticalSection(&worker.lock);
            outstanding = worker.integrated.core.outstanding;
            queued = worker.integrated.core.queued;
            registry_count = worker.integrated.registry.count;
            callers = worker.integrated.core.caller_active;
            causes = worker.integrated.core.degradation_causes;
            LeaveCriticalSection(&worker.lock);
            (void)GetExitCodeThread(worker.thread, &thread_code);
            (void)fprintf(stderr, "diag wait=%lu thread=%lu code=%lu outstanding=%zu queued=%zu registry=%zu callers=%zu causes=%lu handoffs=%ld released=%ld\\n",
                (unsigned long)first_wait, (unsigned long)thread_state,
                (unsigned long)thread_code, outstanding, queued, registry_count,
                callers, (unsigned long)causes, (long)handoffs, (long)released_count);
            return 1;
        }
    }
    for (i = 1; i < NOPS; ++i)
        CHECK(WaitForSingleObject(sem, 15000) == WAIT_OBJECT_0);
    CHECK(InterlockedCompareExchange(&released_count, 0, 0) == NOPS);
    CHECK(raya_managed_worker_join(&worker, 10000));
    CHECK(raya_managed_worker_destroy(&worker));
    for (i = 0; i < NOPS; ++i) {
        CHECK(buffers[i] == (BYTE)'P');
        CHECK(CloseHandle(clients[i])); CHECK(CloseHandle(servers[i]));
    }
    for (i = 0; i < NPRODUCERS; ++i) CHECK(CloseHandle(ready[i]));
    CHECK(CloseHandle(gate)); CHECK(CloseHandle(sem));
    (void)printf("PASS: %u assertions; four producers drained 70 pre-reserved ops after STOPPING\n", checks);
    return 0;
}
