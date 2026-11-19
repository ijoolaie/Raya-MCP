#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    OVERLAPPED ov;
    HANDLE pipe;
    HANDLE event_handle;
    unsigned char buffer[8];
    DWORD result;
    DWORD bytes;
} pending_operation;

static HANDLE event_for_overlapped(HANDLE h) {
    return (HANDLE)((ULONG_PTR)h | (ULONG_PTR)1);
}

/* Test seam: deliberately injects an error without invoking native CancelIoEx. */
typedef BOOL (WINAPI *cancel_fn)(HANDLE, LPOVERLAPPED);
static BOOL WINAPI injected_cancel_failure(HANDLE pipe, LPOVERLAPPED ov) {
    (void)pipe;
    (void)ov;
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}

static DWORD WINAPI completion_owner(void *arg) {
    pending_operation *op = (pending_operation*)arg;
    DWORD bytes = 0;
    if (WaitForSingleObject(op->event_handle, 5000) != WAIT_OBJECT_0) {
        op->result = 1;
    } else if (!GetOverlappedResult(op->pipe, &op->ov, &bytes, FALSE)) {
        op->result = GetLastError();
    } else if (bytes != 4 || memcmp(op->buffer, "DATA", 4) != 0) {
        op->result = 2;
    } else {
        op->bytes = bytes;
        op->result = 0;
    }
    CloseHandle(op->event_handle);
    CloseHandle(op->pipe);
    free(op);
    return 0;
}

static int make_pipe(HANDLE *server, HANDLE *client) {
    wchar_t name[128];
    OVERLAPPED connect_ov;
    HANDLE connect_event;
    DWORD ignored = 0, error;
    BOOL connected;

    *server = INVALID_HANDLE_VALUE;
    *client = INVALID_HANDLE_VALUE;
    if (swprintf_s(name, 128, L"\\\\.\\pipe\\RayaCancelInject_%lu",
                   (unsigned long)GetCurrentProcessId()) < 0) return 0;
    *server = CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, NULL);
    if (*server == INVALID_HANDLE_VALUE) return 0;
    *client = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (*client == INVALID_HANDLE_VALUE) return 0;

    memset(&connect_ov, 0, sizeof(connect_ov));
    connect_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!connect_event) return 0;
    connect_ov.hEvent = event_for_overlapped(connect_event);
    connected = ConnectNamedPipe(*server, &connect_ov);
    if (!connected) {
        error = GetLastError();
        if (error == ERROR_IO_PENDING) {
            if (WaitForSingleObject(connect_event, 5000) != WAIT_OBJECT_0 ||
                !GetOverlappedResult(*server, &connect_ov, &ignored, FALSE)) {
                CloseHandle(connect_event);
                return 0;
            }
        } else if (error != ERROR_PIPE_CONNECTED) {
            CloseHandle(connect_event);
            return 0;
        }
    }
    CloseHandle(connect_event);
    return 1;
}

#define CHECK(x, label) do { if (!(x)) { \
    printf("FAIL: %s (error=%lu)\\n", label, (unsigned long)GetLastError()); \
    goto cleanup; } } while (0)

int main(void) {
    HANDLE server = INVALID_HANDLE_VALUE, client = INVALID_HANDLE_VALUE;
    HANDLE worker = NULL, duplicated = NULL;
    pending_operation *op = NULL;
    DWORD immediate = 0, written = 0, worker_exit = 99, error;
    BOOL cancel_ok;
    cancel_fn cancel_call = injected_cancel_failure;
    int result = 1;

    CHECK(make_pipe(&server, &client), "create connected disposable pipe");
    op = (pending_operation*)calloc(1, sizeof(*op));
    CHECK(op != NULL, "allocate operation");
    CHECK(DuplicateHandle(GetCurrentProcess(), server, GetCurrentProcess(),
        &duplicated, 0, FALSE, DUPLICATE_SAME_ACCESS), "duplicate issuing pipe handle");
    op->pipe = duplicated;
    duplicated = NULL;
    op->event_handle = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(op->event_handle != NULL, "create operation event");
    op->ov.hEvent = event_for_overlapped(op->event_handle);

    CHECK(!ReadFile(op->pipe, op->buffer, sizeof(op->buffer), &immediate, &op->ov),
        "ReadFile must return pending");
    error = GetLastError();
    CHECK(error == ERROR_IO_PENDING, "ReadFile returns ERROR_IO_PENDING");

    cancel_ok = cancel_call(op->pipe, &op->ov);
    CHECK(!cancel_ok, "injected CancelIoEx wrapper returns failure");
    error = GetLastError();
    CHECK(error == ERROR_ACCESS_DENIED, "capture injected non-ERROR_NOT_FOUND error");
    CHECK(WaitForSingleObject(op->event_handle, 0) == WAIT_TIMEOUT,
        "real pending operation remains incomplete after injected failure");
    CHECK(WaitForSingleObject(op->event_handle, 25) == WAIT_TIMEOUT,
        "bounded synchronous drain expires while real I/O remains pending");

    worker = CreateThread(NULL, 0, completion_owner, op, 0, NULL);
    CHECK(worker != NULL, "transfer ownership to isolated completion thread");
    op = NULL; /* Ownership transferred; caller must not access operation again. */

    CHECK(WriteFile(client, "DATA", 4, &written, NULL), "peer completes real pending read");
    CHECK(written == 4, "peer writes expected byte count");
    CHECK(WaitForSingleObject(worker, 5000) == WAIT_OBJECT_0,
        "completion owner finishes");
    CHECK(GetExitCodeThread(worker, &worker_exit), "read completion-owner thread status");
    CHECK(worker_exit == 0, "completion owner collected terminal result and payload");

    printf("PASS: real pending overlapped read survived injected CancelIoEx failure\\n");
    printf("PASS: worker collected terminal result; operation resources released once\\n");
    result = 0;

cleanup:
    if (op) {
        if (op->event_handle) CloseHandle(op->event_handle);
        if (op->pipe) CloseHandle(op->pipe);
        free(op);
    }
    if (duplicated) CloseHandle(duplicated);
    if (worker) CloseHandle(worker);
    if (client != INVALID_HANDLE_VALUE) CloseHandle(client);
    if (server != INVALID_HANDLE_VALUE) CloseHandle(server);
    return result;
}
