#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "read-frame.h"

#define CHECK(label, expression) do { \
    if (expression) { printf("PASS %s\n", label); ++passed; } \
    else { printf("FAIL %s (error=%lu)\n", label, (unsigned long)GetLastError()); ++failed; } \
} while (0)

static unsigned passed;
static unsigned failed;

static BOOL wait_for_completion(
    HANDLE pipe,
    OVERLAPPED *operation,
    DWORD timeout_ms,
    DWORD *transferred,
    DWORD *error_out)
{
    DWORD wait_result = WaitForSingleObject(operation->hEvent, timeout_ms);
    if (wait_result == WAIT_TIMEOUT) {
        *error_out = WAIT_TIMEOUT;
        return FALSE;
    }
    if (wait_result != WAIT_OBJECT_0) {
        *error_out = GetLastError();
        return FALSE;
    }

    if (GetOverlappedResult(pipe, operation, transferred, FALSE)) {
        *error_out = ERROR_SUCCESS;
        return TRUE;
    }

    *error_out = GetLastError();
    return FALSE;
}

static BOOL connect_pair(HANDLE *server_out, HANDLE *client_out)
{
    wchar_t pipe_name[128];
    OVERLAPPED connect_operation;
    HANDLE server = INVALID_HANDLE_VALUE;
    HANDLE client = INVALID_HANDLE_VALUE;
    DWORD transferred = 0;
    DWORD error = ERROR_SUCCESS;
    BOOL connected = FALSE;

    (void)swprintf_s(pipe_name, 128,
        L"\\\\.\\pipe\\RayaIoLifetime-%lu-%llu",
        (unsigned long)GetCurrentProcessId(),
        (unsigned long long)GetTickCount64());

    server = CreateNamedPipeW(
        pipe_name,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, 4096, 4096, 0, NULL);
    if (server == INVALID_HANDLE_VALUE)
        return FALSE;

    ZeroMemory(&connect_operation, sizeof(connect_operation));
    connect_operation.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (connect_operation.hEvent == NULL)
        goto fail;

    connected = ConnectNamedPipe(server, &connect_operation);
    if (!connected) {
        error = GetLastError();
        if (error != ERROR_IO_PENDING)
            goto fail;
    }

    client = CreateFileW(
        pipe_name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (client == INVALID_HANDLE_VALUE)
        goto fail;

    if (!connected) {
        if (!wait_for_completion(server, &connect_operation, 5000,
                &transferred, &error))
            goto fail;
    }

    CloseHandle(connect_operation.hEvent);
    *server_out = server;
    *client_out = client;
    return TRUE;

fail:
    if (connect_operation.hEvent != NULL) {
        if (!connected && error == ERROR_IO_PENDING) {
            (void)CancelIoEx(server, &connect_operation);
            (void)WaitForSingleObject(connect_operation.hEvent, INFINITE);
            (void)GetOverlappedResult(server, &connect_operation, &transferred, FALSE);
        }
        CloseHandle(connect_operation.hEvent);
    }
    if (client != INVALID_HANDLE_VALUE)
        CloseHandle(client);
    if (server != INVALID_HANDLE_VALUE)
        CloseHandle(server);
    SetLastError(error != ERROR_SUCCESS ? error : GetLastError());
    return FALSE;
}

static void test_cancel_pending_read(HANDLE server)
{
    OVERLAPPED operation;
    BYTE buffer[64];
    DWORD transferred = 0;
    DWORD completion_error = ERROR_SUCCESS;
    BOOL canceled;
    DWORD cancel_error = ERROR_SUCCESS;
    BOOL completed;

    ZeroMemory(&operation, sizeof(operation));
    ZeroMemory(buffer, sizeof(buffer));
    operation.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK("create event for pending read", operation.hEvent != NULL);
    if (operation.hEvent == NULL)
        return;

    completed = ReadFile(server, buffer, sizeof(buffer), &transferred, &operation);
    CHECK("read enters pending state",
        !completed && GetLastError() == ERROR_IO_PENDING);
    if (completed || GetLastError() != ERROR_IO_PENDING) {
        CloseHandle(operation.hEvent);
        return;
    }

    canceled = CancelIoEx(server, &operation);
    if (!canceled)
        cancel_error = GetLastError();
    CHECK("targeted CancelIoEx accepts pending read",
        canceled || cancel_error == ERROR_NOT_FOUND);

    completed = wait_for_completion(server, &operation, 5000,
        &transferred, &completion_error);
    if (!completed && completion_error == WAIT_TIMEOUT) {
        printf("FAIL cancellation completion exceeded 5 seconds; retaining resources until terminal completion\n");
        ++failed;
        (void)WaitForSingleObject(operation.hEvent, INFINITE);
        (void)GetOverlappedResult(server, &operation, &transferred, FALSE);
    }

    if (!completed && completion_error == ERROR_OPERATION_ABORTED) {
        printf("PASS canceled read completion observed as ERROR_OPERATION_ABORTED\n");
        ++passed;
    } else {
        printf("FAIL canceled read terminal status was %lu, expected %u\n",
            (unsigned long)completion_error, (unsigned)ERROR_OPERATION_ABORTED);
        ++failed;
    }

    CHECK("canceled read event remains valid until completion",
        WaitForSingleObject(operation.hEvent, 0) == WAIT_OBJECT_0);
    CloseHandle(operation.hEvent);
}

static void test_normal_completion_and_late_cancel(
    HANDLE server, HANDLE client)
{
    OVERLAPPED operation;
    BYTE buffer[8];
    const BYTE payload[] = { 'R', 'Y', 'A', '2' };
    DWORD transferred = 0;
    DWORD completion_error = ERROR_SUCCESS;
    DWORD written = 0;
    BOOL read_result;
    BOOL write_result;
    BOOL cancel_result;
    DWORD cancel_error = ERROR_SUCCESS;
    BOOL completed;

    ZeroMemory(&operation, sizeof(operation));
    ZeroMemory(buffer, sizeof(buffer));
    operation.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK("create event for normal read", operation.hEvent != NULL);
    if (operation.hEvent == NULL)
        return;

    read_result = ReadFile(server, buffer, sizeof(payload), &transferred, &operation);
    CHECK("normal read enters pending state",
        !read_result && GetLastError() == ERROR_IO_PENDING);
    if (read_result || GetLastError() != ERROR_IO_PENDING) {
        CloseHandle(operation.hEvent);
        return;
    }

    write_result = WriteFile(client, payload, sizeof(payload), &written, NULL);
    CHECK("client writes test payload", write_result && written == sizeof(payload));
    if (!write_result || written != sizeof(payload)) {
        (void)CancelIoEx(server, &operation);
        (void)WaitForSingleObject(operation.hEvent, INFINITE);
        (void)GetOverlappedResult(server, &operation, &transferred, FALSE);
        CloseHandle(operation.hEvent);
        return;
    }

    completed = wait_for_completion(server, &operation, 5000,
        &transferred, &completion_error);
    CHECK("normal read completion observed",
        completed && completion_error == ERROR_SUCCESS);
    CHECK("normal read payload and length verified",
        completed && transferred == sizeof(payload) &&
        memcmp(buffer, payload, sizeof(payload)) == 0);

    cancel_result = CancelIoEx(server, &operation);
    if (!cancel_result)
        cancel_error = GetLastError();
    CHECK("late cancellation does not imply pending ownership",
        !cancel_result && cancel_error == ERROR_NOT_FOUND);

    CloseHandle(operation.hEvent);
}

static void test_cancel_completion_race(HANDLE server, HANDLE client)
{
    OVERLAPPED operation;
    BYTE buffer[8];
    const BYTE payload[] = { 'R', 'A', 'C', 'E' };
    DWORD transferred = 0;
    DWORD written = 0;
    DWORD completion_error = ERROR_SUCCESS;
    DWORD cancel_error = ERROR_SUCCESS;
    BOOL read_result;
    BOOL write_result;
    BOOL cancel_result;
    BOOL completed;

    ZeroMemory(&operation, sizeof(operation));
    ZeroMemory(buffer, sizeof(buffer));
    operation.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK("create event for cancellation race", operation.hEvent != NULL);
    if (operation.hEvent == NULL)
        return;

    read_result = ReadFile(server, buffer, sizeof(payload), &transferred, &operation);
    CHECK("race read enters pending state",
        !read_result && GetLastError() == ERROR_IO_PENDING);
    if (read_result || GetLastError() != ERROR_IO_PENDING) {
        CloseHandle(operation.hEvent);
        return;
    }

    write_result = WriteFile(client, payload, sizeof(payload), &written, NULL);
    CHECK("race writer submits payload", write_result && written == sizeof(payload));
    if (!write_result || written != sizeof(payload)) {
        (void)CancelIoEx(server, &operation);
        (void)WaitForSingleObject(operation.hEvent, INFINITE);
        (void)GetOverlappedResult(server, &operation, &transferred, FALSE);
        CloseHandle(operation.hEvent);
        return;
    }

    cancel_result = CancelIoEx(server, &operation);
    if (!cancel_result)
        cancel_error = GetLastError();

    completed = wait_for_completion(server, &operation, 5000,
        &transferred, &completion_error);
    CHECK("cancel/completion race reaches terminal completion",
        completed || completion_error == ERROR_OPERATION_ABORTED);
    CHECK("race outcome is success or cancellation",
        (completed && completion_error == ERROR_SUCCESS &&
            transferred == sizeof(payload) &&
            memcmp(buffer, payload, sizeof(payload)) == 0) ||
        (!completed && completion_error == ERROR_OPERATION_ABORTED));
    CHECK("race cancellation result is a documented terminal race",
        cancel_result || cancel_error == ERROR_NOT_FOUND);

    CloseHandle(operation.hEvent);
}

static void test_request_deadline_with_per_frame_budget(HANDLE server);
int main(void)
{
    HANDLE server = INVALID_HANDLE_VALUE;
    HANDLE client = INVALID_HANDLE_VALUE;

    if (!connect_pair(&server, &client)) {
        fprintf(stderr, "FATAL named-pipe setup failed: %lu\n",
            (unsigned long)GetLastError());
        return 2;
    }

    printf("NAMED_PIPE_OVERLAPPED_LIFETIME_TEST_BEGIN\n");
    test_cancel_pending_read(server);
    test_normal_completion_and_late_cancel(server, client);
    test_cancel_completion_race(server, client);
    test_request_deadline_with_per_frame_budget(server);
    CloseHandle(client);
    CloseHandle(server);

    printf("NAMED_PIPE_OVERLAPPED_LIFETIME: %u passed, %u failed\n",
        passed, failed);
    return failed == 0 ? 0 : 1;
}

static BOOL timeout_read_and_reap(
    HANDLE pipe, DWORD timeout_ms, DWORD *terminal_error)
{
    OVERLAPPED operation;
    BYTE buffer[16];
    DWORD transferred = 0;
    DWORD wait_result;
    DWORD error = ERROR_SUCCESS;
    BOOL read_result;
    BOOL cancel_result;

    ZeroMemory(&operation, sizeof(operation));
    ZeroMemory(buffer, sizeof(buffer));
    operation.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (operation.hEvent == NULL) {
        *terminal_error = GetLastError();
        return FALSE;
    }

    read_result = ReadFile(pipe, buffer, sizeof(buffer), &transferred, &operation);
    if (read_result || GetLastError() != ERROR_IO_PENDING) {
        *terminal_error = read_result ? ERROR_SUCCESS : GetLastError();
        CloseHandle(operation.hEvent);
        return FALSE;
    }

    wait_result = WaitForSingleObject(operation.hEvent, timeout_ms);
    if (wait_result != WAIT_TIMEOUT) {
        if (wait_result == WAIT_OBJECT_0) {
            if (!GetOverlappedResult(pipe, &operation, &transferred, FALSE))
                error = GetLastError();
        } else {
            error = GetLastError();
        }
        *terminal_error = error;
        CloseHandle(operation.hEvent);
        return FALSE;
    }

    cancel_result = CancelIoEx(pipe, &operation);
    if (!cancel_result) {
        DWORD cancel_error = GetLastError();
        if (cancel_error != ERROR_NOT_FOUND) {
            *terminal_error = cancel_error;
            (void)WaitForSingleObject(operation.hEvent, INFINITE);
            (void)GetOverlappedResult(pipe, &operation, &transferred, FALSE);
            CloseHandle(operation.hEvent);
            return FALSE;
        }
    }

    /* The request deadline has expired, but ownership remains until completion. */
    wait_result = WaitForSingleObject(operation.hEvent, INFINITE);
    if (wait_result != WAIT_OBJECT_0) {
        *terminal_error = GetLastError();
        CloseHandle(operation.hEvent);
        return FALSE;
    }

    if (GetOverlappedResult(pipe, &operation, &transferred, FALSE))
        error = ERROR_SUCCESS;
    else
        error = GetLastError();

    *terminal_error = error;
    CloseHandle(operation.hEvent);
    return error == ERROR_OPERATION_ABORTED;
}

static void test_request_deadline_with_per_frame_budget(HANDLE server)
{
    raya_deadline deadline;
    uint64_t started = GetTickCount64();
    uint32_t first_budget;
    uint32_t second_budget;
    DWORD first_terminal = ERROR_SUCCESS;
    DWORD second_terminal = ERROR_SUCCESS;

    CHECK("initialize 500 ms overall request deadline",
        raya_deadline_init(&deadline, started, 500));
    first_budget = raya_deadline_frame_budget_ms(&deadline,
        GetTickCount64(), 200);
    CHECK("first frame receives 200 ms cap", first_budget == 200);
    CHECK("first pending read times out and is canceled safely",
        timeout_read_and_reap(server, first_budget, &first_terminal) &&
        first_terminal == ERROR_OPERATION_ABORTED);

    Sleep(150);
    second_budget = raya_deadline_frame_budget_ms(&deadline,
        GetTickCount64(), 200);
    CHECK("second frame budget is reduced by overall deadline",
        second_budget > 0 && second_budget < 200);
    CHECK("second pending read times out within remaining request budget",
        timeout_read_and_reap(server, second_budget, &second_terminal) &&
        second_terminal == ERROR_OPERATION_ABORTED);

    CHECK("overall request deadline is not reset per frame",
        raya_deadline_remaining_ms(&deadline, GetTickCount64()) == 0 &&
        raya_deadline_frame_budget_ms(&deadline, GetTickCount64(), 200) == 0);
}
