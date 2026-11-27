#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "read-frame.h"

static unsigned passed;
static unsigned failed;

#define CHECK(label, expression) do { \
    if (expression) { printf("PASS %s\n", label); ++passed; } \
    else { printf("FAIL %s (error=%lu)\n", label, (unsigned long)GetLastError()); ++failed; } \
} while (0)

static BOOL create_message_pipe_pair(
    HANDLE *server_out, HANDLE *client_out, unsigned suffix)
{
    wchar_t name[128];
    HANDLE server;
    HANDLE client;
    BOOL connected;
    DWORD error;
    DWORD mode = PIPE_READMODE_MESSAGE;

    (void)swprintf_s(name, 128, L"\\\\.\\pipe\\RayaFrameTransport-%lu-%u",
        (unsigned long)GetCurrentProcessId(), suffix);

    server = CreateNamedPipeW(name,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1, 65536, 65536, 0, NULL);
    if (server == INVALID_HANDLE_VALUE)
        return FALSE;

    client = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (client == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        CloseHandle(server);
        SetLastError(error);
        return FALSE;
    }

    connected = ConnectNamedPipe(server, NULL);
    if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
        error = GetLastError();
        CloseHandle(client);
        CloseHandle(server);
        SetLastError(error);
        return FALSE;
    }

    if (!SetNamedPipeHandleState(client, &mode, NULL, NULL)) {
        error = GetLastError();
        CloseHandle(client);
        CloseHandle(server);
        SetLastError(error);
        return FALSE;
    }

    *server_out = server;
    *client_out = client;
    return TRUE;
}

static BOOL write_message(HANDLE pipe, const uint8_t *bytes, DWORD length)
{
    DWORD written = 0;
    return WriteFile(pipe, bytes, length, &written, NULL) && written == length;
}

static BOOL read_message(
    HANDLE pipe, uint8_t *buffer, DWORD capacity, DWORD *received, DWORD *error)
{
    BOOL result;
    *received = 0;
    result = ReadFile(pipe, buffer, capacity, received, NULL);
    *error = result ? ERROR_SUCCESS : GetLastError();
    return result;
}

static BOOL transfer_response_over_message_pipe(
    uint32_t total_bytes, uint32_t chunk_size, unsigned suffix)
{
    static uint8_t body[RAYA_MAX_RESPONSE_BYTES];
    uint8_t wire[RAYA_FRAME_WIRE_CAPACITY];
    uint8_t ack_wire[RAYA_FRAME_HEADER_SIZE + 4u];
    uint8_t incoming_wire[RAYA_FRAME_WIRE_CAPACITY];
    uint8_t incoming_ack[RAYA_FRAME_HEADER_SIZE + 4u];
    HANDLE server = INVALID_HANDLE_VALUE;
    HANDLE client = INVALID_HANDLE_VALUE;
    raya_response_tx tx;
    raya_response_rx rx;
    uint32_t offset = 0;
    unsigned frames = 0;
    BOOL success = FALSE;

    for (uint32_t i = 0; i < total_bytes; ++i)
        body[i] = (uint8_t)(i * 13u + 0x31u);

    if (!create_message_pipe_pair(&server, &client, suffix) ||
        !raya_response_tx_init(&tx, total_bytes, chunk_size))
        goto done;

    raya_response_rx_init(&rx);

    while (!tx.complete) {
        raya_frame_view outgoing;
        raya_frame_view received;
        raya_frame_view ack;
        raya_frame_view received_ack;
        size_t wire_length = 0;
        size_t ack_length = 0;
        DWORD received_length = 0;
        DWORD io_error = ERROR_SUCCESS;
        DWORD data_length = 0;
        const uint8_t *data = NULL;

        if (tx.header_sent && tx.sent_bytes < tx.total_bytes) {
            uint32_t remaining = tx.total_bytes - tx.sent_bytes;
            data_length = remaining < tx.chunk_size ? remaining : tx.chunk_size;
            data = body + offset;
        }

        if (!raya_response_tx_next(&tx, data, data_length, &outgoing) ||
            !raya_frame_encode(&outgoing, wire, sizeof(wire), &wire_length) ||
            !write_message(client, wire, (DWORD)wire_length))
            goto done;

        if (!read_message(server, incoming_wire, sizeof(incoming_wire),
                &received_length, &io_error))
            goto done;

        if (!raya_frame_decode(incoming_wire, received_length, &received) ||
            !raya_response_rx_accept(&rx, &received))
            goto done;

        if (received.type == RAYA_FRAME_DATA) {
            if (received.payload_length != data_length ||
                memcmp(received.payload, body + offset, data_length) != 0)
                goto done;
            offset += data_length;
        }

        if (!raya_response_rx_make_ack(&rx, &ack) ||
            !raya_frame_encode(&ack, ack_wire, sizeof(ack_wire), &ack_length) ||
            !write_message(server, ack_wire, (DWORD)ack_length))
            goto done;

        if (!read_message(client, incoming_ack, sizeof(incoming_ack),
                &received_length, &io_error) ||
            !raya_frame_decode(incoming_ack, received_length, &received_ack) ||
            !raya_response_tx_accept_ack(&tx, &received_ack))
            goto done;

        ++frames;
    }

    success = rx.ended && rx.received_bytes == total_bytes &&
        offset == total_bytes && frames <= RAYA_MAX_DATA_FRAMES + 2u;

done:
    if (client != INVALID_HANDLE_VALUE)
        CloseHandle(client);
    if (server != INVALID_HANDLE_VALUE)
        CloseHandle(server);
    return success;
}

static void test_message_mode_full_transfers(void)
{
    CHECK("message-mode zero-byte response",
        transfer_response_over_message_pipe(0, RAYA_MAX_FRAME_PAYLOAD, 1));
    CHECK("message-mode chunk-plus-one response",
        transfer_response_over_message_pipe(RAYA_MAX_FRAME_PAYLOAD + 1u,
            RAYA_MAX_FRAME_PAYLOAD, 2));
    CHECK("message-mode exact 256 KiB response",
        transfer_response_over_message_pipe(RAYA_MAX_RESPONSE_BYTES,
            RAYA_MAX_FRAME_PAYLOAD, 3));
}

static void test_error_more_data_is_rejected(void)
{
    HANDLE server = INVALID_HANDLE_VALUE;
    HANDLE client = INVALID_HANDLE_VALUE;
    uint8_t message[128];
    uint8_t small_buffer[32];
    DWORD received = 0;
    DWORD error = ERROR_SUCCESS;
    BOOL result;

    memset(message, 0x5a, sizeof(message));
    if (!create_message_pipe_pair(&server, &client, 4)) {
        CHECK("create pipe for ERROR_MORE_DATA test", FALSE);
        return;
    }

    CHECK("write oversized single pipe message",
        write_message(client, message, sizeof(message)));
    result = read_message(server, small_buffer, sizeof(small_buffer),
        &received, &error);
    CHECK("short message-mode read returns ERROR_MORE_DATA",
        !result && error == ERROR_MORE_DATA && received == sizeof(small_buffer));

    CloseHandle(client);
    CloseHandle(server);
}

static void test_truncated_frame_is_rejected(void)
{
    HANDLE server = INVALID_HANDLE_VALUE;
    HANDLE client = INVALID_HANDLE_VALUE;
    uint8_t truncated[RAYA_FRAME_HEADER_SIZE - 1u] = { 0 };
    uint8_t incoming[RAYA_FRAME_WIRE_CAPACITY];
    DWORD received = 0;
    DWORD error = ERROR_SUCCESS;
    raya_frame_view frame;

    if (!create_message_pipe_pair(&server, &client, 5)) {
        CHECK("create pipe for truncated-frame test", FALSE);
        return;
    }

    CHECK("send truncated frame as one complete message",
        write_message(client, truncated, sizeof(truncated)));
    CHECK("message read succeeds without truncation",
        read_message(server, incoming, sizeof(incoming), &received, &error));
    CHECK("frame decoder rejects truncated message",
        received == sizeof(truncated) &&
        !raya_frame_decode(incoming, received, &frame));

    CloseHandle(client);
    CloseHandle(server);
}

static void test_disconnect_between_frames(void)
{
    HANDLE server = INVALID_HANDLE_VALUE;
    HANDLE client = INVALID_HANDLE_VALUE;
    uint8_t header_payload[8] = { 0, 0, 0, 0, 0, 64, 0, 0 };
    uint8_t wire[RAYA_FRAME_HEADER_SIZE + sizeof(header_payload)];
    uint8_t incoming[RAYA_FRAME_WIRE_CAPACITY];
    size_t wire_length = 0;
    DWORD received = 0;
    DWORD error = ERROR_SUCCESS;
    raya_frame_view header = {
        RAYA_FRAME_RESPONSE_HEADER, 0, header_payload, sizeof(header_payload)
    };
    raya_frame_view decoded;
    BOOL result;

    if (!create_message_pipe_pair(&server, &client, 6)) {
        CHECK("create pipe for disconnect test", FALSE);
        return;
    }

    CHECK("encode response header before disconnect",
        raya_frame_encode(&header, wire, sizeof(wire), &wire_length));
    CHECK("write response header before disconnect",
        write_message(client, wire, (DWORD)wire_length));
    CHECK("server receives response header",
        read_message(server, incoming, sizeof(incoming), &received, &error) &&
        raya_frame_decode(incoming, received, &decoded) &&
        decoded.type == RAYA_FRAME_RESPONSE_HEADER);

    CloseHandle(client);
    client = INVALID_HANDLE_VALUE;
    result = read_message(server, incoming, sizeof(incoming), &received, &error);
    CHECK("disconnect between frames fails closed",
        !result && (error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA));

    CloseHandle(server);
}

static void test_request_error_roundtrip_over_message_pipe(void);
int main(void)
{
    printf("MESSAGE_PIPE_FRAME_TRANSPORT_TEST_BEGIN\n");
    test_message_mode_full_transfers();
    test_error_more_data_is_rejected();
    test_truncated_frame_is_rejected();
    test_disconnect_between_frames();
    test_request_error_roundtrip_over_message_pipe();

    printf("MESSAGE_PIPE_FRAME_TRANSPORT: %u passed, %u failed\n",
        passed, failed);
    return failed == 0 ? 0 : 1;
}

static void test_request_error_roundtrip_over_message_pipe(void)
{
    HANDLE server = INVALID_HANDLE_VALUE;
    HANDLE client = INVALID_HANDLE_VALUE;
    uint8_t request_payload[RAYA_MAX_FRAME_PAYLOAD];
    uint8_t error_payload[8];
    uint8_t ack_payload[4];
    uint8_t wire[RAYA_FRAME_WIRE_CAPACITY];
    uint8_t incoming[RAYA_FRAME_WIRE_CAPACITY];
    uint8_t ack_wire[RAYA_FRAME_HEADER_SIZE + 4u];
    size_t payload_length = 0;
    size_t wire_length = 0;
    size_t ack_length = 0;
    DWORD received = 0;
    DWORD io_error = ERROR_SUCCESS;
    raya_request request;
    raya_request decoded_request;
    raya_error_info error = { RAYA_ERROR_ACCESS_DENIED, false };
    raya_error_info decoded_error;
    raya_frame_view frame;
    raya_frame_view decoded;
    raya_frame_view ack;

    memset(&request, 0, sizeof(request));
    request.operation = RAYA_OPERATION_READ_FILE;
    strcpy_s(request.root_id, sizeof(request.root_id), "fixture");
    wcscpy_s(request.relative_path,
        RAYA_MAX_REQUEST_PATH_CHARS + 1u, L"docs\\private.txt");

    CHECK("create pipe pair for REQUEST/ERROR contract",
        create_message_pipe_pair(&server, &client, 7));
    if (server == INVALID_HANDLE_VALUE || client == INVALID_HANDLE_VALUE)
        return;

    CHECK("encode REQUEST payload",
        raya_request_encode_payload(&request, request_payload,
            sizeof(request_payload), &payload_length));
    if (payload_length == 0)
        goto done;

    frame = (raya_frame_view){
        RAYA_FRAME_REQUEST, 0, request_payload, (uint32_t)payload_length
    };
    CHECK("encode REQUEST frame",
        raya_frame_encode(&frame, wire, sizeof(wire), &wire_length));
    if (wire_length == 0)
        goto done;
    CHECK("send REQUEST as one pipe message",
        write_message(client, wire, (DWORD)wire_length));
    if (!read_message(server, incoming, sizeof(incoming), &received, &io_error)) {
        CHECK("receive REQUEST message", FALSE);
        goto done;
    }
    CHECK("decode REQUEST frame from message",
        raya_frame_decode(incoming, received, &decoded));
    CHECK("validate REQUEST schema from message",
        raya_request_decode_frame(&decoded, &decoded_request));
    CHECK("REQUEST preserves rootId and relative path",
        strcmp(decoded_request.root_id, "fixture") == 0 &&
        wcscmp(decoded_request.relative_path, L"docs\\private.txt") == 0);

    CHECK("encode terminal ACCESS_DENIED payload",
        raya_error_encode_payload(&error, error_payload));
    frame = (raya_frame_view){ RAYA_FRAME_ERROR, 0, error_payload, sizeof(error_payload) };
    CHECK("encode terminal ERROR frame",
        raya_frame_encode(&frame, wire, sizeof(wire), &wire_length));
    CHECK("send ERROR as one pipe message",
        write_message(server, wire, (DWORD)wire_length));
    if (!read_message(client, incoming, sizeof(incoming), &received, &io_error)) {
        CHECK("receive ERROR message", FALSE);
        goto done;
    }
    CHECK("decode ERROR frame from message",
        raya_frame_decode(incoming, received, &decoded));
    CHECK("validate ERROR numeric contract",
        raya_error_decode_frame(&decoded, &decoded_error) &&
        decoded_error.code == RAYA_ERROR_ACCESS_DENIED &&
        !decoded_error.retryable);

    CHECK("construct ACK for terminal ERROR",
        raya_error_make_ack(&decoded, ack_payload, &ack));
    CHECK("encode ACK for terminal ERROR",
        raya_frame_encode(&ack, ack_wire, sizeof(ack_wire), &ack_length));
    CHECK("send ERROR ACK as one pipe message",
        write_message(client, ack_wire, (DWORD)ack_length));
    if (!read_message(server, incoming, sizeof(incoming), &received, &io_error)) {
        CHECK("receive ERROR ACK message", FALSE);
        goto done;
    }
    CHECK("server receives matching ERROR ACK",
        raya_frame_decode(incoming, received, &decoded) &&
        decoded.type == RAYA_FRAME_ACK && decoded.sequence == 0 &&
        decoded.payload_length == 4 && decoded.payload[0] == 0);

done:
    CloseHandle(client);
    CloseHandle(server);
}
