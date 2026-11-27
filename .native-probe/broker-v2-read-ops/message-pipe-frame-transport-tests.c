#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "read-frame.h"
#include "root-registry.h"

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
static void test_end_to_end_read_only_request_over_message_pipe(void);
int main(void)
{
    printf("MESSAGE_PIPE_FRAME_TRANSPORT_TEST_BEGIN\n");
    test_message_mode_full_transfers();
    test_error_more_data_is_rejected();
    test_truncated_frame_is_rejected();
    test_disconnect_between_frames();
    test_request_error_roundtrip_over_message_pipe();
    test_end_to_end_read_only_request_over_message_pipe();

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

static void test_end_to_end_read_only_request_over_message_pipe(void)
{
    wchar_t temp[MAX_PATH];
    wchar_t root_path[MAX_PATH];
    wchar_t file_path[MAX_PATH];
    DWORD temp_length;
    HANDLE server = INVALID_HANDLE_VALUE;
    HANDLE client = INVALID_HANDLE_VALUE;
    RAYA_ROOT_REGISTRY registry = { 0 };
    RAYA_ROOT_CONFIG root_config;
    raya_request request;
    raya_request decoded_request;
    RAYA_REQUEST_CONTEXT context = { 0 };
    raya_response_tx tx;
    raya_response_rx rx;
    uint8_t request_payload[RAYA_MAX_FRAME_PAYLOAD];
    uint8_t wire[RAYA_FRAME_WIRE_CAPACITY];
    uint8_t incoming[RAYA_FRAME_WIRE_CAPACITY];
    uint8_t ack_wire[RAYA_FRAME_HEADER_SIZE + 4u];
    uint8_t file_data[32] = { 0 };
    uint8_t received_body[32] = { 0 };
    size_t request_payload_length = 0;
    size_t wire_length = 0;
    size_t ack_length = 0;
    size_t body_received = 0;
    DWORD file_bytes = 0;
    DWORD received = 0;
    DWORD io_error = ERROR_SUCCESS;
    BOOL registry_open = FALSE;
    BOOL context_open = FALSE;
    BOOL fixture_open = FALSE;
    const char expected[] = "RayaFile";

    temp_length = GetTempPathW(MAX_PATH, temp);
    if (temp_length == 0 || temp_length >= MAX_PATH) {
        CHECK("obtain temp path for read-only vertical slice", FALSE);
        return;
    }

    (void)swprintf_s(root_path, MAX_PATH,
        L"%sRayaReadOnlyE2E-%lu-%llu", temp,
        (unsigned long)GetCurrentProcessId(),
        (unsigned long long)GetTickCount64());
    (void)swprintf_s(file_path, MAX_PATH, L"%s\\data.txt", root_path);

    fixture_open = CreateDirectoryW(root_path, NULL);
    CHECK("create configured root fixture", fixture_open);
    if (!fixture_open)
        goto cleanup;

    {
        HANDLE file = CreateFileW(file_path, GENERIC_WRITE, FILE_SHARE_READ,
            NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        BOOL wrote = FALSE;
        CHECK("create read-only vertical-slice payload file",
            file != INVALID_HANDLE_VALUE);
        if (file == INVALID_HANDLE_VALUE)
            goto cleanup;
        wrote = WriteFile(file, expected, (DWORD)(sizeof(expected) - 1),
            &file_bytes, NULL);
        CloseHandle(file);
        CHECK("write read-only vertical-slice payload",
            wrote && file_bytes == sizeof(expected) - 1);
        if (!wrote || file_bytes != sizeof(expected) - 1)
            goto cleanup;
    }

    root_config.root_id = "fixture";
    root_config.absolute_path = root_path;
    registry_open = raya_root_registry_init(&registry, &root_config, 1);
    CHECK("initialize configured root registry for transport", registry_open);
    if (!registry_open)
        goto cleanup;

    BOOL pipe_created = create_message_pipe_pair(&server, &client, 8);
    CHECK("create message pipe for end-to-end READ_FILE", pipe_created);
    if (!pipe_created)
        goto cleanup;

    memset(&request, 0, sizeof(request));
    request.operation = RAYA_OPERATION_READ_FILE;
    strcpy_s(request.root_id, sizeof(request.root_id), "fixture");
    wcscpy_s(request.relative_path,
        RAYA_MAX_REQUEST_PATH_CHARS + 1u, L"data.txt");

    BOOL request_encoded = raya_request_encode_payload(&request, request_payload,
        sizeof(request_payload), &request_payload_length);
    CHECK("encode end-to-end REQUEST payload", request_encoded);
    if (!request_encoded)
        goto cleanup;

    {
        raya_frame_view request_frame = {
            RAYA_FRAME_REQUEST, 0, request_payload,
            (uint32_t)request_payload_length
        };
        BOOL frame_encoded = raya_frame_encode(&request_frame, wire,
            sizeof(wire), &wire_length);
        CHECK("encode end-to-end REQUEST frame", frame_encoded);
        if (!frame_encoded)
            goto cleanup;
    }

    BOOL request_sent = write_message(client, wire, (DWORD)wire_length);
    CHECK("client sends one REQUEST message", request_sent);
    if (!request_sent)
        goto cleanup;

    if (!read_message(server, incoming, sizeof(incoming), &received, &io_error)) {
        CHECK("server receives REQUEST message", FALSE);
        goto cleanup;
    }

    raya_frame_view request_frame;
    BOOL request_frame_decoded = raya_frame_decode(incoming, received, &request_frame);
    CHECK("server decodes REQUEST frame", request_frame_decoded);
    if (!request_frame_decoded)
        goto cleanup;

    BOOL request_decoded = raya_request_decode_frame(&request_frame, &decoded_request);
    CHECK("server validates REQUEST schema", request_decoded);
    if (!request_decoded)
        goto cleanup;

    context_open = raya_request_context_begin(&registry, &decoded_request,
        1024, &context);
    CHECK("server acquires request-scoped configured-root context", context_open);
    if (!context_open)
        goto cleanup;

    BOOL file_read = context.file_handle != NULL &&
        ReadFile(context.file_handle, file_data, sizeof(file_data),
            &file_bytes, NULL) &&
        file_bytes == sizeof(expected) - 1 &&
        memcmp(file_data, expected, sizeof(expected) - 1) == 0;
    CHECK("request context reads selected file HANDLE", file_read);
    if (!file_read)
        goto cleanup;

    BOOL tx_initialized = raya_response_tx_init(&tx, file_bytes, 4);
    CHECK("initialize bounded response TX/RX", tx_initialized);
    if (!tx_initialized)
        goto cleanup;
    raya_response_rx_init(&rx);

    for (unsigned frame_index = 0;
         !tx.complete && frame_index < RAYA_MAX_DATA_FRAMES + 3u;
         ++frame_index) {
        raya_frame_view outgoing;
        raya_frame_view received_frame;
        raya_frame_view ack;
        const uint8_t *frame_data = NULL;
        uint32_t frame_data_length = 0;
        if (tx.header_sent && tx.sent_bytes < tx.total_bytes) {
            uint32_t remaining = tx.total_bytes - tx.sent_bytes;
            frame_data_length = remaining < tx.chunk_size ? remaining : tx.chunk_size;
            frame_data = file_data + tx.sent_bytes;
        }
        BOOL built = raya_response_tx_next(&tx, frame_data, frame_data_length, &outgoing);
        CHECK("build next bounded response frame", built);
        if (!built)
            goto cleanup;

        BOOL encoded = raya_frame_encode(&outgoing, wire, sizeof(wire), &wire_length);
        CHECK("encode bounded response frame", encoded);
        if (!encoded || wire_length == 0)
            goto cleanup;

        BOOL sent = write_message(server, wire, (DWORD)wire_length);
        CHECK("server sends one response frame message", sent);
        if (!sent)
            goto cleanup;

        if (!read_message(client, incoming, sizeof(incoming), &received, &io_error)) {
            CHECK("client receives response frame message", FALSE);
            goto cleanup;
        }

        BOOL decoded = raya_frame_decode(incoming, received, &received_frame);
        CHECK("client decodes response frame", decoded);
        if (!decoded)
            goto cleanup;

        BOOL accepted = raya_response_rx_accept(&rx, &received_frame);
        CHECK("client validates response sequence and bounds", accepted);
        if (!accepted)
            goto cleanup;

        if (received_frame.type == RAYA_FRAME_DATA) {
            if (body_received + received_frame.payload_length > sizeof(received_body))
                goto cleanup;
            memcpy(received_body + body_received, received_frame.payload,
                received_frame.payload_length);
            body_received += received_frame.payload_length;
        }

        BOOL ack_created = raya_response_rx_make_ack(&rx, &ack);
        CHECK("client constructs matching ACK", ack_created);
        if (!ack_created)
            goto cleanup;

        BOOL ack_encoded = raya_frame_encode(&ack, ack_wire, sizeof(ack_wire), &ack_length);
        CHECK("encode matching ACK frame", ack_encoded);
        if (!ack_encoded || ack_length == 0)
            goto cleanup;

        BOOL ack_sent = write_message(client, ack_wire, (DWORD)ack_length);
        CHECK("client sends one ACK message", ack_sent);
        if (!ack_sent)
            goto cleanup;

        if (!read_message(server, incoming, sizeof(incoming), &received, &io_error)) {
            CHECK("server receives ACK message", FALSE);
            goto cleanup;
        }

        BOOL ack_decoded = raya_frame_decode(incoming, received, &received_frame);
        CHECK("server decodes ACK frame", ack_decoded);
        if (!ack_decoded)
            goto cleanup;

        BOOL ack_accepted = raya_response_tx_accept_ack(&tx, &received_frame);
        CHECK("server accepts matching ACK before next frame", ack_accepted);
        if (!ack_accepted)
            goto cleanup;
    }

    CHECK("response terminates after END ACK", tx.complete && rx.ended);
    CHECK("client received exact file content",
        body_received == sizeof(expected) - 1 &&
        memcmp(received_body, expected, sizeof(expected) - 1) == 0);
    CHECK("registry reference held until request context closes",
        registry.active_contexts == 1);

    BOOL context_closed = raya_request_context_close(&context);
    CHECK("close end-to-end request context", context_closed);
    if (!context_closed)
        goto cleanup;
    context_open = FALSE;

    BOOL registry_closed = raya_root_registry_close(&registry);
    CHECK("close configured root registry after context", registry_closed);
    if (!registry_closed)
        goto cleanup;
    registry_open = FALSE;

cleanup:
    if (context_open)
        (void)raya_request_context_close(&context);
    if (registry_open)
        (void)raya_root_registry_close(&registry);
    if (client != INVALID_HANDLE_VALUE)
        CloseHandle(client);
    if (server != INVALID_HANDLE_VALUE)
        CloseHandle(server);
    if (fixture_open) {
        DeleteFileW(file_path);
        RemoveDirectoryW(root_path);
    }
}
