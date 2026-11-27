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

typedef struct heap_message_read {
    OVERLAPPED operation;
    HANDLE pipe;
    uint8_t *buffer;
    DWORD capacity;
    struct message_reaper_ticket *ticket;
} heap_message_read;

typedef struct message_reaper_ticket {
    HANDLE done_event;
    DWORD bytes;
    DWORD terminal_error;
} message_reaper_ticket;

static volatile LONG active_message_reapers;
static HANDLE message_reaper_gate;

static DWORD WINAPI reap_heap_message_read(void *parameter)
{
    heap_message_read *pending = (heap_message_read *)parameter;
    DWORD transferred = 0;
    if (message_reaper_gate != NULL)
        (void)WaitForSingleObject(message_reaper_gate, INFINITE);
    BOOL completed = GetOverlappedResult(
        pending->pipe, &pending->operation, &transferred, FALSE);

    pending->ticket->bytes = transferred;
    pending->ticket->terminal_error =
        completed ? ERROR_SUCCESS : GetLastError();

    CloseHandle(pending->operation.hEvent);
    CloseHandle(pending->pipe);
    HeapFree(GetProcessHeap(), 0, pending->buffer);
    InterlockedDecrement(&active_message_reapers);
    SetEvent(pending->ticket->done_event);
    HeapFree(GetProcessHeap(), 0, pending);
    return 0;
}

/* On timeout, ownership of the pending OVERLAPPED, buffer and pipe HANDLE is
   transferred to a heap-owned reaper thread. The request returns immediately;
   the caller retains only a ticket event and must keep it alive until signaled. */
static BOOL read_message_with_heap_reaper(
    HANDLE *pipe_in_out,
    uint8_t *result_buffer,
    DWORD result_capacity,
    const raya_deadline *deadline,
    DWORD per_frame_timeout_ms,
    read_outcome *outcome,
    message_reaper_ticket *ticket)
{
    heap_message_read *pending = NULL;
    DWORD immediate = 0;
    DWORD transferred = 0;
    DWORD wait_result;
    DWORD error;
    BOOL started;
    HANDLE thread = NULL;

    if (pipe_in_out == NULL || *pipe_in_out == NULL ||
        *pipe_in_out == INVALID_HANDLE_VALUE ||
        result_buffer == NULL || result_capacity == 0 ||
        deadline == NULL || outcome == NULL || ticket == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    ZeroMemory(outcome, sizeof(*outcome));
    ZeroMemory(ticket, sizeof(*ticket));
    ticket->done_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (ticket->done_event == NULL)
        return FALSE;

    if (InterlockedIncrement(&active_message_reapers) > 4) {
        InterlockedDecrement(&active_message_reapers);
        CloseHandle(ticket->done_event);
        ticket->done_event = NULL;
        SetLastError(ERROR_BUSY);
        return FALSE;
    }

    pending = (heap_message_read *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*pending));
    if (pending == NULL)
        goto fail_before_issue;
    pending->buffer = (uint8_t *)HeapAlloc(
        GetProcessHeap(), 0, result_capacity);
    if (pending->buffer == NULL)
        goto fail_before_issue;
    pending->capacity = result_capacity;
    pending->pipe = *pipe_in_out;
    pending->ticket = ticket;
    pending->operation.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (pending->operation.hEvent == NULL)
        goto fail_before_issue;

    started = ReadFile(pending->pipe, pending->buffer, pending->capacity,
        &immediate, &pending->operation);
    if (started) {
        if (immediate > result_capacity) {
            SetLastError(ERROR_INVALID_DATA);
            goto fail_after_issue;
        }
        memcpy(result_buffer, pending->buffer, immediate);
        outcome->bytes = immediate;
        outcome->io_error = ERROR_SUCCESS;
        outcome->terminal_error = ERROR_SUCCESS;
        CloseHandle(pending->operation.hEvent);
        CloseHandle(ticket->done_event);
        ticket->done_event = NULL;
        HeapFree(GetProcessHeap(), 0, pending->buffer);
        HeapFree(GetProcessHeap(), 0, pending);
        InterlockedDecrement(&active_message_reapers);
        return TRUE;
    }

    error = GetLastError();
    if (error != ERROR_IO_PENDING) {
        outcome->io_error = error;
        outcome->terminal_error = error;
        goto fail_after_issue;
    }

    outcome->budget_ms = raya_deadline_frame_budget_ms(
        deadline, GetTickCount64(), per_frame_timeout_ms);
    wait_result = outcome->budget_ms == 0
        ? WAIT_TIMEOUT
        : WaitForSingleObject(pending->operation.hEvent, outcome->budget_ms);

    if (wait_result == WAIT_OBJECT_0) {
        if (GetOverlappedResult(pending->pipe, &pending->operation,
                &transferred, FALSE)) {
            if (transferred > result_capacity) {
                SetLastError(ERROR_INVALID_DATA);
                goto fail_after_issue;
            }
            memcpy(result_buffer, pending->buffer, transferred);
            outcome->bytes = transferred;
            outcome->io_error = ERROR_SUCCESS;
            outcome->terminal_error = ERROR_SUCCESS;
            CloseHandle(pending->operation.hEvent);
            CloseHandle(ticket->done_event);
            ticket->done_event = NULL;
            HeapFree(GetProcessHeap(), 0, pending->buffer);
            HeapFree(GetProcessHeap(), 0, pending);
            InterlockedDecrement(&active_message_reapers);
            return TRUE;
        }
        error = GetLastError();
        outcome->bytes = transferred;
        outcome->io_error = error;
        outcome->terminal_error = error;
        goto fail_after_issue;
    }

    if (wait_result != WAIT_TIMEOUT) {
        error = GetLastError();
        outcome->io_error = error;
        (void)CancelIoEx(pending->pipe, &pending->operation);
        (void)WaitForSingleObject(pending->operation.hEvent, INFINITE);
        if (!GetOverlappedResult(pending->pipe, &pending->operation,
                &transferred, FALSE))
            outcome->terminal_error = GetLastError();
        else
            outcome->terminal_error = ERROR_SUCCESS;
        outcome->bytes = transferred;
        goto fail_after_issue;
    }

    outcome->io_error = WAIT_TIMEOUT;
    (void)CancelIoEx(pending->pipe, &pending->operation);
    thread = CreateThread(NULL, 0, reap_heap_message_read, pending, 0, NULL);
    if (thread == NULL) {
        /* If reaper startup fails, retain ownership and drain synchronously. */
        (void)WaitForSingleObject(pending->operation.hEvent, INFINITE);
        if (!GetOverlappedResult(pending->pipe, &pending->operation,
                &transferred, FALSE))
            outcome->terminal_error = GetLastError();
        else
            outcome->terminal_error = ERROR_SUCCESS;
        outcome->bytes = transferred;
        goto fail_after_issue;
    }

    CloseHandle(thread);
    *pipe_in_out = INVALID_HANDLE_VALUE; /* Pipe HANDLE is now reaper-owned. */
    return FALSE;

fail_before_issue:
    error = GetLastError();
    if (pending != NULL) {
        if (pending->operation.hEvent != NULL)
            CloseHandle(pending->operation.hEvent);
        if (pending->buffer != NULL)
            HeapFree(GetProcessHeap(), 0, pending->buffer);
        HeapFree(GetProcessHeap(), 0, pending);
    }
    InterlockedDecrement(&active_message_reapers);
    CloseHandle(ticket->done_event);
    ticket->done_event = NULL;
    SetLastError(error);
    return FALSE;

fail_after_issue:
    error = GetLastError();
    if (pending->operation.hEvent != NULL)
        CloseHandle(pending->operation.hEvent);
    if (pending->buffer != NULL)
        HeapFree(GetProcessHeap(), 0, pending->buffer);
    HeapFree(GetProcessHeap(), 0, pending);
    InterlockedDecrement(&active_message_reapers);
    CloseHandle(ticket->done_event);
    ticket->done_event = NULL;
    SetLastError(error);
    return FALSE;
}

static void test_heap_message_read_normal_completion(void)
{
    HANDLE server = INVALID_HANDLE_VALUE;
    HANDLE client = INVALID_HANDLE_VALUE;
    uint8_t ack_wire[RAYA_FRAME_HEADER_SIZE + 4u];
    uint8_t received[RAYA_FRAME_WIRE_CAPACITY];
    size_t ack_length = 0;
    raya_deadline deadline;
    read_outcome outcome;
    message_reaper_ticket ticket;
    raya_frame_view decoded;
    delayed_write writer;
    HANDLE writer_thread = NULL;

    CHECK("create pipe for heap-owned normal message read",
        create_message_pipe_pair(&server, &client, 7));
    if (server == INVALID_HANDLE_VALUE || client == INVALID_HANDLE_VALUE)
        return;

    CHECK("encode ACK for heap-owned normal read",
        encode_ack(0, ack_wire, sizeof(ack_wire), &ack_length));
    writer.pipe = client;
    writer.bytes = ack_wire;
    writer.length = (DWORD)ack_length;
    writer.delay_ms = 40;
    writer.success = FALSE;
    writer_thread = CreateThread(NULL, 0, delayed_writer, &writer, 0, NULL);

    CHECK("initialize heap-owned normal read deadline",
        raya_deadline_init(&deadline, GetTickCount64(), 1000));
    CHECK("heap-owned message read completes without reaper handoff",
        read_message_with_heap_reaper(&server, received, sizeof(received),
            &deadline, 250, &outcome, &ticket));
    CHECK("normal completion returns decoded ACK and closes unused ticket",
        outcome.bytes == ack_length && ticket.done_event == NULL &&
        raya_frame_decode(received, outcome.bytes, &decoded) &&
        decoded.type == RAYA_FRAME_ACK && decoded.sequence == 0);
    if (writer_thread != NULL) {
        (void)WaitForSingleObject(writer_thread, 5000);
        CloseHandle(writer_thread);
    }
    CHECK("normal heap-owned read leaves no reaper slot",
        writer.success && InterlockedCompareExchange(&active_message_reapers, 0, 0) == 0);

    CloseHandle(client);
    CloseHandle(server);
}
static void test_heap_reaper_owns_timed_out_message_read(void)
{
    HANDLE server = INVALID_HANDLE_VALUE;
    HANDLE client = INVALID_HANDLE_VALUE;
    uint8_t result_buffer[RAYA_FRAME_WIRE_CAPACITY];
    raya_deadline deadline;
    read_outcome outcome;
    message_reaper_ticket ticket;

    CHECK("create message pipe for heap reaper ownership",
        create_message_pipe_pair(&server, &client, 6));
    if (server == INVALID_HANDLE_VALUE || client == INVALID_HANDLE_VALUE)
        return;

    CHECK("initialize heap-reaper request deadline",
        raya_deadline_init(&deadline, GetTickCount64(), 500));
    CHECK("timed-out message read transfers to heap reaper",
        !read_message_with_heap_reaper(&server, result_buffer,
            sizeof(result_buffer), &deadline, 60, &outcome, &ticket) &&
        outcome.io_error == WAIT_TIMEOUT &&
        server == INVALID_HANDLE_VALUE &&
        ticket.done_event != NULL);
    CHECK("request returns while reaper retains pending I/O resources",
        InterlockedCompareExchange(&active_message_reapers, 0, 0) == 1);

    CloseHandle(client);
    client = INVALID_HANDLE_VALUE;
    CHECK("heap reaper observes terminal cancellation and cleans resources",
        WaitForSingleObject(ticket.done_event, 5000) == WAIT_OBJECT_0 &&
        ticket.terminal_error == ERROR_OPERATION_ABORTED &&
        InterlockedCompareExchange(&active_message_reapers, 0, 0) == 0);
    CloseHandle(ticket.done_event);
}
static void test_heap_message_reaper_admission_cap(void)
{
    HANDLE servers[5];
    HANDLE clients[5];
    message_reaper_ticket tickets[4];
    message_reaper_ticket rejected_ticket;
    uint8_t result_buffer[RAYA_FRAME_WIRE_CAPACITY];
    raya_deadline deadline;
    read_outcome outcome;
    BOOL started[4] = { FALSE, FALSE, FALSE, FALSE };

    ZeroMemory(servers, sizeof(servers));
    ZeroMemory(clients, sizeof(clients));
    ZeroMemory(tickets, sizeof(tickets));
    ZeroMemory(&rejected_ticket, sizeof(rejected_ticket));
    message_reaper_gate = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK("create heap message reaper gate", message_reaper_gate != NULL);
    if (message_reaper_gate == NULL)
        return;

    for (unsigned i = 0; i < 5; ++i) {
        CHECK("create bounded-reaper message pipe pair",
            create_message_pipe_pair(&servers[i], &clients[i], 10u + i));
        if (servers[i] == INVALID_HANDLE_VALUE ||
            clients[i] == INVALID_HANDLE_VALUE) {
            for (unsigned j = 0; j <= i; ++j) {
                if (clients[j] != NULL && clients[j] != INVALID_HANDLE_VALUE)
                    CloseHandle(clients[j]);
                if (servers[j] != NULL && servers[j] != INVALID_HANDLE_VALUE)
                    CloseHandle(servers[j]);
            }
            SetEvent(message_reaper_gate);
            CloseHandle(message_reaper_gate);
            message_reaper_gate = NULL;
            return;
        }
    }

    for (unsigned i = 0; i < 4; ++i) {
        CHECK("initialize bounded-reaper request deadline",
            raya_deadline_init(&deadline, GetTickCount64(), 500));
        started[i] = !read_message_with_heap_reaper(&servers[i],
            result_buffer, sizeof(result_buffer), &deadline, 40,
            &outcome, &tickets[i]) &&
            outcome.io_error == WAIT_TIMEOUT &&
            servers[i] == INVALID_HANDLE_VALUE &&
            tickets[i].done_event != NULL;
        CHECK("admit one of four bounded heap reaper operations", started[i]);
        CloseHandle(clients[i]);
        clients[i] = INVALID_HANDLE_VALUE;
    }

    CHECK("fifth outstanding reaper operation is rejected at hard cap",
        !read_message_with_heap_reaper(&servers[4], result_buffer,
            sizeof(result_buffer), &deadline, 40, &outcome, &rejected_ticket) &&
        GetLastError() == ERROR_BUSY &&
        servers[4] != INVALID_HANDLE_VALUE && rejected_ticket.done_event == NULL);
    CloseHandle(clients[4]);
    CloseHandle(servers[4]);
    clients[4] = INVALID_HANDLE_VALUE;
    servers[4] = INVALID_HANDLE_VALUE;

    SetEvent(message_reaper_gate);
    for (unsigned i = 0; i < 4; ++i) {
        CHECK("bounded reaper drains canceled operation",
            started[i] &&
            WaitForSingleObject(tickets[i].done_event, 5000) == WAIT_OBJECT_0 &&
            tickets[i].terminal_error == ERROR_OPERATION_ABORTED);
        if (tickets[i].done_event != NULL)
            CloseHandle(tickets[i].done_event);
    }
    CHECK("bounded heap reaper registry returns to zero",
        InterlockedCompareExchange(&active_message_reapers, 0, 0) == 0);
    CloseHandle(message_reaper_gate);
    message_reaper_gate = NULL;
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
    test_heap_message_read_normal_completion();
    test_heap_reaper_owns_timed_out_message_read();
    test_heap_message_reaper_admission_cap();
    test_overlapped_message_timeout_and_cancel();
    test_overlapped_message_shared_deadline();
    test_overlapped_message_error_more_data();
    test_overlapped_message_disconnect();
    printf("OVERLAPPED_MESSAGE_FRAME_TESTS: %u passed, %u failed\n",
        passed, failed);
    return failed == 0 ? 0 : 1;
}
