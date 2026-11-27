#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPERATION_LIMIT 4

typedef struct result_slot {
    HANDLE done_event;
    DWORD terminal_error;
    DWORD transferred;
    DWORD closed_handles;
    LONG reaped;
} result_slot;

typedef struct io_operation {
    OVERLAPPED overlapped;
    HANDLE server;
    HANDLE client;
    BYTE buffer[64];
    result_slot *result;
    BOOL pending;
} io_operation;

static unsigned passed;
static unsigned failed;

#define CHECK(label, expression) do { \
    if (expression) { printf("PASS %s\n", label); ++passed; } \
    else { printf("FAIL %s (error=%lu)\n", label, (unsigned long)GetLastError()); ++failed; } \
} while (0)

static BOOL make_pipe_pair(HANDLE *server_out, HANDLE *client_out, unsigned index)
{
    wchar_t name[128];
    HANDLE server;
    HANDLE client;
    OVERLAPPED connect_operation;
    DWORD transferred = 0;
    DWORD error = ERROR_SUCCESS;
    BOOL connected;

    (void)swprintf_s(name, 128, L"\\\\.\\pipe\\RayaIoReaper-%lu-%u",
        (unsigned long)GetCurrentProcessId(), index);

    server = CreateNamedPipeW(name,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, 4096, 4096, 0, NULL);
    if (server == INVALID_HANDLE_VALUE)
        return FALSE;

    ZeroMemory(&connect_operation, sizeof(connect_operation));
    connect_operation.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (connect_operation.hEvent == NULL) {
        CloseHandle(server);
        return FALSE;
    }

    connected = ConnectNamedPipe(server, &connect_operation);
    if (!connected) {
        error = GetLastError();
        if (error != ERROR_IO_PENDING) {
            CloseHandle(connect_operation.hEvent);
            CloseHandle(server);
            SetLastError(error);
            return FALSE;
        }
    }

    client = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (client == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        if (!connected) {
            (void)CancelIoEx(server, &connect_operation);
            (void)WaitForSingleObject(connect_operation.hEvent, INFINITE);
            (void)GetOverlappedResult(server, &connect_operation, &transferred, FALSE);
        }
        CloseHandle(connect_operation.hEvent);
        CloseHandle(server);
        SetLastError(error);
        return FALSE;
    }

    if (!connected) {
        if (WaitForSingleObject(connect_operation.hEvent, 5000) != WAIT_OBJECT_0 ||
            !GetOverlappedResult(server, &connect_operation, &transferred, FALSE)) {
            error = GetLastError();
            CloseHandle(client);
            (void)CancelIoEx(server, &connect_operation);
            (void)WaitForSingleObject(connect_operation.hEvent, INFINITE);
            (void)GetOverlappedResult(server, &connect_operation, &transferred, FALSE);
            CloseHandle(connect_operation.hEvent);
            CloseHandle(server);
            SetLastError(error);
            return FALSE;
        }
    }

    CloseHandle(connect_operation.hEvent);
    *server_out = server;
    *client_out = client;
    return TRUE;
}

static DWORD WINAPI reaper_thread(void *parameter)
{
    io_operation **operations = (io_operation **)parameter;
    HANDLE events[OPERATION_LIMIT];
    DWORD wait_result;
    unsigned i;

    for (i = 0; i < OPERATION_LIMIT; ++i)
        events[i] = operations[i]->overlapped.hEvent;

    wait_result = WaitForMultipleObjects(OPERATION_LIMIT, events, TRUE, 5000);
    if (wait_result == WAIT_TIMEOUT) {
        printf("REAPER_WAIT_TIMEOUT: preserving ownership until every completion arrives\n");
        ++failed;
        for (i = 0; i < OPERATION_LIMIT; ++i)
            (void)WaitForSingleObject(events[i], INFINITE);
    } else if (wait_result != WAIT_OBJECT_0) {
        printf("REAPER_WAIT_FAILED: %lu; draining individually\n",
            (unsigned long)GetLastError());
        ++failed;
        for (i = 0; i < OPERATION_LIMIT; ++i)
            (void)WaitForSingleObject(events[i], INFINITE);
    }

    for (i = 0; i < OPERATION_LIMIT; ++i) {
        io_operation *operation = operations[i];
        DWORD transferred = 0;
        DWORD terminal_error = ERROR_SUCCESS;
        result_slot *result = operation->result;

        if (!GetOverlappedResult(operation->server, &operation->overlapped,
                &transferred, FALSE))
            terminal_error = GetLastError();

        result->terminal_error = terminal_error;
        result->transferred = transferred;

        if (CloseHandle(operation->overlapped.hEvent))
            result->closed_handles++;
        if (CloseHandle(operation->client))
            result->closed_handles++;
        if (CloseHandle(operation->server))
            result->closed_handles++;

        (void)InterlockedIncrement(&result->reaped);
        free(operation);
        operations[i] = NULL;
        SetEvent(result->done_event);
    }

    return 0;
}

int main(void)
{
    io_operation *operations[OPERATION_LIMIT] = { 0 };
    result_slot results[OPERATION_LIMIT];
    HANDLE reaper = NULL;
    unsigned i;

    ZeroMemory(results, sizeof(results));
    for (i = 0; i < OPERATION_LIMIT; ++i) {
        results[i].done_event = CreateEventW(NULL, TRUE, FALSE, NULL);
        CHECK("create caller completion event", results[i].done_event != NULL);
        if (results[i].done_event == NULL)
            goto cleanup;
    }

    for (i = 0; i < OPERATION_LIMIT; ++i) {
        DWORD transferred = 0;
        BOOL read_result;
        DWORD cancel_error = ERROR_SUCCESS;
        BOOL cancel_result;

        operations[i] = (io_operation *)calloc(1, sizeof(io_operation));
        CHECK("allocate heap-owned I/O operation", operations[i] != NULL);
        if (operations[i] == NULL)
            goto cleanup;

        operations[i]->result = &results[i];
        if (!make_pipe_pair(&operations[i]->server, &operations[i]->client, i)) {
            printf("FAIL create independent named-pipe pair: %lu\n",
                (unsigned long)GetLastError());
            ++failed;
            goto cleanup;
        }

        operations[i]->overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        CHECK("create operation event", operations[i]->overlapped.hEvent != NULL);
        if (operations[i]->overlapped.hEvent == NULL)
            goto cleanup;

        read_result = ReadFile(operations[i]->server, operations[i]->buffer,
            sizeof(operations[i]->buffer), &transferred, &operations[i]->overlapped);
        CHECK("heap operation enters pending state",
            !read_result && GetLastError() == ERROR_IO_PENDING);
        if (read_result || GetLastError() != ERROR_IO_PENDING)
            goto cleanup;
        operations[i]->pending = TRUE;
        /* Pending ownership stays with the heap record until reaped. */

        cancel_result = CancelIoEx(operations[i]->server, &operations[i]->overlapped);
        if (!cancel_result)
            cancel_error = GetLastError();
        CHECK("request owner submits targeted cancellation",
            cancel_result || cancel_error == ERROR_NOT_FOUND);
    }

    /* From this point the request side never touches the OVERLAPPED or its resources. */
    for (i = 0; i < OPERATION_LIMIT; ++i) {
        DWORD flags = 0;
        CHECK("resources remain live after request-side return",
            GetHandleInformation(operations[i]->server, &flags) &&
            GetHandleInformation(operations[i]->client, &flags) &&
            GetHandleInformation(operations[i]->overlapped.hEvent, &flags));
    }

    reaper = CreateThread(NULL, 0, reaper_thread, operations, 0, NULL);
    CHECK("start single-owner reaper thread", reaper != NULL);
    if (reaper == NULL)
        goto cleanup;

    for (i = 0; i < OPERATION_LIMIT; ++i) {
        CHECK("caller receives reaper completion",
            WaitForSingleObject(results[i].done_event, 10000) == WAIT_OBJECT_0);
        CHECK("reaper observes canceled terminal status",
            results[i].terminal_error == ERROR_OPERATION_ABORTED);
        CHECK("reaper closes event and both pipe handles",
            results[i].closed_handles == 3);
        CHECK("operation is reaped exactly once",
            InterlockedCompareExchange(&results[i].reaped, 0, 0) == 1);
    }

    CHECK("reaper thread exits cleanly",
        WaitForSingleObject(reaper, 5000) == WAIT_OBJECT_0);

cleanup:
    if (reaper != NULL) {
        (void)WaitForSingleObject(reaper, INFINITE);
        CloseHandle(reaper);
    }
    for (i = 0; i < OPERATION_LIMIT; ++i) {
        if (operations[i] != NULL) {
            /* Only pre-reaper setup failures reach this path. */
            if (operations[i]->overlapped.hEvent != NULL) {
                if (operations[i]->pending) {
                    (void)CancelIoEx(operations[i]->server, &operations[i]->overlapped);
                    (void)WaitForSingleObject(operations[i]->overlapped.hEvent, INFINITE);
                    DWORD ignored = 0;
                    (void)GetOverlappedResult(operations[i]->server,
                        &operations[i]->overlapped, &ignored, FALSE);
                }
                CloseHandle(operations[i]->overlapped.hEvent);
            }
            if (operations[i]->client != NULL &&
                operations[i]->client != INVALID_HANDLE_VALUE)
                CloseHandle(operations[i]->client);
            if (operations[i]->server != NULL &&
                operations[i]->server != INVALID_HANDLE_VALUE)
                CloseHandle(operations[i]->server);
            free(operations[i]);
        }
        if (results[i].done_event != NULL)
            CloseHandle(results[i].done_event);
    }

    printf("OVERLAPPED_PIPE_REAPER: %u passed, %u failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
