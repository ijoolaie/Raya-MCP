#define _WIN32_WINNT 0x0602
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "read-frame.h"

static unsigned passed;
static unsigned failed;

#define CHECK(label, expression) do { \
    if (expression) { printf("PASS %s\n", label); ++passed; } \
    else { printf("FAIL %s (error=%lu)\n", label, (unsigned long)GetLastError()); ++failed; } \
} while (0)

typedef struct read_outcome {
    DWORD bytes;
    DWORD io_error;
    DWORD terminal_error;
    DWORD budget_ms;
} read_outcome;

typedef struct delayed_write {
    HANDLE pipe;
    const uint8_t *bytes;
    DWORD length;
    DWORD delay_ms;
    BOOL success;
} delayed_write;

static BOOL create_message_pipe_pair(
    HANDLE *server_out, HANDLE *client_out, unsigned suffix)
{
    wchar_t name[128];
    HANDLE server = INVALID_HANDLE_VALUE;
    HANDLE client = INVALID_HANDLE_VALUE;
    OVERLAPPED connect_operation;
    DWORD transferred = 0;
    DWORD error = ERROR_SUCCESS;
    DWORD mode = PIPE_READMODE_MESSAGE;
    BOOL connected = FALSE;

    (void)swprintf_s(name, 128, L"\\\\.\\pipe\\RayaAsyncFrame-%lu-%u",
        (unsigned long)GetCurrentProcessId(), suffix);

    server = CreateNamedPipeW(name,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1, 65536, 65536, 0, NULL);
    if (server == INVALID_HANDLE_VALUE)
        return FALSE;

    ZeroMemory(&connect_operation, sizeof(connect_operation));
    connect_operation.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (connect_operation.hEvent == NULL)
        goto fail;

    connected = ConnectNamedPipe(server, &connect_operation);
    if (!connected) {
        error = GetLastError();
        if (error != ERROR_IO_PENDING && error != ERROR_PIPE_CONNECTED)
            goto fail;
    }

    client = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (client == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        goto fail;
    }

    if (!SetNamedPipeHandleState(client, &mode, NULL, NULL)) {
        error = GetLastError();
        goto fail;
    }

    if (!connected && error == ERROR_IO_PENDING) {
        if (WaitForSingleObject(connect_operation.hEvent, 5000) != WAIT_OBJECT_0 ||
            !GetOverlappedResult(server, &connect_operation, &transferred, FALSE)) {
            error = GetLastError();
            goto fail;
        }
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
    SetLastError(error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE);
    return FALSE;
}
static BOOL write_message(HANDLE pipe, const uint8_t *bytes, DWORD length)
{
    DWORD written = 0;
    if (!WriteFile(pipe, bytes, length, &written, NULL))
        return FALSE;
    if (written != length) {
        SetLastError(ERROR_WRITE_FAULT);
        return FALSE;
    }
    return TRUE;
}

/* The operation and buffer remain alive until terminal completion is observed.
   Timeout requests cancellation, then drains completion before the stack frame
   and event are released. This is a bounded-wait experiment, not a production
   reaper: the final drain intentionally retains ownership if completion is late. */
static BOOL read_message_with_deadline(
    HANDLE pipe,
    uint8_t *buffer,
    DWORD capacity,
    const raya_deadline *deadline,
    DWORD per_frame_timeout_ms,
    read_outcome *outcome)
{
    OVERLAPPED operation;
    DWORD transferred = 0;
    DWORD error = ERROR_SUCCESS;
    DWORD wait_result;
    BOOL started;

    if (outcome == NULL || deadline == NULL || buffer == NULL || capacity == 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    ZeroMemory(outcome, sizeof(*outcome));
    ZeroMemory(&operation, sizeof(operation));
    operation.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (operation.hEvent == NULL)
        return FALSE;

    started = ReadFile(pipe, buffer, capacity, NULL, &operation);
    if (started) {
        if (GetOverlappedResult(pipe, &operation, &transferred, FALSE)) {
            outcome->bytes = transferred;
            outcome->io_error = ERROR_SUCCESS;
            outcome->terminal_error = ERROR_SUCCESS;
            CloseHandle(operation.hEvent);
            return TRUE;
        }
        error = GetLastError();
        outcome->bytes = transferred;
        outcome->io_error = error;
        outcome->terminal_error = error;
        CloseHandle(operation.hEvent);
        SetLastError(error);
        return FALSE;
    }

    error = GetLastError();
    if (error != ERROR_IO_PENDING) {
        if (error == ERROR_MORE_DATA) {
            (void)GetOverlappedResult(pipe, &operation, &transferred, FALSE);
            outcome->bytes = transferred;
            outcome->io_error = ERROR_MORE_DATA;
            outcome->terminal_error = ERROR_MORE_DATA;
        } else {
            outcome->io_error = error;
            outcome->terminal_error = error;
        }
        CloseHandle(operation.hEvent);
        SetLastError(error);
        return FALSE;
    }

    outcome->budget_ms = raya_deadline_frame_budget_ms(
        deadline, GetTickCount64(), per_frame_timeout_ms);
    if (outcome->budget_ms == 0) {
        outcome->io_error = WAIT_TIMEOUT;
        (void)CancelIoEx(pipe, &operation);
        (void)WaitForSingleObject(operation.hEvent, INFINITE);
        if (!GetOverlappedResult(pipe, &operation, &transferred, FALSE))
            outcome->terminal_error = GetLastError();
        else
            outcome->terminal_error = ERROR_SUCCESS;
        outcome->bytes = transferred;
        CloseHandle(operation.hEvent);
        SetLastError(WAIT_TIMEOUT);
        return FALSE;
    }

    wait_result = WaitForSingleObject(operation.hEvent, outcome->budget_ms);
    if (wait_result == WAIT_OBJECT_0) {
        if (GetOverlappedResult(pipe, &operation, &transferred, FALSE)) {
            outcome->bytes = transferred;
            outcome->io_error = ERROR_SUCCESS;
            outcome->terminal_error = ERROR_SUCCESS;
            CloseHandle(operation.hEvent);
            return TRUE;
        }
        error = GetLastError();
        outcome->bytes = transferred;
        outcome->io_error = error;
        outcome->terminal_error = error;
        CloseHandle(operation.hEvent);
        SetLastError(error);
        return FALSE;
    }

    if (wait_result == WAIT_TIMEOUT) {
        outcome->io_error = WAIT_TIMEOUT;
        (void)CancelIoEx(pipe, &operation);
        /* Do not free OVERLAPPED, event or buffer until terminal completion. */
        (void)WaitForSingleObject(operation.hEvent, INFINITE);
        if (GetOverlappedResult(pipe, &operation, &transferred, FALSE))
            outcome->terminal_error = ERROR_SUCCESS;
        else
            outcome->terminal_error = GetLastError();
        outcome->bytes = transferred;
        CloseHandle(operation.hEvent);
        SetLastError(WAIT_TIMEOUT);
        return FALSE;
    }

    error = GetLastError();
    outcome->io_error = error;
    (void)CancelIoEx(pipe, &operation);
    (void)WaitForSingleObject(operation.hEvent, INFINITE);
    if (!GetOverlappedResult(pipe, &operation, &transferred, FALSE))
        outcome->terminal_error = GetLastError();
    else
        outcome->terminal_error = ERROR_SUCCESS;
    outcome->bytes = transferred;
    CloseHandle(operation.hEvent);
    SetLastError(error);
    return FALSE;
}
static DWORD WINAPI delayed_writer(void *parameter)
{
    delayed_write *write = (delayed_write *)parameter;
    Sleep(write->delay_ms);
    write->success = write_message(write->pipe, write->bytes, write->length);
    return 0;
}

static BOOL encode_ack(uint32_t sequence, uint8_t *wire, size_t capacity, size_t *length)
{
    uint8_t payload[4];
    raya_frame_view frame;
    payload[0] = (uint8_t)sequence;
    payload[1] = (uint8_t)(sequence >> 8);
    payload[2] = (uint8_t)(sequence >> 16);
    payload[3] = (uint8_t)(sequence >> 24);
    frame.type = RAYA_FRAME_ACK;
    frame.sequence = sequence;
    frame.payload = payload;
    frame.payload_length = sizeof(payload);
    return raya_frame_encode(&frame, wire, capacity, length);
}

static void test_overlapped_message_frame_success(void)
{
    HANDLE server = INVALID_HANDLE_VALUE;
    HANDLE client = INVALID_HANDLE_VALUE;
    uint8_t ack_wire[RAYA_FRAME_HEADER_SIZE + 4u];
    uint8_t received[RAYA_FRAME_WIRE_CAPACITY];
    size_t ack_length = 0;
   raya_deadline deadline;
    read_outcome outcome;
    raya_frame_view decoded;
    delayed_write writer;
    HANDLE writer_thread = NULL;

    CHECK("create overlapped message-mode pipe pair",
        create_message_pipe_pair(&server, &client, 1));
    if (server == INVALID_HANDLE_VALUE || client == INVALID_HANDLE_VALUE)
        return;

    CHECK("encode sequence-zero ACK frame",
        encode_ack(0, ack_wire, sizeof(ack_wire), &ack_length));
    writer.pipe = client;
    writer.bytes = ack_wire;
    writer.length = (DWORD)ack_length;
    writer.delay_ms = 40;
    writer.success = FALSE;
    writer_thread = CreateThread(NULL, 0, delayed_writer, &writer, 0, NULL);
    CHECK("start delayed ACK writer", writer_thread != NULL);

    CHECK("initialize overall request deadline",
        raya_deadline_init(&deadline, GetTickCount64(), 1000));
    CHECK("overlapped message read completes before frame deadline",
        read_message_with_deadline(server, received, sizeof(received),
            &deadline, 250, &outcome));
    CHECK("overlapped message returns one complete ACK frame",
        outcome.bytes == ack_length &&
        raya_frame_decode(received, outcome.bytes, &decoded) &&
        decoded.type == RAYA_FRAME_ACK && decoded.sequence == 0);
    if (writer_thread != NULL) {
        (void)WaitForSingleObject(writer_thread, 5000);
        CloseHandle(writer_thread);
    }
    CHECK("delayed ACK writer completed successfully", writer.success);

    CloseHandle(client);
    CloseHandle(server);
}

static void test_overlapped_message_timeout_and_cancel(void)
{
    HANDLE server = INVALID_HANDLE_VALUE;
    HANDLE client = INVALID_HANDLE_VALUE;
    uint8_t buffer[RAYA_FRAME_WIRE_CAPACITY];
    raya_deadline deadline;
    read_outcome outcome;

    CHECK("create pipe for overlapped message timeout",
        create_message_pipe_pair(&server, &client, 2));
    if (server == INVALID_HANDLE_VALUE || client == INVALID_HANDLE_VALUE)
        return;

    CHECK("initialize short overall request deadline",
        raya_deadline_init(&deadline, GetTickCount64(), 500));
    CHECK("pending message read times out and is canceled",
        !read_message_with_deadline(server, buffer, sizeof(buffer),
            &deadline, 80, &outcome) &&
        outcome.io_error == WAIT_TIMEOUT);
    CHECK("timeout cancellation reaches terminal completion",
        outcome.terminal_error == ERROR_OPERATION_ABORTED ||
        outcome.terminal_error == ERROR_SUCCESS);
    DWORD handle_flags = 0;
    CHECK("timed-out pipe HANDLE remains valid after cancellation drain",
        GetHandleInformation(server, &handle_flags));

    CloseHandle(client);
    CloseHandle(server);
}

static void test_overlapped_message_shared_deadline(void)
{
    HANDLE server = INVALID_HANDLE_VALUE;
    HANDLE client = INVALID_HANDLE_VALUE;
    uint8_t ack_wire[RAYA_FRAME_HEADER_SIZE + 4u];
    uint8_t received[RAYA_FRAME_WIRE_CAPACITY];
    size_t ack_length = 0;
    raya_deadline deadline;
    read_outcome first;
    read_outcome second;
    delayed_write writer;
    HANDLE writer_thread = NULL;

    CHECK("create pipe for shared message deadline",
        create_message_pipe_pair(&server, &client, 3));
    if (server == INVALID_HANDLE_VALUE || client == INVALID_HANDLE_VALUE)
        return;

    CHECK("encode first ACK for shared deadline",
        encode_ack(0, ack_wire, sizeof(ack_wire), &ack_length));
    writer.pipe = client;
    writer.bytes = ack_wire;
    writer.length = (DWORD)ack_length;
    writer.delay_ms = 70;
    writer.success = FALSE;
    writer_thread = CreateThread(NULL, 0, delayed_writer, &writer, 0, NULL);

    CHECK("initialize shared 250 ms request deadline",
        raya_deadline_init(&deadline, GetTickCount64(), 250));
    CHECK("first message read completes within 150 ms frame cap",
        read_message_with_deadline(server, received, sizeof(received),
            &deadline, 150, &first));
    CHECK("first message was actually received", first.bytes == ack_length);
    if (writer_thread != NULL) {
        (void)WaitForSingleObject(writer_thread, 5000);
        CloseHandle(writer_thread);
    }

    Sleep(140);
    CHECK("second message budget is reduced by same request deadline",
        raya_deadline_frame_budget_ms(&deadline, GetTickCount64(), 150) > 0 &&
        raya_deadline_frame_budget_ms(&deadline, GetTickCount64(), 150) < 70);
    CHECK("second pending message read times out under remaining request budget",
        !read_message_with_deadline(server, received, sizeof(received),
            &deadline, 150, &second) &&
        second.io_error == WAIT_TIMEOUT);
    CHECK("second timeout is drained to terminal cancellation",
        second.terminal_error == ERROR_OPERATION_ABORTED ||
        second.terminal_error == ERROR_SUCCESS);

    CloseHandle(client);
    CloseHandle(server);
}

static void test_overlapped_message_error_more_data(void)
{
    HANDLE server = INVALID_HANDLE_VALUE;
    HANDLE client = INVALID_HANDLE_VALUE;
    uint8_t message[128];
    uint8_t small[32];
    raya_deadline deadline;
    read_outcome outcome;

    memset(message, 0x5a, sizeof(message));
    CHECK("create pipe for overlapped ERROR_MORE_DATA",
        create_message_pipe_pair(&server, &client, 4));
    if (server == INVALID_HANDLE_VALUE || client == INVALID_HANDLE_VALUE)
        return;

    CHECK("write oversized message before overlapped read",
        write_message(client, message, sizeof(message)));
    CHECK("initialize ERROR_MORE_DATA request deadline",
        raya_deadline_init(&deadline, GetTickCount64(), 500));
    CHECK("overlapped message read rejects short frame buffer",
        !read_message_with_deadline(server, small, sizeof(small),
            &deadline, 200, &outcome) &&
        outcome.io_error == ERROR_MORE_DATA &&
        outcome.bytes == sizeof(small));

    CloseHandle(client);
    CloseHandle(server);
}

static void test_overlapped_message_disconnect(void)
{
    HANDLE server = INVALID_HANDLE_VALUE;
    HANDLE client = INVALID_HANDLE_VALUE;
    uint8_t buffer[RAYA_FRAME_WIRE_CAPACITY];
    raya_deadline deadline;
    read_outcome outcome;

    CHECK("create pipe for overlapped disconnect",
        create_message_pipe_pair(&server, &client, 5));
    if (server == INVALID_HANDLE_VALUE || client == INVALID_HANDLE_VALUE)
        return;

    CHECK("initialize disconnect request deadline",
        raya_deadline_init(&deadline, GetTickCount64(), 500));
    CloseHandle(client);
    client = INVALID_HANDLE_VALUE;
    CHECK("overlapped read reports disconnected peer",
        !read_message_with_deadline(server, buffer, sizeof(buffer),
            &deadline, 300, &outcome) &&
        (outcome.io_error == ERROR_BROKEN_PIPE ||
         outcome.io_error == ERROR_NO_DATA));

    CloseHandle(server);
}

int main(void)
{
    printf("OVERLAPPED_MESSAGE_FRAME_TEST_BEGIN\n");
    test_overlapped_message_frame_success();
    test_overlapped_message_timeout_and_cancel();
    test_overlapped_message_shared_deadline();
    test_overlapped_message_error_more_data();
    test_overlapped_message_disconnect();
    printf("OVERLAPPED_MESSAGE_FRAME_TESTS: %u passed, %u failed\n",
        passed, failed);
    return failed == 0 ? 0 : 1;
}
